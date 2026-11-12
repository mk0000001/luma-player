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
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

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
            {"decoder", state->hardwareDecoder}, {"videoDecoder", state->videoDecoder},
            {"dolbyVisionProfile", state->dolbyVisionProfile ? QJsonValue(*state->dolbyVisionProfile) : QJsonValue()},
            {"droppedFrames", state->droppedVideoFrames},
            {"lostAudioBuffers", state->lostAudioBuffers}, {"sourceFps", state->sourceFps},
            {"inputTransfer", state->transferFunction}, {"inputPrimaries", state->colorPrimaries},
            {"outputTransfer", state->outputTransferFunction}, {"outputPrimaries", state->outputPrimaries},
            {"requestedDecoder", state->videoDecoderRequest}, {"requestedTransfer", state->videoColorRequest},
            {"forcedHdr", state->videoColorForced}, {"audioFormat", state->audioFormat},
            {"audioLayout", state->audioLayout}, {"audioBitstreamRequested", state->audioBitstreamRequested},
            {"audioBitstreamActive", state->audioBitstreamActive}, {"audioVolumeExternal", state->audioVolumeExternal},
            {"requestedDolbyVision", state->dolbyVisionRequest}, {"overrideStatus", state->videoOverrideStatus}
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
    buildDeveloperVideoTools(layout);
}

void MainWindow::buildDeveloperVideoTools(QVBoxLayout *layout) {
    using Hardware=VideoOverrides::Hardware;
    using Codec=VideoOverrides::Codec;
    using Color=VideoOverrides::Color;
    using Dolby=VideoOverrides::DolbyVision;
    auto *heading=new QLabel("영상 디코딩과 색상");
    heading->setObjectName("section");
    layout->addWidget(heading);
    auto *form=new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    const auto choice=[form](const QString &label,const char *name){
        auto *box=new ChoiceBox;
        box->setObjectName(name);
        form->addRow(label,box);
        return box;
    };
    auto *hardware=choice("디코딩 장치","developerHardware");
    hardware->addItem("자동",int(Hardware::Auto));
    hardware->addItem("CPU 소프트웨어",int(Hardware::Software));
#ifdef Q_OS_WIN
    hardware->addItem("D3D11VA",int(Hardware::D3D11));
    hardware->addItem("DXVA2",int(Hardware::DXVA2));
#elif defined(Q_OS_LINUX)
    hardware->addItem("VAAPI",int(Hardware::VAAPI));
#endif
    auto *codec=choice("파일 디코더 우선순위","developerCodec");
    codec->addItem("자동",int(Codec::Auto));
    codec->addItem("H.264 · FFmpeg",int(Codec::H264));
    codec->addItem("HEVC · FFmpeg",int(Codec::HEVC));
    codec->addItem("VP9 · FFmpeg",int(Codec::VP9));
    codec->addItem("AV1 · FFmpeg",int(Codec::AV1));
    codec->addItem("AV1 · dav1d",int(Codec::Dav1d));
    auto *color=choice("파일 출력 형식","developerColor");
    color->addItem("자동",int(Color::Auto));
    color->addItem("SDR · BT.709",int(Color::SDR));
    color->addItem("HDR10 · PQ / BT.2020",int(Color::HDR10));
    color->addItem("HDR · HLG / BT.2020",int(Color::HLG));
    auto *dolby=choice("파일 Dolby Vision","developerDolby");
    dolby->addItem("자동",int(Dolby::Auto));
    dolby->addItem("메타데이터 적용",int(Dolby::Apply));
    dolby->addItem("메타데이터 무시",int(Dolby::Ignore));
    dolby->addItem("기본 레이어 + 메타데이터",int(Dolby::BaseLayer));
    dolby->setToolTip("소스의 Dolby Vision 메타데이터를 영상 변환에 사용합니다. HDR10 출력을 함께 요청할 수 있습니다.");
    auto *force=new QCheckBox("SDR 화면에서도 HDR 렌더링 강제");
    force->setObjectName("developerForceHdr");
    force->setToolTip("시험용 설정입니다. SDR 화면에서 색상과 밝기가 달라질 수 있습니다.");
    layout->addWidget(force);

    const std::array boxes{hardware,codec,color,dolby};
    QSettings settings;
    force->setChecked(settings.value("developer/forceHdr",false).toBool());
    for(auto *box:boxes)
        box->setCurrentIndex(std::max(0,box->findData(settings.value("developer/"+box->objectName(),0).toInt())));
    const auto read=[hardware,codec,color,dolby,force]{
        return VideoOverrides{Hardware(hardware->currentData().toInt()),Codec(codec->currentData().toInt()),
                              Color(color->currentData().toInt()),Dolby(dolby->currentData().toInt()),force->isChecked()};
    };
    developerVideoOptions_=read();
    auto *note=new QLabel("출력 형식과 HDR 강제는 현재 파일에 적용합니다. 디코더와 Dolby Vision 설정은 다음에 열 때 적용합니다.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *reset=new QPushButton("영상 설정 자동으로 되돌리기");
    reset->setObjectName("resetDeveloperVideo");
    layout->addWidget(reset);
    developerVideoStatus_=new QLabel("미디어를 열면 디코더와 입·출력 색상을 표시합니다.");
    developerVideoStatus_->setObjectName("developerVideoStatus");
    developerVideoStatus_->setWordWrap(true);
    developerVideoStatus_->setTextFormat(Qt::PlainText);
    layout->addWidget(developerVideoStatus_);
    const auto save=[this,boxes,read,force]{
        if(!developerApproved_)return;
        developerVideoOptions_=read();
        QSettings preferences;
        for(auto *box:boxes)preferences.setValue("developer/"+box->objectName(),box->currentData());
        preferences.setValue("developer/forceHdr",force->isChecked());
        applyVideoOutputPreferences();
    };
    for(auto *box:boxes)connect(box,&QComboBox::currentIndexChanged,this,save);
    connect(force,&QCheckBox::toggled,this,save);
    connect(reset,&QPushButton::clicked,this,[boxes,save,force]{
        for(auto *box:boxes){
            const QSignalBlocker block(box);
            box->setCurrentIndex(0);
        }
        const QSignalBlocker block(force);force->setChecked(false);
        save();
    });
}

void MainWindow::updateDeveloperVideoStatus(const PlaybackState &state) {
    if(!developerApproved_)return;
    const auto show=[](const QString &value){return value.isEmpty()?QString("—"):value;};
    const auto decoder=state.videoDecoder.isEmpty()?state.videoCodec:state.videoDecoder;
    auto text=QString("디코더 요청 %1\n실제 %2 · %3\n입력 %4 / %5\n출력 요청 %6 · 렌더러 %7 / %8\nDolby Vision 요청 %9")
        .arg(show(state.videoDecoderRequest),show(decoder),show(state.hardwareDecoder),
             show(state.transferFunction),show(state.colorPrimaries),show(state.videoColorRequest),
             show(state.outputTransferFunction),show(state.outputPrimaries),show(state.dolbyVisionRequest));
    if(state.dolbyVisionProfile)text+=QString(" · 원본 프로필 %1").arg(*state.dolbyVisionProfile);
    if(state.videoColorForced)text+="\nSDR 화면 HDR 렌더링 강제";
    if(!state.videoOverrideStatus.isEmpty())text+="\n"+state.videoOverrideStatus;
    if(developerVideoStatus_->text()!=text)developerVideoStatus_->setText(text);
}

}
