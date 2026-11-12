#include "main_window.h"
#include "performance_overlay.h"
#include "player_controls.h"
#include "luma_version.h"
#include "subtitles/translation_service.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>

namespace luma {

PerformanceInput MainWindow::performanceInput() const {
    PerformanceInput input;
    input.sourceGeneration = sourceGeneration_;
    input.recoveries = videoRecoveryCount_;
    input.captionWaiting = bufferHolding_;
    input.recognitionBackend = job_.selectedBackend();
    input.translationBackend = translation_->selectedBackend();
    if (active_) {
        input.playback = active_->state();
        input.disc = active_ == discEngine_.get();
        if (liveCaptioningActive())
            input.captionAheadMs = coverage_.bufferedAhead(input.playback->positionMs, input.playback->durationMs);
    }
    return input;
}

QJsonObject MainWindow::developerSnapshot() const {
    const auto input = performanceInput();
    QJsonObject result{
        {"version", LUMA_VERSION}, {"performance", performance_->summary()},
        {"videoRecoveries", input.recoveries}, {"captionAheadMs", input.captionAheadMs},
        {"captionWaiting", input.captionWaiting}, {"recognitionBackend", input.recognitionBackend},
        {"translationBackend", input.translationBackend}, {"captureAllowed", captureApproved_}
    };
    if (const auto &state = input.playback) {
        result.insert("playback", QJsonObject{
            {"engine", input.disc ? "libVLC" : "libmpv"}, {"playing", state->playing},
            {"buffering", state->buffering || state->fileCacheHolding}, {"failed", state->failed},
            {"videoCodec", state->videoCodec}, {"audioCodec", state->audioCodec},
            {"decoder", state->hardwareDecoder}, {"droppedFrames", state->droppedVideoFrames},
            {"lostAudioBuffers", state->lostAudioBuffers}, {"sourceFps", state->sourceFps}
        });
    }
    return result;
}

void MainWindow::buildDeveloperTools(QVBoxLayout *layout) {
    performance_ = new PerformanceOverlay(surface_, [this] { return performanceInput(); });
    auto *heading = new QLabel("성능 진단");
    heading->setObjectName("section");
    layout->addWidget(heading);

    performanceEnabled_ = new QCheckBox("성능 오버레이");
    performanceEnabled_->setObjectName("performanceEnabled");
    performanceEnabled_->setToolTip("Ctrl+Shift+D");
    layout->addWidget(performanceEnabled_);
    auto *options = new QFormLayout;
    options->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *view = new ChoiceBox;
    view->setObjectName("performanceView");
    view->addItem("텍스트 + 그래프", "both");
    view->addItem("텍스트", "text");
    view->addItem("그래프", "graph");
    options->addRow("표시", view);
    auto *corner = new ChoiceBox;
    corner->setObjectName("performanceCorner");
    corner->addItem("오른쪽 위", "right");
    corner->addItem("왼쪽 위", "left");
    options->addRow("위치", corner);
    layout->addLayout(options);

    auto *feedback = new QLabel;
    feedback->setObjectName("performanceFeedback");
    feedback->setWordWrap(true);
    feedback->setTextFormat(Qt::PlainText);
    const auto addButton = [layout](const QString &name, const char *id) {
        auto *button = new QPushButton(name);
        button->setObjectName(id);
        layout->addWidget(button);
        return button;
    };
    auto *copy = addButton("진단 요약 복사", "copyDiagnostics");
    auto *exportCsv = addButton("성능 기록 CSV 저장", "exportPerformance");
    auto *clear = addButton("성능 기록 초기화", "clearPerformance");
    auto *clearLog = addButton("재생 로그 지우기", "clearDeveloperLog");
    layout->addWidget(feedback);

    QSettings settings;
    view->setCurrentIndex(std::max(0, view->findData(settings.value("developer/performanceView", "both"))));
    corner->setCurrentIndex(std::max(0, corner->findData(settings.value("developer/performanceCorner", "right"))));
    const auto applyOptions = [this, view, corner] {
        const auto style = view->currentData().toString();
        const auto position = corner->currentData().toString();
        QSettings preferences;
        preferences.setValue("developer/performanceView", style);
        preferences.setValue("developer/performanceCorner", position);
        performance_->setView(style == "text" ? PerformanceOverlay::View::Text :
                              style == "graph" ? PerformanceOverlay::View::Graph : PerformanceOverlay::View::Both);
        performance_->setLeftAligned(position == "left");
    };
    applyOptions();
    connect(view, &QComboBox::currentIndexChanged, this, applyOptions);
    connect(corner, &QComboBox::currentIndexChanged, this, applyOptions);
    connect(performanceEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        const bool allowed = developerApproved_ && enabled;
        QSettings().setValue("developer/performanceEnabled", allowed);
        if (discEngine_)
            discEngine_->setPerformanceMetricsEnabled(allowed);
        if (fileEngine_)
            fileEngine_->setPerformanceMetricsEnabled(allowed);
        performance_->setMonitoring(allowed);
    });
    performanceEnabled_->setChecked(developerApproved_ && settings.value("developer/performanceEnabled", false).toBool());

    connect(copy, &QPushButton::clicked, this, [this, feedback] {
        if (!developerApproved_)
            return;
        QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(developerSnapshot()).toJson()));
        feedback->setText("진단 요약을 복사했습니다.");
    });
    connect(exportCsv, &QPushButton::clicked, this, [this, feedback] {
        if (!developerApproved_)
            return;
        if (!performance_->hasSamples()) {
            feedback->setText("성능 오버레이를 켜고 측정값을 수집하세요.");
            return;
        }
        const auto history = performance_->csv();
        const auto path = QFileDialog::getSaveFileName(this, "성능 기록 저장", "LumaPlayer-performance.csv", "CSV (*.csv)");
        if (path.isEmpty())
            return;
        QSaveFile output(path);
        const bool saved = output.open(QIODevice::WriteOnly) && output.write(history) == history.size() && output.commit();
        feedback->setText(saved ? "성능 기록을 저장했습니다." : "성능 기록을 저장하지 못했습니다.");
    });
    connect(clear, &QPushButton::clicked, this, [this, feedback] {
        if (developerApproved_) {
            performance_->clearHistory();
            feedback->setText("성능 기록을 초기화했습니다.");
        }
    });
    connect(clearLog, &QPushButton::clicked, this, [this] {
        if (developerApproved_)
            log_->clear();
    });
}

}
