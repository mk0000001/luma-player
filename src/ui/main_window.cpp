#include "main_window.h"

#include "caption_overlay.h"
#include "caption_preparation_dialog.h"
#include "fullscreen_controls.h"
#include "lan_playback_dialog.h"
#include "lan_receiver_dialog.h"
#include "player_controls.h"
#include "performance_overlay.h"
#include "theme.h"
#include "video_surface.h"
#include "luma_version.h"
#include "media/platform_features.h"
#include "subtitles/ai_devices.h"
#include "subtitles/subtitle_file.h"
#include "subtitles/translation_devices.h"
#include "subtitles/translation_service.h"
#include "subtitles/translation_support.h"
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalBlocker>
#include <QTime>
#include <QTimer>
#include <QWindow>

namespace luma {

MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),job_({},this),subtitleCache_(QDir::tempPath()+"/luma-captions-XXXXXX"){
    setWindowTitle("LumaPlayer");resize(1440,900);setMinimumSize(1080,680);setAcceptDrops(true);setWindowIcon(playerApplicationIcon());
    buildUi();fullscreenControls_=new FullscreenControls(this,transport_,surface_);
    connect(static_cast<VideoSurface*>(surface_),&VideoSurface::urlsDropped,this,&MainWindow::openDroppedUrls);
    connect(fullscreenControls_,&FullscreenControls::reservedHeightChanged,captions_,&CaptionOverlay::setBottomInset);captions_->setBottomInset(fullscreenControls_->reservedHeight());
    captureGuard_=new CaptureGuard(this);captureGuard_->watch(static_cast<VideoSurface*>(surface_)->videoWindow());captureGuard_->watch(captions_);
    const auto qualityModel=QCoreApplication::applicationDirPath()+"/runtime/models/ggml-large-v3-turbo-q5_0.bin";
    model_=QSettings().value("ai/model",QFileInfo::exists(qualityModel)?qualityModel:QCoreApplication::applicationDirPath()+"/runtime/models/ggml-base.bin").toString();
    lastUnmutedVolume_=QSettings().value("playback/lastUnmutedVolume",70).toInt();qApp->installEventFilter(this);
    translation_=new TranslationService({},this);
    const auto qualityTranslation=QCoreApplication::applicationDirPath()+"/runtime/translation/models/Hy-MT2-7B-Q4_K_M.gguf";
    translation_->setModel(QSettings().value("translation/model",QFileInfo::exists(qualityTranslation)?qualityTranslation:QString{}).toString());
    translation_->setGlossary(configuredTranslationGlossary());
    connectDeveloperDiagnostics();
    connect(translation_,&TranslationService::translatedCue,this,[this](quint64 generation,const QString &key,const QString &,const QString &korean){
        if(generation!=translation_->generation() || !translationEnabled_->isChecked())return;
        translationPending_.remove(key);translations_.insert(key,korean);rebuildManagedCaptions();translationStatus_->setText(QString("문맥을 참고한 한국어 번역 %1개 준비됨").arg(translations_.size()));
    });
    connect(translation_,&TranslationService::skippedCue,this,[this](quint64 generation,const QString &key,const QString &,const QString &message){
        if(generation!=translation_->generation() || !translationEnabled_->isChecked())return;
        translationPending_.remove(key);
        if(!translationSkipped_.contains(key)){
            translationSkipped_.insert(key);translationSkippedOrder_.append(key);
            while(translationSkippedOrder_.size()>256)translationSkipped_.remove(translationSkippedOrder_.takeFirst());
        }
        translationStatus_->setText("이 자막의 번역을 건너뛰고 다음 구간을 계속 준비합니다.\n"+message);
        appendLog("번역 한 구간 제외: "+message);QTimer::singleShot(0,this,&MainWindow::scheduleTranslations);
    });
    connect(translation_,&TranslationService::progress,translationStatus_,&QLabel::setText);
    connect(translation_,&TranslationService::backendSelected,this,[this](const QString &backend){
        const QString compatibility=translation_->selectedVulkanCompatibility()?" · 저VRAM":QString{};
        translationBackend_->setText("번역 · "+backend+compatibility);
    });
    connect(translation_,&TranslationService::failed,this,[this](const QString &message){translationPending_.clear();translationFailed_=true;translationStatus_->setText(message);appendLog("번역: "+message);});
    discEngine_=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::Disc,QString{},this);
    fileEngine_=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::File,QString{},this);bindEngine(discEngine_.get());bindEngine(fileEngine_.get());
    subtitleFileLoader_=new SubtitleFileLoader({},this);
    connect(subtitleFileLoader_,&SubtitleFileLoader::ready,this,[this](quint64 generation,QVector<SubtitleCue> cues){if(generation==subtitleFileLoader_->generation() && active_)applyExternalCaptions(std::move(cues));});
    connect(subtitleFileLoader_,&SubtitleFileLoader::failed,this,[this](quint64 generation,const QString &message){if(generation==subtitleFileLoader_->generation() && active_)subtitleImportFailed(message);});
    updateResumeButton();
    connect(&job_,&SubtitleJob::progress,this,[this](const QString &message){aiStatus_->setText(message);});
    connect(&job_,&SubtitleJob::backendSelected,this,[this](const QString &backend){
        auto text=backend=="NPU"?QString("음성 인코더 · NPU (실행 확인)"):backend=="GPU"?QString("음성 인식 · GPU (실행 확인)"):QString("음성 인식 · ")+backend+" (실행 확인)";
        if(job_.selectedAlignment()=="dtw")text+=" · 음성 시점 정렬";
        accelerator_->setText(text);
    });
    connect(&job_,&SubtitleJob::failed,this,[this](const QString &message){
        coverage_.cancel();aiFailed_=true;appendLog(message);
        if(aiEnabled_->isChecked() && aiRetryCount_<3){
            const int delay=2000*(1<<aiRetryCount_++);nextAiAttemptMs_=QDateTime::currentMSecsSinceEpoch()+delay;
            aiStatus_->setText(message+QString("\n%1초 후 다시 시도합니다. (%2/3)").arg(delay/1000).arg(aiRetryCount_));
            bufferStatus_->setText("자막 준비 재시도 중 · ‘자막 없이 계속’으로 바로 재생할 수 있습니다.");
        }else{
            nextAiAttemptMs_=0;holdForCaptionBuffer();
            aiStatus_->setText(message+"\n자동 재시도를 마쳤습니다. 모델·오디오를 확인하고 다시 시도하세요.");
            bufferStatus_->setText("최소 자막 버퍼를 기다립니다. 다시 시도하거나 ‘자막 없이 계속’을 선택하세요.");
        }
        status_->setText(bufferStatus_->text());
    });
    connect(&job_,&SubtitleJob::ready,this,[this](quint64 generation,QVector<SubtitleCue> cues){
        if(generation!=job_.generation() || !active_ || !aiEnabled_->isChecked())return;
        coverage_.complete();aiFailed_=false;aiRetryCount_=0;nextAiAttemptMs_=0;generatedCues_+=cues;presentationDirty_=true;rebuildManagedCaptions();updateCaptionBuffer();
        aiStatus_->setText(QString("자막 %1개 생성 · 기기에서 처리됨").arg(generatedCues_.size()));
        QTimer::singleShot(0,this,&MainWindow::scheduleSubtitles);
    });
    updateAiPreferences();updateTranslationPreferences();updateMotionSettings();
    deviceProbe_=new AiDeviceProbe({},this);
    connect(deviceProbe_,&AiDeviceProbe::devicesReady,this,[this](const QJsonArray &devices){
        const auto selected=QSettings().value("ai/gpu").toString();QSignalBlocker guard(aiGpu_);aiGpu_->clear();aiGpu_->addItem("자동 GPU 선택",QString{});
        for(const auto &entry:devices){const auto device=entry.toObject();if(device.value("type").toString().compare("GPU",Qt::CaseInsensitive)!=0)continue;aiGpu_->addItem(device.value("name").toString()+" · "+device.value("backend").toString(),device.value("id").toString());}
        int index=aiGpu_->findData(selected);if(index<0 && !selected.isEmpty()){aiGpu_->addItem("저장된 GPU · "+selected,selected);index=aiGpu_->count()-1;}if(index>=0)aiGpu_->setCurrentIndex(index);
    });
    connect(deviceProbe_,&AiDeviceProbe::failed,this,[this](const QString &message){
        recordDeveloperEvent("asr_device_probe_failed","device_enumeration_failed");
        appendLog("AI 기기 검색: "+message);aiGpu_->setToolTip(message);
    });
    auto *timer=new QTimer(this);timer->setInterval(500);connect(timer,&QTimer::timeout,this,[this]{scheduleSubtitles();scheduleTranslations();});timer->start();
    translationDeviceProbe_=new TranslationDeviceProbe({},this);
    connect(translationDeviceProbe_,&TranslationDeviceProbe::devicesReady,this,[this](const QJsonArray &devices){
        const auto saved=QSettings().value("translation/gpu").toString();QSignalBlocker blocked(translationGpu_);translationGpu_->clear();translationGpu_->addItem("자동 GPU 선택",QString{});
        for(const auto &entry:devices){const auto device=entry.toObject();if(device.value("type").toString()!="gpu")continue;translationGpu_->addItem(device.value("backend").toString()+" · "+device.value("name").toString(),device.value("id").toString());}
        int index=translationGpu_->findData(saved);if(index<0 && !saved.isEmpty()){translationGpu_->addItem("저장된 GPU · "+saved,saved);index=translationGpu_->count()-1;}if(index>=0)translationGpu_->setCurrentIndex(index);
    });
    connect(translationDeviceProbe_,&TranslationDeviceProbe::failed,this,[this](const QString &message){
        recordDeveloperEvent("translation_device_probe_failed","device_enumeration_failed");
        appendLog("번역 장치 확인: "+message);
    });
    QTimer::singleShot(0,this,[this]{
        discoverDisc();deviceProbe_->start();translationDeviceProbe_->start();updateDisplayStatus();applyTheme();
        if(auto *window=windowHandle())connect(window,&QWindow::screenChanged,this,&MainWindow::updateDisplayStatus,Qt::QueuedConnection);
    });
}

MainWindow::~MainWindow(){discProbeClosing_=true;discPlayPending_=false;stopLanReceiver();delete lanReceiverDialog_;lanReceiverDialog_=nullptr;delete lanPlaybackDialog_;lanPlaybackDialog_=nullptr;performance_->setMonitoring(false);setDeveloperDiagnosticCollection(false);savePlaybackProgress(true);qApp->removeEventFilter(this);subtitleFileLoader_->cancel();captions_->hide();static_cast<VideoSurface*>(surface_)->setVideoActive(false);job_.shutdown();translation_->cancel();active_=nullptr;fileEngine_.reset();discEngine_.reset();}

void MainWindow::appendLog(const QString &message){log_->appendPlainText(QTime::currentTime().toString("HH:mm:ss")+"  "+message);}

QJsonObject MainWindow::diagnostics()const{
    QJsonObject result{{"version",LUMA_VERSION},{"playingSamples",decodedSamples_},{"discRuntime",discEngine_->available()},{"fileRuntime",fileEngine_->available()},{"aiCueCount",generatedCues_.size()},{"aiBackend",job_.selectedBackend()},{"aiEnabled",aiEnabled_->isChecked()},{"aiRetryCount",aiRetryCount_},{"aiRetryAt",nextAiAttemptMs_},{"captionBufferHolding",bufferHolding_},{"captionResumeRequested",bufferResume_},{"captionBufferBypass",bufferBypass_},{"hangulReadings",readingEnabled_->isChecked()},{"readingRuntime",reading_.available()},{"originalCaptions",originalEnabled_->isChecked()},{"koreanTranslations",translationEnabled_->isChecked()},{"translationCount",translations_.size()},{"translationBackend",translation_?translation_->selectedBackend():QString{}},{"bundledFonts",qApp->property("lumaFontsLoaded").toBool()},{"externalCueCount",externalCues_.size()},{"theme",playerUsesDarkTheme()?"dark":"light"},{"fullscreen",full_},{"transportVisible",transport_->isVisible()},{"transportHeight",transport_->height()},{"developerMode",developerApproved_},{"captureAllowed",captureApproved_},{"videoRecoveryCount",videoRecoveryCount_},{"videoRecoveryExhausted",videoRecoveryExhausted_}};
    result.insert("translationSkippedCount",translationSkipped_.size());result.insert("translationPendingCount",translationPending_.size());result.insert("translationFailed",translationFailed_);
    result.insert("translationGlossaryEntries",translation_->glossary().size());
    result.insert("pictureInPicture",pictureInPicture_);
    result.insert("lanReceiving",lanRemotePlayback_);result.insert("lanReceiveSeekable",lanRemoteSeekable_);
    if(lanRemotePlayback_ && active_)result.insert("lanSubtitleCueActive",active_->state().lanSubtitleCueActive);
    result.insert("performanceOverlay",performance_->summary());
    result.insert("preparedCaptionsApplied",preparedApplied_);result.insert("preparedCueCount",preparedCaptions_?preparedCaptions_->originals.size():0);result.insert("preparedTranslationCount",preparedCaptions_?preparedCaptions_->translations.size():0);
    if(preparationDialog_)result.insert("preparation",preparationDialog_->diagnostics());
    result.insert("aiTiming",job_.selectedAlignment());
    const auto request=job_.currentRequest();
    if(active_)result.insert("navigationTitle",active_->state().navigationTitle);
    if(active_)result.insert("discMenus",active_->state().discMenus);
    result.insert("pendingAudioTrack",pendingAudio_);result.insert("pendingChapter",pendingChapter_);result.insert("pendingTitle",pendingTitle_);
    result.insert("jobBusy",job_.busy());result.insert("requestedAudioTrack",request.audioTrackId);result.insert("requestedAudioIndex",request.audioIndex);result.insert("requestedPlaying",wantsPlayback());
    result.insert("asrModel",QFileInfo(model_).fileName());result.insert("requestedCaptionStartMs",request.startMs);result.insert("requestedCaptionDurationMs",request.durationMs);result.insert("resumePending",pendingResume_.has_value());
    if(active_)result.insert("timelineReady",active_->state().timelineReady);
    result.insert("discRoot",discInfo_.root);result.insert("discValid",discInfo_.valid);result.insert("discProtected",discInfo_.aacs || discInfo_.bdplus);
    if(active_)result.insert("currentAudioTrack",active_->state().currentAudio);
    if(captureGuard_){result.insert("captureRequested",captureGuard_->enabled());result.insert("captureApplied",captureGuard_->applied());result.insert("captureStatus",captureGuard_->status());}result.insert("display",displayCapabilities(winId()));
    if(active_){const auto &state=active_->state();result.insert("engine",active_->name());result.insert("positionMs",state.positionMs);result.insert("durationMs",state.durationMs);result.insert("playing",state.playing);result.insert("failed",state.failed);result.insert("title",state.currentTitle);result.insert("chapter",state.currentChapter);result.insert("chapterCount",state.chapters.size());result.insert("audioTrackCount",state.audioTracks.size());result.insert("videoCodec",state.videoCodec);result.insert("hardwareDecoder",state.hardwareDecoder);result.insert("audioCodec",state.audioCodec);result.insert("decodedVideo",state.decodedVideo);result.insert("decodedAudio",state.decodedAudio);result.insert("displayedPictures",state.displayedPictures);result.insert("playedAudioBuffers",state.playedAudioBuffers);result.insert("captionBufferedAheadMs",coverage_.bufferedAhead(state.positionMs,state.durationMs));result.insert("sourceFps",state.sourceFps);result.insert("filteredFps",state.filteredFps);result.insert("displayFps",state.displayFps);result.insert("motionStatus",state.motionStatus);}
    if(active_){
        const auto &state=active_->state();
        result.insert("requestedVideoTransfer",state.videoColorRequest);
        result.insert("requestedVideoDecoder",state.videoDecoderRequest);
        result.insert("requestedDolbyVision",state.dolbyVisionRequest);
        result.insert("videoDecoder",state.videoDecoder);
        result.insert("videoInputTransfer",state.transferFunction);
        result.insert("videoInputPrimaries",state.colorPrimaries);
        result.insert("videoTransfer",state.outputTransferFunction);
        result.insert("videoPrimaries",state.outputPrimaries);
        result.insert("forcedHdr",state.videoColorForced);
        result.insert("videoOverrideStatus",state.videoOverrideStatus);
        result.insert("audioCodecFormat",state.audioFormat);
        result.insert("audioLayout",state.audioLayout);
        result.insert("audioOutput",state.audioOutput);
        result.insert("audioBitstreamRequested",state.audioBitstreamRequested);
        result.insert("audioBitstreamActive",state.audioBitstreamActive);
        result.insert("audioVolumeExternal",state.audioVolumeExternal);
        result.insert("audioSettingsError",state.audioSettingsError);
        result.insert("mediaBuffering",state.buffering);
        result.insert("droppedVideoFrames",state.droppedVideoFrames);
        result.insert("lostAudioBuffers",state.lostAudioBuffers);
    }
    QJsonArray messages;const auto lines=log_->toPlainText().split('\n');for(const auto &line:lines.mid(std::max<qsizetype>(0,lines.size()-30)))messages.append(line);result.insert("recentMessages",messages);return result;
}

}
