#include "main_window.h"

#include "caption_overlay.h"
#include "caption_preparation_dialog.h"
#include "lan_playback_dialog.h"
#include "prepared_caption_binding.h"
#include "video_surface.h"
#include "subtitles/ai_devices.h"
#include "subtitles/subtitle_file.h"
#include "subtitles/translation_service.h"
#include "subtitles/translation_support.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace luma {
namespace {
QString preferredComputeDevice(QComboBox *choice, const QString &mode) {
    const auto selected=choice->currentData().toString();
    if(mode!="auto" || (!selected.isEmpty() && !choice->currentText().startsWith("저장된 GPU · ")))
        return selected;
    const QRegularExpression radeon(R"(\b(?:AMD|Radeon)\b)",QRegularExpression::CaseInsensitiveOption);
    for(int index=1;index<choice->count();++index){
        const auto device=choice->itemData(index).toString();
        if(device.startsWith("Vulkan") && radeon.match(choice->itemText(index)).hasMatch())
            return device;
    }
    return {};
}
}

void MainWindow::editTranslationGlossary(){
    const auto discIdentity=active_==discEngine_.get()?resumeIdentity_:QString{};
    const auto saved=configuredTranslationGlossary(discIdentity);
    bool accepted=false;
    const auto text=QInputDialog::getMultiLineText(this,discIdentity.isEmpty()?"기본 번역 용어집":"이 Blu-ray의 번역 용어집",
        "한 줄에 하나씩 입력하세요. 예시  青空 → 푸른 하늘",
        formatTranslationGlossary(saved),&accepted);
    if(!accepted)return;
    const auto glossary=parseTranslationGlossary(text);
    if(!glossary){
        QMessageBox::warning(this,"번역 용어집","원문 → 번역 형식으로 중복 없이 최대 64개, 각 항목은 100자 이내로 입력하세요.");
        return;
    }
    if(*glossary==saved)return;
    QSettings().setValue(translationGlossarySettingsKey(discIdentity),formatTranslationGlossary(*glossary));
    translation_->setGlossary(*glossary);
    translationPending_.clear();
    translationRefresh_.clear();
    clearTranslationSkips();
    translationFailed_=false;
    if(preparedCaptions_){
        translationStatus_->setText("용어집을 저장했습니다. 다음 전체 자막 생성부터 적용됩니다.");
    }else{
        translations_.clear();
        rebuildManagedCaptions();
        translationStatus_->setText("용어집을 저장했습니다. 현재 위치부터 다시 번역합니다.");
    }
}

void MainWindow::updateAiPreferences(){
    applyAiPreferences(true);
}

void MainWindow::applyAiPreferences(bool save){
    const auto mode=aiMode_->currentData().toString();
    AiPreferences preferences{mode,preferredComputeDevice(aiGpu_,mode)};
#ifdef Q_OS_WIN
    if(developerHardwareTest_){
        if(save)developerHardwareTest_->recognition.reset();
        else if(developerApproved_ && developerHardwareTest_->recognition)preferences=*developerHardwareTest_->recognition;
    }
#endif
    if(save){QSettings().setValue("ai/mode",mode);QSettings().setValue("ai/gpu",aiGpu_->currentData());}
    if(!save && job_.preferences().mode==preferences.mode && job_.preferences().gpuDevice==preferences.gpuDevice)return;
    if(job_.preferences().mode!=preferences.mode || job_.preferences().gpuDevice!=preferences.gpuDevice){cancelSubtitleWork();job_.setPreferences(preferences);}
    aiGpu_->setEnabled(preferences.mode=="auto" || preferences.mode=="gpu");
    aiFailed_=false;translationFailed_=false;nextAiAttemptMs_=0;aiRetryCount_=0;
    accelerator_->setText("음성 인식 · 선택한 기기에서 실행 대기");
    updateNpuStatus();
    if(liveCaptioningActive())job_.warmUp(model_);
    if(active_)scheduleSubtitles();
}

void MainWindow::updateNpuStatus(){
    const bool kotoba=QFileInfo(model_).completeBaseName()=="ggml-kotoba-whisper-v2.0-q5_0";
    npuStatus_->setVisible(kotoba);if(!kotoba)return;
    const auto result=npuCalibrationStatus(QCoreApplication::applicationDirPath()+"/runtime",model_);
    npuStatus_->setProperty("calibrationReason",result.reason);npuStatus_->setProperty("calibrationEligible",result.eligible);
    if(result.eligible)npuStatus_->setText(QString("NPU 설치 검사 통과 · 연속 %1회 · 최악 %2초").arg(result.completedRequests).arg(std::max(result.maxWarmInferenceMs,result.maxWarmWallMs)/1000.,0,'f',2));
    else if(result.reason=="too-slow")npuStatus_->setText("이 모델의 NPU 처리가 늦어 자동 모드에서 GPU를 우선합니다.");
    else if(result.state==NpuCalibrationState::Missing)npuStatus_->setText("NPU 설치 검사 전에는 자동 모드에서 GPU를 우선합니다.");
    else if(result.state==NpuCalibrationState::Stale)npuStatus_->setText("모델·드라이버·런타임이 바뀌어 NPU 재검사가 필요합니다. 자동 모드는 GPU를 사용합니다.");
    else npuStatus_->setText("안정적인 NPU 실행을 확인하지 못해 자동 모드에서 GPU를 우선합니다.");
    if(aiMode_->currentData()=="npu" && !result.eligible)npuStatus_->setText(npuStatus_->text()+"\n현재는 NPU를 수동 선택했습니다. 실패하면 GPU로 전환합니다.");
}

void MainWindow::updateTranslationPreferences(){
    applyTranslationPreferences(true);
}

void MainWindow::applyTranslationPreferences(bool save){
    const auto mode=translationMode_->currentData().toString();
    const bool saveVram=captionVramSaving_->isChecked();
    AiPreferences preferences=saveVram?AiPreferences{"cpu",{}}:AiPreferences{mode,preferredComputeDevice(translationGpu_,mode)};
#ifdef Q_OS_WIN
    if(developerHardwareTest_){
        if(save)developerHardwareTest_->translation.reset();
        else if(!saveVram && developerApproved_ && developerHardwareTest_->translation)preferences=*developerHardwareTest_->translation;
    }
#endif
    if(save){QSettings().setValue("translation/mode",mode);QSettings().setValue("translation/gpu",translationGpu_->currentData());}
    translationMode_->setEnabled(!saveVram);
    translationGpu_->setEnabled(!saveVram && (mode=="auto" || mode=="gpu"));
    if(translation_ && (translation_->preferences().mode!=preferences.mode || translation_->preferences().gpuDevice!=preferences.gpuDevice)){
        translation_->setPreferences(preferences);
        translationPending_.clear();translationRefresh_.clear();translations_.clear();clearTranslationSkips();
        translationFailed_=false;translationRetryCount_=0;translationRetryAt_=0;
        rebuildManagedCaptions();
        warmTranslationForCaptions();
    }
}

void MainWindow::warmTranslationForCaptions(){
    if(active_ && !lanRemotePlayback_ && !preparedCaptions_ && !translationFailed_
        && translationEnabled_->isChecked() && (liveCaptioningActive() || !externalCues_.isEmpty())){
        requestComputeDeviceInventory();
        translation_->warmUp();
    }
}

void MainWindow::showCaptionPreparation(){
    requestComputeDeviceInventory();
    if(!stopLanShareForSourceChange())return;
    stopLanReceiver();
    const auto initialFile=active_==fileEngine_.get()?source_:QString{};
    const auto preparationDiscRoot=active_==discEngine_.get()?source_:discPath_;
    CaptionPreparationRequest defaults;defaults.model=model_;defaults.source.language=language_->currentData().toString();defaults.recognitionPreferences=job_.preferences();defaults.translationPreferences=translation_->preferences();defaults.translate=translationEnabled_->isChecked();
    defaults.translationGlossary=configuredTranslationGlossary();
    if(active_==discEngine_.get()){
        const auto &state=active_->state();const auto playlist=matchDiscPlaylist(discInfo_,state.durationMs);
        if(!state.failed && state.timelineReady && !state.navigationTitle && state.currentTitle>=0 && !playlist.isEmpty()){
            int ordinal=0;for(const auto &track:state.audioTracks){if(track.id<0)continue;if(track.id==state.currentAudio && ordinal<64){defaults.source.source=source_;defaults.source.disc=true;defaults.source.playlist=playlist;defaults.source.audioIndex=ordinal;defaults.durationMs=state.durationMs;break;}++ordinal;}
        }
    }
    const auto root=QCoreApplication::applicationDirPath()+"/runtime/translation/models/";
    const auto quality=root+"Hy-MT2-7B-Q4_K_M.gguf";
    defaults.translationModel=QSettings().value("translation/model",QFileInfo::exists(quality)?quality:root+"Qwen3-4B-Q4_K_M.gguf").toString();
    savePlaybackProgress(true);preparedCaptions_.reset();preparedApplied_=false;resetSubtitleSession();job_.shutdown();translation_->setModel(defaults.translationModel);
    releaseCaptionBuffer(false);if(active_)active_->stop();active_=nullptr;pendingResume_.reset();requestedPlaying_=false;
    updateLanPlaybackAvailability();
    static_cast<VideoSurface*>(surface_)->setVideoActive(false);welcome_->show();applyCapturePolicy();status_->setText("영상 재생 없이 전체 자막을 준비합니다.");
    CaptionPreparationDialog dialog(defaults,preparationDiscRoot,initialFile,this);preparationDialog_=&dialog;
    const int result=dialog.exec();preparationDialog_.clear();
    if(result==QDialog::Accepted)if(auto prepared=dialog.prepared())playPreparedCaptions(std::move(*prepared));
}

bool MainWindow::loadPreparedCaptionProject(const QString &path){
    QString error;auto result=CaptionPreparationService::loadProject(path,&error);
    if(!result){status_->setText("자막 프로젝트를 열 수 없습니다. "+error);return false;}
    return playPreparedCaptions(std::move(*result));
}

bool MainWindow::playPreparedCaptions(CaptionPreparationResult result){
    QString error;if(!CaptionPreparationService::sourceUnchanged(result,&error)){status_->setText("준비한 자막의 미디어가 변경됐습니다. "+error);return false;}
    const auto source=result.request.source.source;
    if(!openSourceImpl(source,true,result.request.source.disc?result.request.durationMs:0))return false;
    subtitleFileLoader_->cancel();
    if(active_->state().failed)return false;
    if(pendingResume_ && std::abs(pendingResume_->durationMs-result.request.durationMs)>1000)pendingResume_.reset();
    preparedCaptions_=std::move(result);preparedApplied_=false;applyPreparedCaptions();
    aiStatus_->setText("전체 자막 프로젝트 준비됨 · 해당 본편에서 표시합니다.");
    return true;
}

void MainWindow::applyPreparedCaptions(){
    if(!preparedCaptions_ || preparedApplied_ || !active_)return;
    const auto &project=*preparedCaptions_;const auto &state=active_->state();
    if(!state.timelineReady || state.navigationTitle || state.failed)return;
    if(!preparedCaptionSourceTimelineMatches(project.request.source,project.request.durationMs,source_,state,active_==discEngine_.get()?&discInfo_:nullptr))return;
    QVector<int> audio;for(const auto &track:state.audioTracks)if(track.id>=0)audio.append(track.id);
    const int index=project.request.source.audioIndex;
    if(index>=0 && index<audio.size() && state.currentAudio!=audio[index]){active_->setAudio(audio[index]);return;}
    if(!preparedCaptionTimelineMatches(project.request.source,project.request.durationMs,source_,state,active_==discEngine_.get()?&discInfo_:nullptr))return;
    QString error;if(!CaptionPreparationService::sourceUnchanged(project,&error)){preparedCaptions_.reset();aiStatus_->setText("미디어가 변경돼 준비한 자막을 적용하지 않았습니다. "+error);return;}
    job_.shutdown();translation_->cancel();translationPending_.clear();translationRefresh_.clear();clearTranslationSkips();
    externalCues_=project.originals;readings_.clear();translations_=project.translations;translationFailed_=false;preparedApplied_=true;presentationDirty_=true;
    active_->setSubtitle(-1);rebuildManagedCaptions();releaseCaptionBuffer(true);
    bufferStatus_->setText("전체 자막 준비됨 · 탐색 후에도 바로 표시합니다.");
    aiStatus_->setText(QString("미리 만든 원문 %1개 · 한국어 %2개").arg(externalCues_.size()).arg(translations_.size()));
    translationStatus_->setText(project.request.translate?QString("전체 문맥으로 미리 준비한 한국어 %1개%2").arg(translations_.size()).arg(project.skippedTranslations?QString(" · %1구간 제외").arg(project.skippedTranslations):QString{}):"원문 자막을 미리 준비했습니다.");
}

void MainWindow::enableAutomaticSubtitles(const QString &language){const int index=language_->findData(language);if(index>=0){language_->setCurrentIndex(index);if(aiEnabled_->isChecked())automaticCaptionsChanged(true);else aiEnabled_->setChecked(true);}}

void MainWindow::automaticCaptionsChanged(bool enabled){
    if(subtitleFileLoader_)subtitleFileLoader_->cancel();presentationDirty_=true;QSettings().setValue("subtitles/aiEnabled",enabled);
    if(lanRemotePlayback_)return;
    if(enabled){
        preparedCaptions_.reset();preparedApplied_=false;externalCues_.clear();aiFailed_=false;bufferBypass_=false;
        captionPrefillPending_=true;aiRetryCount_=0;nextAiAttemptMs_=0;
        if(active_)active_->setSubtitle(-1);
        rebuildManagedCaptions();
        if(liveCaptioningActive()){
            requestComputeDeviceInventory();
            job_.warmUp(model_);
            warmTranslationForCaptions();
            scheduleSubtitles();
        }
    }
    else{cancelSubtitleWork();job_.shutdown();releaseCaptionBuffer(true);if(active_)active_->clearManagedSubtitles();aiStatus_->setText("AI 자동자막 꺼짐");bufferStatus_->setText("AI 자동자막 꺼짐 · 일반 재생");rebuildManagedCaptions();}
}

bool MainWindow::liveCaptioningActive()const{
    return active_ && !active_->state().failed && !lanRemotePlayback_ && aiEnabled_->isChecked()
        && !preparedCaptions_ && externalCues_.isEmpty();
}

void MainWindow::setCaptionsVisible(bool visible){
    captionsVisible_=visible;
    captions_->setCaptionsVisible(visible);
    if(active_)active_->setSubtitlesVisible(visible);
    if(auto *button=findChild<QPushButton*>("captionQuick")){
        button->setText(visible?"자막":"자막 끔");
        button->setToolTip(visible?"자막 설정 · C로 숨기기":"자막 숨김 · C로 표시");
    }
    publishLanCaptions();
}

bool MainWindow::captionTimelineAvailable()const{
    if(!active_)return false;const auto &state=active_->state();
    return state.timelineReady && state.durationMs>0 && state.currentAudio>=0 && (active_!=discEngine_.get() || (!state.navigationTitle && state.currentTitle>=0 && !state.audioTracks.isEmpty()));
}

void MainWindow::holdForCaptionBuffer(){
    if((lanPlaybackDialog_ && lanPlaybackDialog_->isSharing()) || !liveCaptioningActive() ||
        !active_ || bufferHolding_ || !captionPrefillPending_ || bufferBypass_ || !captionTimelineAvailable())return;
    captionPrefillPending_=false;
    const auto &state=active_->state();
    const auto position=pendingSeek_>=0?pendingSeek_:state.positionMs;
    if(state.ended || position>=state.durationMs || coverage_.readyForPlayback(position,state.durationMs))return;
    startCaptionHold(wantsPlayback());
}

void MainWindow::startCaptionHold(bool resume){
    captionPrefillPending_=false;
    bufferHolding_=true;
    bufferResume_=resume;
    captionHoldDeadline_.setRemainingTime(12'000);
    active_->setPaused(true);
}

void MainWindow::updateCaptionBuffer(){
    if(lanDiscSharing_ || !liveCaptioningActive() || !active_)return;
    const auto &state=active_->state();
    if(state.ended && pendingSeek_<0){releaseCaptionBuffer(false);return;}
    if(active_==discEngine_.get() && state.navigationTitle){releaseCaptionBuffer(true);bufferStatus_->setText("디스크 메뉴 · 본편에서 자막을 준비합니다.");return;}
    if(bufferHolding_ && captionHoldDeadline_.hasExpired()){
        releaseCaptionBuffer(true);
        bufferStatus_->setText("자막 준비가 늦어 재생을 계속합니다. 준비된 자막은 도착하는 대로 표시합니다.");
        return;
    }
    if(!state.timelineReady){
        bufferStatus_->setText("타이틀 정보를 확인한 뒤 자막을 준비합니다.");return;
    }
    if(state.currentAudio<0){
        releaseCaptionBuffer(true);
        bufferStatus_->setText("인식할 수 있는 오디오 트랙이 없습니다.");
        return;
    }
    if(state.durationMs<=0)return;
    const auto position=pendingSeek_>=0?pendingSeek_:state.positionMs;const auto ahead=coverage_.bufferedAhead(position,state.durationMs);const auto needed=std::min<qint64>(CaptionCoverage::lookAheadMs,std::max<qint64>(0,state.durationMs-position));
    if(position>=state.durationMs){releaseCaptionBuffer(true);return;}
    if(pendingAudio_!=-2 || pendingChapter_!=-2 || pendingTitle_!=-2){bufferStatus_->setText("선택한 트랙·챕터로 이동하는 중…");return;}
    if(full_ && bufferHolding_)clock_->setText(QString("자막 %1 / %2초").arg(ahead/1000.,0,'f',1).arg(needed/1000.,0,'f',1));
    if(!aiFailed_){
        const auto phase=job_.currentPhase();
        const QString pending=!lanAudioRequest_.isEmpty() || phase=="reading"?" · 음성 읽는 중":
            phase=="loading"?" · 인식 모델 준비 중":phase=="recognizing"?" · 음성 인식 중":QString{};
        bufferStatus_->setText(QString("자막 %1 / %2초 준비%3%4").arg(ahead/1000.,0,'f',1).arg(needed/1000.,0,'f',1)
            .arg(pending,bufferHolding_?(bufferResume_?" · 준비 후 자동 재생":" · 일시정지 유지"):""));
    }
    if(coverage_.readyForPlayback(position,state.durationMs) && coverage_.readyForPlayback(state.positionMs,state.durationMs))releaseCaptionBuffer(true);
    else holdForCaptionBuffer();
}

void MainWindow::releaseCaptionBuffer(bool continuePlayback){
    const bool resume=bufferHolding_ && bufferResume_ && continuePlayback;bufferHolding_=false;bufferResume_=false;captionHoldDeadline_={};
    if(resume && active_){requestedPlaying_=true;active_->setPaused(false);}
}

void MainWindow::retryCaptions(){
    updateNpuStatus();
    cancelSubtitleWork();aiRetryCount_=0;nextAiAttemptMs_=0;aiFailed_=false;bufferBypass_=false;translationFailed_=false;
    aiStatus_->setText(aiEnabled_->isChecked()?"자막 준비를 다시 시도합니다.":"AI 자동자막을 켜면 시작합니다.");scheduleSubtitles();scheduleTranslations();
}

void MainWindow::cancelSubtitleWork(bool keepDiscAudio){
    lanAudioRequest_.clear();
    job_.cancel(keepDiscAudio);coverage_.cancel();if(translation_)translation_->cancel();translationPending_.clear();translationRefresh_.clear();clearTranslationSkips();aiFailed_=false;translationFailed_=false;aiRetryCount_=0;nextAiAttemptMs_=0;
    translationRetryCount_=0;translationRetryAt_=0;
    if(captions_)captions_->setCues(displayCues());
}

void MainWindow::resetSubtitleSession(bool preserveExternal){
    if(!preserveExternal)preparedApplied_=false;
    if(!preserveExternal && subtitleFileLoader_)subtitleFileLoader_->cancel();
    cancelSubtitleWork();if(active_)active_->clearManagedSubtitles();generatedCues_.clear();
    if(!preparedApplied_)translations_.clear();
    if(!preserveExternal){externalCues_.clear();readings_.clear();translations_.clear();}
    presentationCues_.clear();presentationDirty_=true;coverage_.reset();pendingSeek_=-1;bufferBypass_=false;captionPrefillPending_=true;
    if(preserveExternal)rebuildManagedCaptions();else if(captions_)captions_->setCues({});
}

QVector<SubtitleCue> MainWindow::originalCues()const{return !externalCues_.isEmpty()?externalCues_:aiEnabled_->isChecked()?generatedCues_:QVector<SubtitleCue>{};}

QVector<SubtitleCue> MainWindow::displayCues(qint64 start,qint64 end)const{
    QVector<SubtitleCue> result;for(const auto &source:presentationCues_){
        if(source.endMs<start || source.startMs>end)continue;
        QStringList lines;if(originalEnabled_->isChecked())lines.append(source.text);
        if(readingEnabled_->isChecked() && !readings_.value(source.text).isEmpty())lines.append(readings_.value(source.text));
        if(translationEnabled_->isChecked()){const auto korean=translations_.value(translationCueKey(source));if(!korean.isEmpty())lines.append(korean);}
        if(!lines.isEmpty())result.append({source.startMs,source.endMs,lines.join('\n')});
    }return result;
}

void MainWindow::rebuildManagedCaptions(){
    if(!captions_)return;
    if(preparedApplied_ && preparedCaptions_)translations_=preparedCaptions_->translations;
    if(presentationDirty_){
        const auto originals=originalCues();
        presentationCues_=preparedApplied_ && preparedCaptions_?preparedCaptionPages(originals):captionPages(originals);
        presentationDirty_=false;
        // Retain a ready translation while newer context can still improve it.
        // A cue already on screen is frozen for the rest of its interval.
        translationRefresh_.clear();
        if(translation_ && !preparedCaptions_){
            translation_->setContextCues(originalCues());
            const auto position=captionPosition();
            constexpr qint64 refreshLeadMs=3000,lookAheadMs=90000;
            for(const auto &cue:presentationCues_){
                if(cue.startMs<position || cue.startMs-position<refreshLeadMs || cue.startMs-position>lookAheadMs)continue;
                const auto key=translationCueKey(cue);
                if(translations_.contains(key) && translation_->needsTranslation(cue))translationRefresh_.insert(key);
            }
        }
    }
    if(readingEnabled_->isChecked())for(const auto &cue:presentationCues_)if(!readings_.contains(cue.text))readings_.insert(cue.text,reading_.reading(cue.text));
    captions_->setCues(displayCues());
    if(active_)captions_->setPosition(active_->state().positionMs);
    publishLanCaptions();
    scheduleTranslations();
}

bool MainWindow::loadCaptionFile(const QString &path){
    preparedCaptions_.reset();preparedApplied_=false;
    if(!active_)return false;
    subtitleFileLoader_->cancel();
    if(!QFileInfo::exists(path)){subtitleImportFailed("자막 파일을 찾을 수 없습니다.");return false;}
    if(QFileInfo(path).suffix().compare("srt",Qt::CaseInsensitive)==0){auto result=readSrtFile(path);if(result.valid()){applyExternalCaptions(std::move(result.cues));return true;}}
    status_->setText("자막의 형식과 문자 인코딩을 확인하는 중…");subtitleFileLoader_->load(path);return true;
}

void MainWindow::applyExternalCaptions(QVector<SubtitleCue> cues){
    aiEnabled_->setChecked(false);if(translation_)translation_->cancel();translationPending_.clear();translationRefresh_.clear();clearTranslationSkips();readings_.clear();translations_.clear();translationFailed_=false;externalCues_=std::move(cues);presentationDirty_=true;active_->setSubtitle(-1);rebuildManagedCaptions();status_->setText(QString("자막 %1개를 불러왔습니다.").arg(externalCues_.size()));
}

void MainWindow::subtitleImportFailed(const QString &message){enableAutomaticSubtitles(language_->currentData().toString());status_->setText(message+" 자동 자막을 켰습니다.");appendLog("자막 불러오기: "+message);}

void MainWindow::scheduleSubtitles(){
    if(!liveCaptioningActive() || !active_ || videoRecoveryExhausted_)return;
    const bool discShare=lanDiscSharing_;
    if(discShare && (!lanPlaybackDialog_->isSharing() || lanCaptionPositionMs_<0))return;
    if(!lanAudioRequest_.isEmpty()){
        if(!lanAudioDeadline_.hasExpired())return;
        lanAudioRequest_.clear();
        coverage_.cancel();
    }
    updateCaptionBuffer();
    if(job_.busy() || pendingAudio_!=-2 || pendingChapter_!=-2 || pendingTitle_!=-2)return;
    if(!captionTimelineAvailable())return;const auto &state=active_->state();
    if(state.ended && pendingSeek_<0)return;
    if(aiFailed_){if(nextAiAttemptMs_<=0 || QDateTime::currentMSecsSinceEpoch()<nextAiAttemptMs_)return;aiFailed_=false;nextAiAttemptMs_=0;}
    const auto position=captionPosition();
    const auto limit=discShare && coverage_.bufferedAhead(position,state.durationMs)<CaptionCoverage::lookAheadMs?
        CaptionCoverage::lookAheadMs:CaptionCoverage::chunkMs;
    const auto chunk=coverage_.begin(position,state.durationMs,active_==discEngine_.get() && !discShare?2000:0,limit);
    if(!chunk)return;
    if(discShare){
        lanAudioStartMs_=*chunk;
        lanAudioDurationMs_=coverage_.pendingDuration();
        lanAudioRequest_=QUuid::createUuid().toString(QUuid::Id128);
        lanAudioDeadline_.setRemainingTime(2'000);
        if(!lanPlaybackDialog_->requestCaptionAudio(lanAudioRequest_,lanAudioStartMs_,lanAudioDurationMs_)){
            lanAudioRequest_.clear();
            coverage_.cancel();
        }
        return;
    }
    RecognitionRequest request;request.source=source_;request.disc=active_==discEngine_.get();request.title=std::max(1,state.currentTitle);request.language=language_->currentData().toString();request.startMs=*chunk;request.durationMs=coverage_.pendingDuration();request.audioTrackId=state.currentAudio;
    if(request.disc)request.playlist=matchDiscPlaylist(discInfo_,state.durationMs);
    int index=0;for(const auto &track:state.audioTracks){if(track.id<0)continue;if(track.id==state.currentAudio){request.audioIndex=index;break;}++index;}
    job_.start(request,model_);
}

void MainWindow::scheduleTranslations(){
    if(preparedCaptions_ || !translation_ || !translationEnabled_->isChecked() || videoRecoveryExhausted_)return;
    if(translationFailed_){
        if(translationRetryAt_<=0 || QDateTime::currentMSecsSinceEpoch()<translationRetryAt_)return;
        translationFailed_=false;
        translationRetryAt_=0;
    }
    const qint64 position=captionPosition();
    auto work=nearbyTranslationCues(presentationCues_,position,[this](const QString &key){
        return translations_.contains(key) || translationSkipped_.contains(key);
    });
    // Keep a single live window ordered by the playback position. Improving a
    // future result consumes spare work only after subtitles missing Korean.
    if(!translationRefresh_.isEmpty() && work.size()<16){
        const auto refresh=nearbyTranslationCues(presentationCues_,position,[this](const QString &key){
            return !translationRefresh_.contains(key) || translationSkipped_.contains(key);
        });
        for(const auto &cue:refresh){
            if(work.size()>=16)break;
            if(cue.startMs>position)work.append(cue);
        }
    }
    translationPending_.clear();
    for(const auto &cue:work)translationPending_.insert(translationCueKey(cue));
    translation_->requestCues(work,position);
}

void MainWindow::clearTranslationSkips(){translationSkipped_.clear();translationSkippedOrder_.clear();}

}
