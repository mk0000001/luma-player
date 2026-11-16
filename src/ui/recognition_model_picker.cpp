#include "recognition_model_picker.h"
#include "player_controls.h"
#include "subtitles/ai_backend.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace luma {
namespace {
QString defaultModelFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).absoluteFilePath("models");
}

QString modelName(const QString &fileName) {
    static const QHash<QString, QString> names{
        {"ggml-large-v3-turbo-q5_0.bin", "Whisper large-v3 Turbo · Q5"},
        {"ggml-large-v3-q5_0.bin", "Whisper large-v3 · Q5"},
        {"ggml-base.bin", "Whisper Base"},
        {"ggml-kotoba-whisper-v2.0-q5_0.bin", "Kotoba-Whisper v2.0 · Q5"},
        {heartTranscriptorModelFile, "HeartTranscriptor · 가창 · Q5"}
    };
    return names.value(fileName.toLower(), fileName);
}
}

RecognitionModelPicker::RecognitionModelPicker(QWidget *parent)
    : QWidget(parent), models_(new ChoiceBox(this)), folderPath_(new QLineEdit(this)),
      watcher_(new QFileSystemWatcher(this)), refreshTimer_(new QTimer(this)) {
    const auto savedFolder = QSettings().value("ai/modelDirectory").toString();
    folder_ = QDir::isAbsolutePath(savedFolder) && !savedFolder.isEmpty()
        ? QDir::cleanPath(savedFolder) : defaultModelFolder();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto *label = new QLabel("음성 인식 모델", this);
    label->setObjectName("section");
    label->setBuddy(models_);
    layout->addWidget(label);
    models_->setObjectName("recognitionModelList");
    models_->setAccessibleName("음성 인식 모델");
    layout->addWidget(models_);

    folderPath_->setObjectName("recognitionModelFolder");
    folderPath_->setAccessibleName("모델 폴더");
    folderPath_->setReadOnly(true);
    layout->addWidget(folderPath_);
    showFolderPath();
    auto *buttons = new QHBoxLayout;
    auto *change = new QPushButton("모델 폴더 변경", this);
    auto *open = new QPushButton("폴더 열기", this);
    change->setObjectName("changeRecognitionModelFolder");
    open->setObjectName("openRecognitionModelFolder");
    change->setCursor(Qt::PointingHandCursor);
    open->setCursor(Qt::PointingHandCursor);
    buttons->addWidget(change, 1);
    buttons->addWidget(open, 1);
    layout->addLayout(buttons);

    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(120);
    const auto queueRefresh = [this] { refreshTimer_->start(); };
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, queueRefresh);
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, queueRefresh);
    connect(refreshTimer_, &QTimer::timeout, this, &RecognitionModelPicker::refreshModels);
    connect(change, &QPushButton::clicked, this, &RecognitionModelPicker::changeFolder);
    connect(open, &QPushButton::clicked, this, &RecognitionModelPicker::openFolder);
    connect(models_, &QComboBox::activated, this, [this](int index) {
        const auto path = models_->itemData(index).toString();
        if (!QFileInfo(path).isFile()) {
            refreshModels();
            return;
        }
        QSettings().setValue("ai/model", path);
        emit modelChanged(path);
    });
    refreshModels();
}

QString RecognitionModelPicker::selectedModel() const {
    return models_->currentData().toString();
}

void RecognitionModelPicker::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refreshModels();
}

void RecognitionModelPicker::refreshModels() {
    const auto previous = selectedModel();
    const QFileInfo savedInfo(QSettings().value("ai/model").toString());
    const auto saved = savedInfo.isFile() ? savedInfo.absoluteFilePath() : QString{};
    const QDir bundled(QCoreApplication::applicationDirPath() + "/runtime/models");
    QStringList paths, watched, directories;
    for (const auto &folder : {bundled.path(), folder_}) {
        const auto path = QDir(folder).canonicalPath();
        if (path.isEmpty() || directories.contains(path))
            continue;
        directories.append(path);
        watched.append(path);
        for (const auto &file : QDir(path).entryInfoList({"*.bin"}, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase)) {
            if (file.fileName().endsWith("-encoder-openvino.bin", Qt::CaseInsensitive))
                continue;
            if (isWhisperGgmlModel(file.absoluteFilePath()))
                paths.append(file.absoluteFilePath());
            else
                watched.append(file.absoluteFilePath());
        }
    }
    // A model chosen by the previous file dialog may be outside both scan directories.
    if (!saved.isEmpty() && !paths.contains(saved)) {
        paths.append(saved);
        watched.append(saved);
    }

    const auto currentWatches = watcher_->directories() + watcher_->files();
    for (const auto &path : currentWatches)
        if (!watched.contains(path)) watcher_->removePath(path);
    for (const auto &path : watched)
        if (!currentWatches.contains(path)) watcher_->addPath(path);

    QStringList listed;
    for (int i = 0; i < models_->count(); ++i) {
        const auto path = models_->itemData(i).toString();
        if (!path.isEmpty()) listed.append(path);
    }
    if (models_->count() && listed == paths)
        return;

    models_->hidePopup();
    models_->clear();
    models_->setEnabled(!paths.isEmpty());
    if (paths.isEmpty()) {
        models_->addItem("사용할 모델 없음");
    } else {
        QHash<QString, int> occurrences;
        for (const auto &path : paths) ++occurrences[QFileInfo(path).fileName().toLower()];
        for (const auto &path : paths) {
            const QFileInfo file(path);
            auto name = modelName(file.fileName());
            if (occurrences.value(file.fileName().toLower()) > 1)
                name += " · " + QDir::toNativeSeparators(file.absolutePath());
            models_->addItem(name, path);
        }
        int index = models_->findData(previous);
        if (index < 0) index = models_->findData(saved);
        if (index < 0) {
            const auto singing = std::find_if(paths.cbegin(), paths.cend(), [](const QString &path) {
                return QFileInfo(path).fileName().compare(heartTranscriptorModelFile, Qt::CaseInsensitive) == 0;
            });
            if (singing != paths.cend()) index = models_->findData(*singing);
        }
        if (index < 0) {
            for (const auto *name : {"ggml-large-v3-turbo-q5_0.bin", "ggml-large-v3-q5_0.bin", "ggml-base.bin"}) {
                index = models_->findData(bundled.absoluteFilePath(name));
                if (index >= 0) break;
            }
        }
        models_->setCurrentIndex(index < 0 ? 0 : index);
    }
    if (const auto selected = selectedModel(); selected != previous)
        emit modelChanged(selected);
}

void RecognitionModelPicker::showFolderPath() {
    folderPath_->setText(QDir::toNativeSeparators(folder_));
    folderPath_->setCursorPosition(0);
}

void RecognitionModelPicker::changeFolder() {
    const auto start = QDir(folder_).exists() ? folder_ : QDir::homePath();
    const auto path = QFileDialog::getExistingDirectory(this, "음성 인식 모델 폴더", start);
    if (path.isEmpty())
        return;
    folder_ = QDir::cleanPath(path);
    QSettings().setValue("ai/modelDirectory", folder_);
    showFolderPath();
    refreshModels();
}

void RecognitionModelPicker::openFolder() {
    if (folder_ == defaultModelFolder())
        QDir().mkpath(folder_);
    if (!QDir(folder_).exists() || !QDesktopServices::openUrl(QUrl::fromLocalFile(folder_))) {
        QMessageBox::warning(this, "모델 폴더", "모델 폴더를 열 수 없습니다. ‘모델 폴더 변경’에서 위치를 확인하세요.");
        return;
    }
    refreshModels();
}
}
