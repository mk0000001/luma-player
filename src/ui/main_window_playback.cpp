#include "main_window.h"

#include "caption_overlay.h"
#include "video_surface.h"
#include "player_controls.h"
#include "prepared_caption_binding.h"
#include "media/platform_features.h"
#include "subtitles/subtitle_file.h"
#include "subtitles/translation_service.h"
#include "subtitles/translation_support.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStorageInfo>
#include <QTimer>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace luma {

namespace {
void syncCombo(QComboBox *box,const QVector<MediaItem> &items){
    QStringList expected,current;for(const auto &item:items)expected.append(item.label);for(int i=0;i<box->count();++i)current.append(box->itemText(i));
    if(current==expected)return;
    QSignalBlocker guard(box);const auto selected=box->currentData();box->clear();for(const auto &item:items)box->addItem(item.label,item.id);
    const int index=box->findData(selected);if(index>=0)box->setCurrentIndex(index);
}
}

void MainWindow::bindEngine(PlaybackEngine *engine){
    connect(engine,&PlaybackEngine::updated,this,[this,engine]{if(engine==active_)refresh();});connect(engine,&PlaybackEngine::logMessage,this,&MainWindow::appendLog);
    connect(engine,&PlaybackEngine::errorOccurred,this,[this,engine](const QString &message){appendLog(message);if(engine==active_){requestedPlaying_.reset();releaseCaptionBuffer(false);status_->setText(message);status_->setProperty("error",true);}});
    connect(engine,&PlaybackEngine::videoOutputLost,this,[this,engine](const QString &message){
        const QPointer<PlaybackEngine> target(engine);const auto generation=sourceGeneration_;
        QTimer::singleShot(0,this,[this,target,generation,message]{if(target && target==active_ && generation==sourceGeneration_)recoverVideoOutput(target,message);});
    });
    if(!engine->available())appendLog(engine->lastError());
}

void MainWindow::recoverVideoOutput(PlaybackEngine *engine,const QString &message){
    if(engine!=active_ || source_.isEmpty())return;
    appendLog(message);
    if(videoRecoveryCount_>=1){
        videoRecoveryExhausted_=true;cancelSubtitleWork();pendingResume_.reset();directResume_=false;recoveryPlayback_.reset();recoveryExternalCues_.clear();pendingSeek_=-1;
        requestedPlaying_=false;releaseCaptionBuffer(false);engine->setPaused(true);status_->setText("영상 출력 복구 후 장치 오류가 다시 발생했습니다. 재생을 멈췄습니다. 미디어를 다시 열어 주세요.");status_->setProperty("error",true);return;
    }
    const auto state=engine->state();const bool disc=engine==discEngine_.get(),playing=wantsPlayback();
    const auto source=source_;const auto prepared=preparedCaptions_;const auto targetDuration=discTargetDurationMs_;
    const auto external=prepared?QVector<SubtitleCue>{}:externalCues_;
    std::optional<ResumePoint> point;
    if(state.timelineReady && state.durationMs>0 && (!disc || (!state.navigationTitle && state.currentTitle>=0)))
        point=ResumePoint{source,disc,disc?state.currentTitle:-1,pendingSeek_>=0?pendingSeek_:state.positionMs,state.durationMs,state.discMenus,preparedApplied_ && preparedCaptions_?preparedCaptions_->projectFile:QString{}};
    savePlaybackProgress(true);cancelSubtitleWork();releaseCaptionBuffer(false);engine->stop();active_=nullptr;
    static_cast<VideoSurface*>(surface_)->setVideoActive(false);
    auto &replacement=disc?discEngine_:fileEngine_;
    replacement=std::make_unique<PlaybackEngine>(disc?PlaybackEngine::Kind::Disc:PlaybackEngine::Kind::File,QString{},this);bindEngine(replacement.get());
    if(disc)replacement->useSoftwareDiscDecoderOnNextOpen();
    if(prepared)playPreparedCaptions(*prepared);else openSourceImpl(source,false,targetDuration);
    videoRecoveryCount_=1;requestedPlaying_=playing;recoveryAudio_=state.currentAudio;recoverySubtitle_=state.currentSubtitle;
    recoveryExternalCues_=external;
    if(!active_ || active_->state().failed){
        pendingResume_.reset();recoveryPlayback_.reset();recoveryExternalCues_.clear();
        if(!active_){requestedPlaying_=false;videoRecoveryExhausted_=true;preparedCaptions_.reset();preparedApplied_=false;resetSubtitleSession();status_->setText("원본 미디어가 변경되어 영상 출력을 복구하지 못했습니다. 미디어를 다시 열어 주세요.");status_->setProperty("error",true);}
        return;
    }
    pendingResume_=point;directResume_=disc && point.has_value();recoveryPlayback_=point?std::optional<bool>(playing):std::nullopt;
    if(!point && !recoveryExternalCues_.isEmpty()){const auto cues=std::move(recoveryExternalCues_);recoveryExternalCues_.clear();applyExternalCaptions(cues);}
    if(bufferHolding_)bufferResume_=playing;
    active_->setPaused(disc && point?false:point.has_value() || !playing);
    status_->setText(disc?"Blu-ray 영상 출력을 다시 준비했습니다. 같은 위치로 복구하는 중…":"그래픽 장치 연결이 끊겨 영상 출력을 다시 만들었습니다. 같은 위치로 복구하는 중…");
}

void MainWindow::applyCapturePolicy(){
    if(!captureGuard_)return;
    const bool protectedDisc=active_==discEngine_.get() && (discInfo_.aacs || discInfo_.bdplus || !discInfo_.valid);
    captureGuard_->setEnabled(protectedDisc && !(developerApproved_ && captureApproved_));
    captureStatus_->setText(protectedDisc?captureGuard_->status():"보호 디스크를 재생하면 Windows 캡처 제외를 적용합니다.");
    captureStatus_->setProperty("error",captureGuard_->enabled() && !captureGuard_->applied());
}

void MainWindow::updateDisplayStatus(){
    const auto display=displayCapabilities(winId());
    if(!display.value("verified").toBool()){displayStatus_->setText("현재 디스플레이의 HDR 상태를 확인하지 못했습니다.");return;}
    const QString mode=display.value("hdrActive").toBool()?"HDR 출력 활성":"SDR 출력";
    displayStatus_->setText(QString("%1 · %2비트\n%3").arg(mode).arg(display.value("bitsPerColor").toInt()).arg(display.value("display").toString()));
    displayStatus_->setToolTip("현재 Windows 출력 상태입니다. Dolby Vision 인증 출력은 확인되지 않았습니다.");
}

void MainWindow::changeVideoAdapter(){
    const auto selected=videoGpu_->currentData().toString();if(QSettings().value("video/adapter").toString()==selected)return;
    QSettings().setValue("video/adapter",selected);
    const bool reopen=active_==fileEngine_.get();const qint64 position=reopen?active_->state().positionMs:0;
    const bool playing=reopen?wantsPlayback():false;
    if(reopen){cancelSubtitleWork();preparedApplied_=false;releaseCaptionBuffer(false);active_->stop();active_=nullptr;}
    fileEngine_.reset();fileEngine_=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::File,QString{},this);bindEngine(fileEngine_.get());updateMotionSettings();
    if(reopen){
        active_=fileEngine_.get();if(active_->open(source_,static_cast<VideoSurface*>(surface_)->videoWindowId())){active_->setVolume(volume_->value());active_->setRate(rate_->currentData().toDouble());active_->setPaused(true);active_->seek(position);pendingSeek_=position;seekRequestedAt_=QDateTime::currentMSecsSinceEpoch();if(liveCaptioningActive()){bufferHolding_=true;bufferResume_=playing;}else active_->setPaused(!playing);}
        else status_->setText(active_->lastError());
    }
}

void MainWindow::updateMotionSettings(){
    const auto mode=motionMode_->currentData().toString();const int fps=motionFps_->currentData().toInt();
    QSettings().setValue("motion/mode",mode);QSettings().setValue("motion/fps",fps);motionFps_->setEnabled(mode=="optical");
    motionStatus_->setText(mode=="off"?"프레임 보간 꺼짐":active_==discEngine_.get()?"Blu-ray 메뉴 재생은 프레임 보간을 지원하지 않습니다.":mode=="optical"?QString("CPU 모션 보간 · 목표 %1 fps · 실제 처리 속도는 재생 후 확인").arg(fps):"GPU 블렌딩 · 현재 디스플레이 주기에 맞춤");
    if(fileEngine_)fileEngine_->setFrameInterpolation(mode,fps);
}

void MainWindow::discoverDisc(){
    discPath_.clear();
#ifdef Q_OS_WIN
    for(wchar_t drive=L'A';drive<=L'Z';++drive){const QString root=QString(QChar(drive))+":/";if(GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()))==DRIVE_CDROM && QFileInfo::exists(root+"BDMV/index.bdmv")){discPath_=root;break;}}
#else
    for(const auto &volume:QStorageInfo::mountedVolumes())if(QFileInfo::exists(volume.rootPath()+"/BDMV/index.bdmv")){discPath_=volume.rootPath();break;}
#endif
    if(discPath_.isEmpty()){discLabel_->setText("연결된 디스크 없음");discDetails_->clear();return;}
    driveInfo_=inspectDisc(discPath_);QString title=driveInfo_.label;title.replace('_',' ');discLabel_->setText(title.isEmpty()?"Blu-ray":title);
    discDetails_->setText(QString("%1\n%2개 플레이리스트\n%3").arg(discPath_).arg(driveInfo_.titles.size()).arg(driveInfo_.aacs || driveInfo_.bdplus?"보호 디스크 감지":"일반 디스크"));
}

void MainWindow::openSource(const QString &source){openSourceImpl(source,false);}

void MainWindow::openSourceImpl(const QString &source,bool preparedOpen,qint64 preparedDiscDurationMs){
    savePlaybackProgress(true);
    preparedCaptions_.reset();preparedApplied_=false;
    ++sourceGeneration_;videoRecoveryCount_=0;videoRecoveryExhausted_=false;recoveryPlayback_.reset();recoveryAudio_=-1;recoverySubtitle_=-1;recoveryExternalCues_.clear();
    if(subtitleFileLoader_)subtitleFileLoader_->cancel();pendingResume_.reset();directResume_=false;
    resetSubtitleSession();releaseCaptionBuffer(false);if(active_)active_->stop();lastTitle_=-1;lastNavigation_=false;lastAudio_=-2;decodedSamples_=0;pendingSeek_=-1;pendingAudio_=-2;pendingChapter_=-2;pendingTitle_=-2;requestedPlaying_=true;
    const bool disc=isDiscSource(source);source_=disc?localDiscPath(source):source;if(disc)discInfo_=inspectDisc(source_);
    discTargetDurationMs_=disc?preparedDiscDurationMs:0;
    resumeIdentity_=resumeIdentity(source_,disc?&discInfo_:nullptr);pendingResume_=resumeHistory_.find(resumeIdentity_,-1,preparedDiscDurationMs==0);resumeSavedAt_=0;
    translation_->setGlossary(configuredTranslationGlossary(disc?resumeIdentity_:QString{}));
    active_=disc?discEngine_.get():fileEngine_.get();nowTitle_->setText(disc?(discInfo_.label.isEmpty()?"Blu-ray":discInfo_.label):QFileInfo(source).fileName());
    surface_->setFocus(Qt::OtherFocusReason);welcome_->hide();status_->setProperty("error",false);status_->setText("미디어를 여는 중…");
    const auto videoWindow=static_cast<VideoSurface*>(surface_)->videoWindowId();
    const bool opened=disc && preparedDiscDurationMs>0?active_->openPreparedDisc(source_,videoWindow,preparedDiscDurationMs):active_->open(source_,videoWindow);
    if(opened){active_->setVolume(volume_->value());active_->setRate(rate_->currentData().toDouble());if(!disc && (preparedOpen || liveCaptioningActive())){bufferHolding_=true;bufferResume_=true;active_->setPaused(true);}}
    else{status_->setText(active_->lastError());welcome_->show();}
    applyCapturePolicy();static_cast<VideoSurface*>(surface_)->setVideoActive(opened);updateDisplayStatus();updateMotionSettings();
    if(opened && !disc && !preparedOpen){
        const QFileInfo media(source_);const auto base=media.dir().filePath(media.completeBaseName());
        for(const auto &extension:QStringList{"srt","ass","ssa","vtt","smi","sami"})if(QFileInfo::exists(base+'.'+extension)){loadCaptionFile(base+'.'+extension);break;}
    }
}

void MainWindow::updateResumeButton(){
    if(!resumeButton_)return;const auto point=resumeHistory_.latest();resumeButton_->setEnabled(point.has_value());
    resumeButton_->setToolTip(point?QString("%1 · %2부터").arg(point->disc?"Blu-ray":QFileInfo(point->source).fileName(),formatTime(point->positionMs)):"재생 기록이 생기면 이어서 볼 수 있습니다.");
}

void MainWindow::savePlaybackProgress(bool force){
    if(!active_ || resumeIdentity_.isEmpty() || pendingResume_ || pendingSeek_>=0 || pendingTitle_!=-2 || pendingChapter_!=-2 || pendingAudio_!=-2)return;
    const auto now=QDateTime::currentMSecsSinceEpoch();if(!force && now-resumeSavedAt_<5000)return;
    const auto &state=active_->state();const bool disc=active_==discEngine_.get();if(state.failed || !state.timelineReady || (disc && (state.navigationTitle || state.currentTitle<0)))return;
    resumeHistory_.save(resumeIdentity_,{source_,disc,disc?state.currentTitle:-1,state.positionMs,state.durationMs,state.discMenus,preparedApplied_ && preparedCaptions_?preparedCaptions_->projectFile:QString{}});resumeSavedAt_=now;updateResumeButton();
}

void MainWindow::restorePlaybackProgress(){
    if(!active_ || !pendingResume_ || videoRecoveryExhausted_)return;const auto state=active_->state();if(state.failed)return;
    const bool disc=active_==discEngine_.get();
    if(disc && state.discMenus!=pendingResume_->discMenus){pendingResume_.reset();return;}
    if(disc && !state.discMenus && !state.timelineReady)return;
    if(disc && directResume_){
        if(state.titles.isEmpty())return;
        // Disc-authored First Play may forbid title selection. Let it reach
        // the interactive menu rather than consuming the one restore attempt.
        if(state.navigationTitle && state.currentTitle!=0)return;
        const int title=pendingResume_->title;directResume_=false;
        if(std::none_of(state.titles.begin(),state.titles.end(),[title](const MediaItem &item){return item.id==title;})){pendingResume_.reset();status_->setText("저장된 디스크 타이틀을 찾을 수 없습니다.");return;}
        if(state.currentTitle!=title || state.navigationTitle){selectTitle(title);return;}
    }
    if(disc && (state.navigationTitle || state.currentTitle<0 || pendingTitle_!=-2))return;
    if(!state.timelineReady || state.durationMs<=0)return;
    if(disc && state.currentTitle!=pendingResume_->title){pendingResume_=resumeHistory_.find(resumeIdentity_,state.currentTitle,state.discMenus);if(!pendingResume_)return;}
    const auto point=*pendingResume_;pendingResume_.reset();
    if(std::abs(point.durationMs-state.durationMs)>2000 || (!recoveryPlayback_ && point.positionMs>=state.durationMs-5000)){
        resumeHistory_.remove(resumeIdentity_,point.title,point.discMenus);updateResumeButton();
        if(recoveryPlayback_){recoveryPlayback_.reset();recoveryExternalCues_.clear();requestedPlaying_=false;active_->setPaused(true);videoRecoveryExhausted_=true;status_->setText("미디어 길이가 달라 재생 위치를 복원하지 못했습니다. 미디어를 다시 열어 주세요.");status_->setProperty("error",true);}
        return;
    }
    if(recoveryPlayback_)requestedPlaying_=*recoveryPlayback_;
    seekTo(point.positionMs);appendLog(QString("이어서 보기 · %1").arg(formatTime(point.positionMs)));
    if(recoveryPlayback_){
        const bool playing=*recoveryPlayback_;
        if(std::any_of(state.audioTracks.begin(),state.audioTracks.end(),[this](const MediaItem &item){return item.id==recoveryAudio_;}))active_->setAudio(recoveryAudio_);
        if(std::any_of(state.subtitleTracks.begin(),state.subtitleTracks.end(),[this](const MediaItem &item){return item.id==recoverySubtitle_;}))active_->setSubtitle(recoverySubtitle_);
        requestedPlaying_=playing;if(bufferHolding_)bufferResume_=playing;else active_->setPaused(!playing);recoveryPlayback_.reset();
        if(!recoveryExternalCues_.isEmpty()){const auto cues=std::move(recoveryExternalCues_);recoveryExternalCues_.clear();applyExternalCaptions(cues);}
    }
}

void MainWindow::resumeLatest(){
    const auto point=resumeHistory_.latest();if(!point)return;QString source=point->source;const auto identity=resumeHistory_.latestIdentity();
    if(point->disc){
        if(!discPath_.isEmpty())source=discPath_;const auto disc=inspectDisc(source);
        if(resumeIdentity(source,&disc)!=identity){status_->setText("이어볼 Blu-ray 디스크를 연결해 주세요.");return;}
    }else if(resumeIdentity(source)!=identity){status_->setText("미디어 파일이 변경되었거나 이동해 저장 위치를 복원할 수 없습니다.");return;}
    if(!point->preparedProject.isEmpty()){
        QString error;auto project=CaptionPreparationService::loadProject(point->preparedProject,&error);
        if(project && preparedCaptionResumeMatches(*project,*point) && playPreparedCaptions(std::move(*project))){pendingResume_=point;directResume_=point->disc;return;}
    }
    openSourceImpl(source,false,point->disc && !point->discMenus?point->durationMs:0);pendingResume_=point;directResume_=point->disc;
}

void MainWindow::refresh(){
    if(!active_)return;const auto state=active_->state();
    if(videoRecoveryExhausted_)active_->setPaused(true);
    if(recoveryPlayback_ && pendingResume_){
        const bool targetReady=state.timelineReady && (active_!=discEngine_.get() || (!state.navigationTitle && state.currentTitle==pendingResume_->title));
        active_->setPaused(active_!=discEngine_.get() || targetReady);if(bufferHolding_)bufferResume_=*recoveryPlayback_;
    }
    if(requestedPlaying_ && !recoveryPlayback_ && !bufferHolding_ && state.playing==*requestedPlaying_)requestedPlaying_.reset();
    if(pendingAudio_!=-2 && state.currentAudio==pendingAudio_)pendingAudio_=-2;
    if(pendingChapter_!=-2 && state.currentChapter==pendingChapter_)pendingChapter_=-2;
    if(pendingTitle_!=-2 && state.currentTitle==pendingTitle_)pendingTitle_=-2;
    if((pendingAudio_!=-2 || pendingChapter_!=-2 || pendingTitle_!=-2) && QDateTime::currentMSecsSinceEpoch()-pendingControlAt_>8000){pendingAudio_=-2;pendingChapter_=-2;pendingTitle_=-2;aiStatus_->setText("선택한 트랙·챕터로 이동하지 못했습니다. 현재 위치에서 자막을 준비합니다.");}
    if(state.currentAudio>=0){
        if(lastAudio_!=-2 && lastAudio_!=state.currentAudio){
            const bool sameTimeline=active_!=discEngine_.get() || (state.currentTitle==lastTitle_ && state.navigationTitle==lastNavigation_);
            const bool detach=preparedApplied_ && sameTimeline;
            if(detach){preparedCaptions_.reset();preparedApplied_=false;}
            resetSubtitleSession(!detach);
            if(detach)aiStatus_->setText("오디오가 바뀌었습니다. 해당 오디오로 자막을 다시 준비할 수 있습니다.");
        }
        lastAudio_=state.currentAudio;
    }
    if(active_==discEngine_.get() && (state.currentTitle!=lastTitle_ || state.navigationTitle!=lastNavigation_)){resetSubtitleSession();lastTitle_=state.currentTitle;lastNavigation_=state.navigationTitle;if(state.navigationTitle)releaseCaptionBuffer(true);}
    if(pendingSeek_>=0 && (std::abs(state.positionMs-pendingSeek_)<=750 || QDateTime::currentMSecsSinceEpoch()-seekRequestedAt_>5000))pendingSeek_=-1;
    restorePlaybackProgress();applyPreparedCaptions();savePlaybackProgress();
    captions_->setPosition(state.positionMs);if(state.playing)++decodedSamples_;
    if(captureGuard_ && captureGuard_->enabled())captureStatus_->setText(captureGuard_->status());
    const bool playing=wantsPlayback();static_cast<IconButton*>(play_)->setPlayerIcon(playing?PlayerIcon::Pause:PlayerIcon::Play);
    pipButton_->setEnabled(pictureInPicture_ || (!state.failed && state.timelineReady));
    play_->setAccessibleName(playing?"일시정지 · Space":"재생 · Space");clock_->setText(formatTime(state.positionMs)+" / "+formatTime(state.durationMs));
    if(!seek_->isSliderDown()){QSignalBlocker guard(seek_);seek_->setValue(state.durationMs>0?int(state.positionMs*10000/state.durationMs):0);}
    codec_->setText(QStringList{active_->name(),state.videoCodec,state.hardwareDecoder}.filter(QRegularExpression(".+")).join(" · "));
    if(active_==fileEngine_.get() && !state.motionStatus.isEmpty()){
        QString text=state.motionStatus;
        if(state.sourceFps>0)text+=QString("\n원본 %1 fps · 필터 %2 fps · 화면 %3 Hz").arg(state.sourceFps,0,'f',2).arg(state.filteredFps,0,'f',2).arg(state.displayFps,0,'f',2);
        motionStatus_->setText(text);
    }
    updateCaptionBuffer();
    if(!state.failed && !videoRecoveryExhausted_){
        if(pendingResume_ && active_==discEngine_.get() && state.navigationTitle)status_->setText(state.currentTitle==0?"디스크 메뉴에서 재생을 선택하면 저장 위치로 이동합니다.":"디스크 시작 안내를 진행한 뒤 재생 위치를 복원합니다.");
        else if(bufferHolding_)status_->setText(bufferStatus_->text());
        else if(state.buffering)status_->setText("디스크 읽기 버퍼를 채우는 중…");
        else if(!aiFailed_)status_->setText(state.playing?"재생 중":"일시정지 / 준비 중");
    }
    syncCombo(titles_,state.titles);syncCombo(audio_,state.audioTracks);syncCombo(subs_,state.subtitleTracks);
    const auto select=[](QComboBox *box,int id){const int index=box->findData(id);if(index>=0 && index!=box->currentIndex()){QSignalBlocker guard(box);box->setCurrentIndex(index);}};
    select(titles_,state.currentTitle);select(audio_,state.currentAudio);select(subs_,state.currentSubtitle);
    if(chapters_->count()!=state.chapters.size()){chapters_->clear();for(const auto &chapter:state.chapters){auto *item=new QListWidgetItem(chapter.label.isEmpty()?QString("Chapter %1").arg(chapter.id+1):chapter.label);item->setData(Qt::UserRole,chapter.id);chapters_->addItem(item);}}
    if(active_==discEngine_.get())menuStatus_->setText(!state.discMenus?QString("준비한 타이틀 · 챕터 %1\n디스크 메뉴는 상단 ‘Blu-ray 재생’에서 열 수 있습니다.").arg(std::max(0,state.currentChapter)+1):state.currentTitle<=0?"디스크 메뉴 · 마우스 또는 Ctrl+방향키·Ctrl+Enter로 조작하세요.":state.navigationTitle?"디스크 시작 안내 · 메뉴를 준비하는 중…":QString("타이틀 %1 · 챕터 %2\n팝업 메뉴로 디스크 메뉴를 열 수 있습니다.").arg(state.currentTitle).arg(std::max(0,state.currentChapter)+1));
}

bool MainWindow::wantsPlayback()const{if(videoRecoveryExhausted_ || (active_ && active_->state().failed))return false;if(recoveryPlayback_)return *recoveryPlayback_;return requestedPlaying_.value_or(bufferHolding_?bufferResume_:active_ && active_->state().playing);}

void MainWindow::togglePlayback(){
    if(!active_ || videoRecoveryExhausted_)return;
    requestedPlaying_=!wantsPlayback();
    if(recoveryPlayback_)recoveryPlayback_=*requestedPlaying_;
    if(bufferHolding_){bufferResume_=*requestedPlaying_;active_->setPaused(true);updateCaptionBuffer();}
    else active_->setPaused(!*requestedPlaying_);
}

void MainWindow::selectAudio(int id){if(!active_ || (active_->state().currentAudio==id && pendingAudio_==-2))return;const bool prepared=preparedCaptions_.has_value();preparedCaptions_.reset();preparedApplied_=false;resetSubtitleSession(!prepared);pendingAudio_=id;pendingControlAt_=QDateTime::currentMSecsSinceEpoch();active_->setAudio(id);if(id<0)releaseCaptionBuffer(true);else holdForCaptionBuffer();}

void MainWindow::selectChapter(int id){if(!active_)return;const auto resume=wantsPlayback();cancelSubtitleWork();pendingSeek_=-1;pendingChapter_=id;pendingControlAt_=QDateTime::currentMSecsSinceEpoch();bufferBypass_=false;if(liveCaptioningActive() && captionTimelineAvailable()){bufferHolding_=true;bufferResume_=resume;active_->setPaused(true);}active_->setChapter(id);}

void MainWindow::selectTitle(int id){if(!active_)return;savePlaybackProgress(true);resetSubtitleSession();pendingTitle_=id;pendingControlAt_=QDateTime::currentMSecsSinceEpoch();if(id<=0)releaseCaptionBuffer(true);active_->setTitle(id);}

void MainWindow::seekTo(qint64 position){
    if(!active_)return;const auto state=active_->state();const bool resume=wantsPlayback();
    cancelSubtitleWork();pendingChapter_=-2;position=std::clamp<qint64>(position,0,std::max<qint64>(0,state.durationMs));pendingSeek_=position;seekRequestedAt_=QDateTime::currentMSecsSinceEpoch();bufferBypass_=false;
    if(liveCaptioningActive() && captionTimelineAvailable()){bufferHolding_=true;bufferResume_=resume;active_->setPaused(true);}active_->seek(position);scheduleSubtitles();
}

void MainWindow::toggleMute(){if(volume_->value()>0){lastUnmutedVolume_=volume_->value();volume_->setValue(0);}else volume_->setValue(std::clamp(lastUnmutedVolume_,1,100));}

}
