#include "ai_acceleration_setup.h"

#include "media/platform_features.h"
#include "subtitles/ai_devices.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <array>
#include <windows.h>

namespace luma {
namespace {
constexpr auto setupObjectName = "aiAccelerationSetup";

bool packageFilesAvailable(const QDir &root) {
    return QFileInfo(root.filePath("tools/install-ai-acceleration.ps1")).isFile();
}

QLabel *plainLabel(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}

class SetupDialog final : public QDialog {
public:
    explicit SetupDialog(QWidget *parent)
        : QDialog(parent), root_(QCoreApplication::applicationDirPath()), deviceProbe_(root_.filePath("runtime"), this) {
        setObjectName(setupObjectName);
        setWindowTitle(QStringLiteral("AI 가속 설정"));
        setAttribute(Qt::WA_DeleteOnClose);
        resize(530, 1);

        auto *layout = new QVBoxLayout(this);
        hardware_ = plainLabel({}, this);
        hardware_->setObjectName("aiAccelerationHardware");
        layout->addWidget(hardware_);
        auto *notice = plainLabel(QStringLiteral("권장 항목 외에도 설치할 수 있습니다. 실제 가속은 호환 장치와 드라이버를 확인한 뒤 사용합니다. 라이브러리는 LumaPlayer 폴더에 설치됩니다."), this);
        notice->setObjectName("muted");
        layout->addWidget(notice);

        for (auto &backend : backends_) {
            auto *group = new QGroupBox(backend.title, this);
            auto *row = new QHBoxLayout(group);
            backend.description = plainLabel({}, group);
            row->addWidget(backend.description, 1);
            backend.install = new QPushButton(group);
            backend.install->setObjectName("installAcceleration-" + backend.id);
            row->addWidget(backend.install);
            connect(backend.install, &QPushButton::clicked, this, [this, id=backend.id] { install(id); });
            layout->addWidget(group);
        }

        status_ = plainLabel({}, this);
        status_->setObjectName("aiAccelerationStatus");
        layout->addWidget(status_);
        details_ = new QPlainTextEdit(this);
        details_->setReadOnly(true);
        details_->setMaximumHeight(110);
        details_->hide();
        layout->addWidget(details_);
        progress_ = new QProgressBar(this);
        progress_->setRange(0, 0);
        progress_->setTextVisible(false);
        progress_->hide();
        layout->addWidget(progress_);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        close_ = buttons->button(QDialogButtonBox::Close);
        close_->setText(QStringLiteral("닫기"));
        connect(buttons, &QDialogButtonBox::rejected, this, &SetupDialog::reject);
        layout->addWidget(buttons);

        QStringList gpuNames;
        for (const auto &value : videoAdapters()) {
            const auto adapter = value.toObject();
            const int vendor = adapter.value("vendorId").toInt();
            if (vendor == 0x10de) nvidia_ = true;
            if (vendor == 0x1002) amd_ = true;
            if (vendor == 0x8086) intel_ = true;
            if (vendor == 0x10de || vendor == 0x1002 || vendor == 0x8086)
                gpuNames.append(adapter.value("name").toString());
        }
        hardware_->setText(gpuNames.isEmpty() ? QStringLiteral("호환 여부를 확인할 GPU를 찾지 못했습니다.")
                                              : QStringLiteral("그래픽 장치 · %1").arg(gpuNames.join(", ")));
        if (!packageFilesAvailable(root_))
            status_->setText(QStringLiteral("이 실행 폴더에 설치 도구가 없습니다. 전체 설치본이나 포터블 패키지에서 실행해 주세요."));
        updateOptions();

        connect(&deviceProbe_, &AiDeviceProbe::devicesReady, this, [this](const QJsonArray &devices) {
            for (const auto &value : devices) {
                const auto device = value.toObject();
                if (device.value("type").toString() != "npu") continue;
                hardware_->setText(hardware_->text() + QStringLiteral("\nNPU · ") + device.value("name").toString());
                npu_ = true;
            }
            updateOptions();
        });
        if (file("runtime/ai/luma-whisper-worker.exe"))
            QTimer::singleShot(0, &deviceProbe_, &AiDeviceProbe::start);

        process_.setProcessChannelMode(QProcess::MergedChannels);
        process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
            arguments->flags |= CREATE_NO_WINDOW;
        });
        connect(&process_, &QProcess::readyRead, this, &SetupDialog::readOutput);
        connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) complete(false, -1);
        });
        connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            readOutput();
            complete(code == 0 && status == QProcess::NormalExit, code);
        });
    }

    ~SetupDialog() override {
        process_.blockSignals(true);
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            process_.waitForFinished(1500);
        }
    }

    void reject() override {
        if (!busy_) QDialog::reject();
    }

private:
    struct Backend {
        QString id;
        QString title;
        QLabel *description = nullptr;
        QPushButton *install = nullptr;
    };

    bool file(const QString &relative) const { return QFileInfo(root_.filePath(relative)).isFile(); }
    bool cudaDependencies(const QString &engine) const {
        const auto complete = [this](const QString &prefix) {
            return file(prefix + "/cudart64_12.dll") && file(prefix + "/cublas64_12.dll") &&
                   file(prefix + "/cublasLt64_12.dll");
        };
        return complete("runtime/cuda") || complete(engine);
    }

    void updateOptions() {
        const bool canInstall = packageFilesAvailable(root_) && !busy_;
        for (const auto &backend : backends_) {
            bool recommended;
            bool asr;
            bool translation;
            if (backend.id == "cuda") {
                recommended = nvidia_;
                asr = file("runtime/ai/ggml-cuda.dll") && cudaDependencies("runtime/ai");
                translation = file("runtime/translation/cuda/llama-server.exe") &&
                              file("runtime/translation/cuda/ggml-cuda.dll") && cudaDependencies("runtime/translation/cuda");
            } else if (backend.id == "vulkan") {
                recommended = amd_ || intel_;
                asr = file("runtime/ai/ggml-vulkan.dll");
                translation = file("runtime/translation/vulkan/llama-server.exe") && file("runtime/translation/vulkan/ggml-vulkan.dll");
            } else {
                recommended = intel_ || npu_;
                asr = file("runtime/ai/openvino_intel_npu_plugin.dll");
                translation = file("runtime/translation/openvino/llama-server.exe") && file("runtime/translation/openvino/ggml-openvino.dll");
            }
            const auto label = QStringLiteral("%1\n음성 인식 · %2\n번역 · %3").arg(
                recommended ? QStringLiteral("이 PC에 권장") : QStringLiteral("수동 선택 가능"),
                asr ? QStringLiteral("라이브러리 있음") : QStringLiteral("라이브러리 없음"),
                translation ? QStringLiteral("라이브러리 있음") : QStringLiteral("라이브러리 없음"));
            backend.description->setText(label);
            backend.install->setText(backend.id == "cuda" && asr && translation ? QStringLiteral("설치 확인") :
                                     (backend.id == "cuda" ? QStringLiteral("설치") : QStringLiteral("번역 설치·복구")));
            backend.install->setEnabled(canInstall);
        }
    }

    void install(const QString &backend) {
        if (busy_ || !packageFilesAvailable(root_)) return;
        wchar_t directory[MAX_PATH];
        const auto length = GetSystemDirectoryW(directory, MAX_PATH);
        if (!length || length >= MAX_PATH) {
            status_->setText(QStringLiteral("Windows PowerShell의 시스템 경로를 확인할 수 없습니다."));
            return;
        }
        const auto shell = QDir(QString::fromWCharArray(directory, int(length))).filePath(
            "WindowsPowerShell/v1.0/powershell.exe");
        busy_ = true;
        activeBackend_ = backend;
        output_.clear();
        details_->hide();
        status_->setText(QStringLiteral("라이브러리를 확인하고 설치하는 중입니다…"));
        close_->setEnabled(false);
        progress_->show();
        updateOptions();
        process_.setWorkingDirectory(root_.absolutePath());
        process_.setProgram(shell);
        process_.setArguments({"-NoProfile", "-NonInteractive", "-File",
            root_.filePath("tools/install-ai-acceleration.ps1"), "-PlayerDirectory", root_.absolutePath(),
            "-Backend", backend});
        process_.start();
    }

    void readOutput() {
        output_.append(process_.readAll());
        constexpr qsizetype limit = 8192;
        if (output_.size() > limit) output_.remove(0, output_.size() - limit);
    }

    void complete(bool success, int exitCode) {
        busy_ = false;
        const QString result = success ? "verified" : "failed";
        QSettings().setValue("ai/accelerationLastInstall",
                             QVariantMap{{"backend", activeBackend_}, {"result", result}, {"exitCode", exitCode}});
        progress_->hide();
        close_->setEnabled(true);
        if (success) {
            status_->setText(QStringLiteral("라이브러리를 확인했습니다. LumaPlayer를 다시 열면 가속 장치를 다시 검사합니다."));
        } else {
            status_->setText(QStringLiteral("설치에 실패했습니다. 설치 폴더의 쓰기 권한, 네트워크 연결, PowerShell 실행 정책을 확인해 주세요."));
            const auto detail = QString::fromLocal8Bit(output_).trimmed();
            details_->setPlainText(detail.isEmpty() ? process_.errorString() : detail);
            details_->show();
        }
        updateOptions();
    }

    const QDir root_;
    AiDeviceProbe deviceProbe_;
    QProcess process_;
    QLabel *hardware_;
    QLabel *status_;
    QPlainTextEdit *details_;
    QProgressBar *progress_;
    QPushButton *close_;
    QByteArray output_;
    QString activeBackend_;
    bool nvidia_ = false;
    bool amd_ = false;
    bool intel_ = false;
    bool npu_ = false;
    bool busy_ = false;
    std::array<Backend, 3> backends_{{
        {"cuda", QStringLiteral("NVIDIA CUDA")},
        {"vulkan", QStringLiteral("Vulkan · AMD Radeon / Intel / NVIDIA")},
        {"openvino", QStringLiteral("Intel OpenVINO · NPU / GPU")}
    }};
};
}

void showAiAccelerationSetup(QWidget *parent) {
    if (auto *existing = parent->findChild<QDialog *>(setupObjectName)) {
        existing->raise();
        existing->activateWindow();
        return;
    }
    (new SetupDialog(parent))->open();
}

}
