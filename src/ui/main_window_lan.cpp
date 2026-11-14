#include "main_window.h"
#include "lan_playback_dialog.h"
#include "lan_receiver_dialog.h"
#include "player_controls.h"
#include "video_surface.h"
#include "media/platform_features.h"
#include "subtitles/translation_service.h"

#include <QFileInfo>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QHash>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSlider>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <iterator>
#include <utility>

namespace luma {

LanPlaybackDialog *MainWindow::lanPlaybackDialog() {
    if (!lanPlaybackDialog_) {
        lanPlaybackDialog_ = new LanPlaybackDialog(this);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::shareRequested, this, &MainWindow::startLanShare);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::remoteControlRequested, this, &MainWindow::applyLanRemoteControl);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::discMenuFrameRequested, this, [this] {
            QByteArray png;
            if (!captureGuard_->enabled() && active_ && active_ == discEngine_.get() && !lanDiscSharing_ && active_->state().discMenus &&
                active_->state().navigationTitle && active_->state().currentTitle >= 0 && !active_->state().failed) {
                const auto frame = static_cast<VideoSurface *>(surface_)->remoteDiscMenuFrame();
                QBuffer buffer(&png);
                if (!frame.isNull() && buffer.open(QIODevice::WriteOnly)) frame.save(&buffer, "PNG", 65);
            }
            lanPlaybackDialog_->sendDiscMenuFrame(png);
        });
        connect(lanPlaybackDialog_, &LanPlaybackDialog::streamDiagnostic, this, [this](const QString &reason,const QString &message) {
            appendLog(message);
            recordDeveloperEvent("lan-stream",reason.toLatin1().constData());
        });
        connect(lanPlaybackDialog_, &LanPlaybackDialog::receiveRequested, this, &MainWindow::showLanReceiver);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::shareStopped, this, &MainWindow::finishLanShare);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::captionClockChanged, this, &MainWindow::receiveLanCaptionClock);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::captionAudioReceived, this, &MainWindow::receiveLanCaptionAudio);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::sharingChanged, this, [this](bool sharing) {
            if(sharing)publishLanCaptions();
        });
        updateLanPlaybackAvailability();
    }
    return lanPlaybackDialog_;
}

void MainWindow::applyLanRemoteControl(const QString &action,const QString &fileId) {
    if (lanSourceChanging_ || lanRemotePlayback_)
        return;
    if (action=="file-open") {
        const auto path=lanPlaybackDialog_->remoteFilePath(fileId);
        if(path.isEmpty())return;
        if(lanPlaybackDialog_->isSharing()){
            if(lanPlaybackDialog_->releaseMediaForRemoteControl())pendingLanFile_=fileId;
        }else if(openSourceImpl(path,false)){
            shareSelectedLanFile_=true;
            updateLanPlaybackAvailability();
        }
        return;
    }
    if (action == "disc-open") {
        if (lanDiscSharing_) return;
        if (lanPlaybackDialog_->isSharing()) {
            if (lanPlaybackDialog_->releaseMediaForRemoteControl()) pendingLanDiscOpen_ = true;
        } else {
            playConnectedDisc();
        }
        return;
    }
    if (action == "resume") { if (!lanDiscSharing_) resumeLatest(); return; }
    if (action == "fullscreen") { toggleFullScreen(); return; }
    if (action == "picture-in-picture") { togglePictureInPicture(); return; }
    if (action == "mute") { toggleMute(); return; }
    if (action=="caption-ai-on" || action=="caption-ai-off" || action=="caption-retry") {
        if(!active_ || !lanPlaybackDialog_->isSharing() ||
            (action!="caption-ai-off" && !captionTimelineAvailable()))return;
        if(action=="caption-retry")retryCaptions();
        else aiEnabled_->setChecked(action=="caption-ai-on");
        publishLanCaptions();
        return;
    }
    const QHash<QString,QCheckBox *> captionChoices{
        {"caption-ai",aiEnabled_}, {"caption-original",originalEnabled_},
        {"caption-reading",readingEnabled_}, {"caption-translation",translationEnabled_}
    };
    if (auto *check = captionChoices.value(action)) {
        if (check->isEnabled()) check->toggle();
        publishLanCaptions();
        return;
    }
    if (action == "speed-next") {
        rate_->setCurrentIndex((rate_->currentIndex()+1)%rate_->count());
        if (active_) active_->setRate(rate_->currentData().toDouble());
        return;
    }
    if (action == "volume-up" || action == "volume-down") {
        if (volume_->isEnabled())
            volume_->setValue(std::clamp(volume_->value() + (action == "volume-up" ? 5 : -5), 0, 100));
        return;
    }
    if (action == "disc-menu" && lanDiscSharing_ && !lanReturnToDiscMenu_) {
        lanReturnToDiscMenu_ = lanPlaybackDialog_->releaseMediaForRemoteControl();
        return;
    }
    if (!active_ || lanDiscSharing_)
        return;
    if (action == "audio-next") {
        const auto &state = active_->state();
        const auto found = std::find_if(state.audioTracks.cbegin(),state.audioTracks.cend(),
            [&state](const MediaItem &track) { return track.id == state.currentAudio; });
        if (!state.audioTracks.isEmpty()) {
            auto next = found == state.audioTracks.cend() ? found : std::next(found);
            if (next == state.audioTracks.cend()) next = state.audioTracks.cbegin();
            selectAudio(next->id);
        }
        return;
    }
    if (action == "subtitle-next" && subs_->count() > 0) {
        subs_->setCurrentIndex((subs_->currentIndex()+1)%subs_->count());
        emit subs_->activated(subs_->currentIndex());
        return;
    }
    if (action == "play-pause") { togglePlayback(); return; }
    if (action == "seek-back" || action == "seek-forward") {
        seekTo(active_->state().positionMs + (action == "seek-forward" ? 10000 : -10000));
        return;
    }
    if (action == "previous-chapter" || action == "next-chapter") {
        selectChapter(std::max(0, active_->state().currentChapter + (action == "next-chapter" ? 1 : -1)));
        return;
    }
    if (active_ != discEngine_.get() || !active_->state().discMenus)
        return;
    if (action == "disc-menu") { selectTitle(0); return; }
    static const QHash<QString, DiscAction> navigation{
        {"disc-popup", DiscAction::Popup}, {"disc-up", DiscAction::Up}, {"disc-down", DiscAction::Down},
        {"disc-left", DiscAction::Left}, {"disc-right", DiscAction::Right}, {"disc-confirm", DiscAction::Activate}
    };
    if (const auto found = navigation.constFind(action); found != navigation.cend()) {
        if (found.value() == DiscAction::Activate && active_->state().navigationTitle)
            lanMenuSelectionDeadline_.setRemainingTime(15000);
        active_->navigate(discActionCode(found.value()));
    }
}

void MainWindow::startLanDiscovery() {
    lanPlaybackDialog()->startDiscovery();
}

void MainWindow::showLanPlayback() {
    auto *dialog = lanPlaybackDialog();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::showLanReceiver() {
    if (!lanReceiverDialog_) {
        lanReceiverDialog_ = new LanReceiverDialog(this);
        connect(lanReceiverDialog_, &LanReceiverDialog::playbackReady, this, &MainWindow::startLanReceive);
        connect(lanReceiverDialog_, &LanReceiverDialog::playbackEnded, this, &MainWindow::finishLanReceive);
    }
    lanReceiverDialog_->setOwnEndpoint(lanPlaybackDialog()->readyEndpoint());
    lanReceiverDialog_->show();
    lanReceiverDialog_->raise();
    lanReceiverDialog_->activateWindow();
}

void MainWindow::startLanReceive(const LanReceiverPlayback &playback) {
    if (!openSourceImpl({}, false, 0, &playback)) {
        const auto message = status_->text();
        QTimer::singleShot(0, lanReceiverDialog_, [this, message] {
            if (lanReceiverDialog_->hasPlayback())
                stopLanReceiver(message);
        });
        return;
    }
    lanReceiverDialog_->hide();
    if (lanPlaybackDialog_)
        lanPlaybackDialog_->hide();
}

void MainWindow::stopLanReceiver(const QString &message) {
    if (lanReceiverDialog_)
        lanReceiverDialog_->stopConnection(message);
    if (lanRemotePlayback_)
        finishLanReceive(message);
}

void MainWindow::finishLanReceive(const QString &message) {
    if (!lanRemotePlayback_)
        return;
    ++sourceGeneration_;
    resetSubtitleSession();
    if (active_)
        active_->stop();
    active_ = nullptr;
    lanRemotePlayback_ = false;
    lanRemoteSeekable_ = false;
    requestedPlaying_.reset();
    pendingSeek_ = -1;
    static_cast<VideoSurface *>(surface_)->setVideoActive(false);
    welcome_->show();
    seek_->setEnabled(true);
    static_cast<SeekSlider *>(seek_)->setBuffer(-1);
    updateChapterDisplay({});
    nowTitle_->setText("재생할 미디어를 선택하세요");
    status_->setText(message.isEmpty() ? "PC 연결을 종료했습니다." : message);
    applyCapturePolicy();
    updateLanPlaybackAvailability();
}

bool MainWindow::lanSourceAvailable() const {
    if (!active_ || lanRemotePlayback_ || active_->state().failed || recoveryPlayback_ || videoRecoveryExhausted_)
        return false;
    if (active_ != discEngine_.get())
        return lanLocalFileSupported(source_);
    const auto &state = active_->state();
    return state.timelineReady && !state.navigationTitle && state.currentTitle >= 0 &&
        pendingAudio_ == -2 && pendingChapter_ == -2 && pendingTitle_ == -2 && pendingSeek_ < 0 &&
        !pendingResume_ && (!preparedCaptions_ || preparedApplied_) &&
        !matchDiscPlaylist(discInfo_, state.durationMs).isEmpty();
}

void MainWindow::updateLanPlaybackAvailability() {
    if (!lanPlaybackDialog_)
        return;
    lanPlaybackDialog_->setMediaAvailable(lanSourceAvailable(), active_ && active_ == discEngine_.get());
    if(shareSelectedLanFile_ && active_ && (active_->state().failed || active_==discEngine_.get()))shareSelectedLanFile_=false;
    if(shareSelectedLanFile_ && active_ && active_->state().timelineReady && lanPlaybackDialog_->canShare()){
        shareSelectedLanFile_=false;
        startLanShare();
    }
    if (!lanMenuSelectionDeadline_.hasExpired() && active_ == discEngine_.get() && lanPlaybackDialog_->canShare() &&
        lanPlaybackDialog_->remoteControlEnabled()) {
        lanMenuSelectionDeadline_ = {};
        const auto &state = active_->state();
        lanSelectedPositionMs_ = std::clamp(state.positionMs, qint64{0}, std::max(qint64{0}, state.durationMs - 250));
        startLanShare();
    }
    advanceLanDiscShare();
}

void MainWindow::startLanShare() {
    if (lanSourceChanging_ || !lanPlaybackDialog_->canShare() || !lanSourceAvailable())
        return;
    if (active_ != discEngine_.get()) {
        releaseCaptionBuffer(true);
        sendLanMedia();
        return;
    }
    lanDiscResume_ = wantsPlayback();
    lanDiscSharing_ = true;
    lanDiscAwaitingPause_ = true;
    lanDiscPauseDeadline_.setRemainingTime(2500);
    lanPlaybackDialog_->prepareDiscShare();
    savePlaybackProgress(true);
    job_.cancel();
    coverage_.cancel();
    releaseCaptionBuffer(false);
    requestedPlaying_ = false;
    active_->setPaused(true);
    advanceLanDiscShare();
}

void MainWindow::advanceLanDiscShare() {
    if (!lanDiscAwaitingPause_ || !lanPlaybackDialog_->isPreparingDiscShare())
        return;
    if (!lanSourceAvailable() || lanDiscPauseDeadline_.hasExpired()) {
        lanPlaybackDialog_->cancelDiscShare("PC의 Blu-ray 재생을 일시정지할 수 없습니다.");
        return;
    }
    const auto &state = active_->state();
    if (state.playing || state.buffering)
        return;
    lanDiscAwaitingPause_ = false;
    if (!sendLanMedia())
        lanPlaybackDialog_->cancelDiscShare("Blu-ray 공유를 시작할 수 없습니다. 송출 도구와 디스크 상태를 확인하세요.");
}

bool MainWindow::sendLanMedia() {
    const auto &state = active_->state();
    LanPlaybackMedia media;
    media.disc = active_ == discEngine_.get();
    media.title = media.disc ? (discInfo_.label.isEmpty() ? QString("Blu-ray") : discInfo_.label) : QFileInfo(source_).fileName();
    media.source = media.disc ? discInfo_.root : QFileInfo(source_).canonicalFilePath();
    media.durationMs = std::max<qint64>(0, state.durationMs);
    media.startMs = std::exchange(lanSelectedPositionMs_, 0);
    media.chaptersMs = state.chapterOffsetsMs;
    if(media.chaptersMs.size()==state.chapters.size()){
        for(const auto &chapter:state.chapters){
            const auto fallback=QString("Chapter %1").arg(chapter.id+1);
            media.chapterTitles.append(chapter.label==fallback?QString{}:chapter.label);
        }
    }
    media.videoRange = state.sourceVideoRange;
    media.liveCaptions = true;
    if (media.disc) {
        media.playlist = matchDiscPlaylist(discInfo_, state.durationMs);
        int ordinal = 0;
        for (const auto &track : state.audioTracks) {
            if (track.id < 0)
                continue;
            if (track.id == state.currentAudio) {
                media.audioIndex = ordinal;
                if (track.id >= 0x1100 && track.id <= 0x1fff)
                    media.audioTrackId = track.id;
                break;
            }
            ++ordinal;
        }
    }
    media.subtitles = lanCaptionTrack(displayCues());
    return lanPlaybackDialog_->shareMedia(media);
}

void MainWindow::finishLanShare(bool cleanupConfirmed) {
    lanSelectedPositionMs_ = 0;
    lanCaptionPositionMs_=-1;
    lanCaptionClock_.invalidate();
    lanAudioRequest_.clear();
    if(lanDiscSharing_){job_.cancel();coverage_.cancel();}
    const auto file=std::exchange(pendingLanFile_,QString{});
    const bool openDisc=std::exchange(pendingLanDiscOpen_,false);
    if(cleanupConfirmed && openDisc){
        QTimer::singleShot(0,this,[this]{applyLanRemoteControl("disc-open",{});});
        return;
    }
    if(cleanupConfirmed && !file.isEmpty()){
        lanDiscSharing_=lanDiscAwaitingPause_=lanDiscResume_=false;
        QTimer::singleShot(0,this,[this,file]{applyLanRemoteControl("file-open",file);});
        return;
    }
    if (!lanDiscSharing_)
        return;
    const bool resume = lanDiscResume_ && cleanupConfirmed;
    lanDiscSharing_ = false;
    lanDiscAwaitingPause_ = false;
    lanDiscResume_ = false;
    const bool returnToMenu = std::exchange(lanReturnToDiscMenu_,false);
    if (cleanupConfirmed && returnToMenu && active_ == discEngine_.get() && active_ && !active_->state().failed) {
        active_->setPaused(false);
        selectTitle(0);
        return;
    }
    if (resume && active_ && active_ == discEngine_.get() && !active_->state().failed) {
        requestedPlaying_ = true;
        captionPrefillPending_=true;
        if (liveCaptioningActive())
            holdForCaptionBuffer();
        if (!bufferHolding_)
            active_->setPaused(false);
    }
}

bool MainWindow::stopLanShareForSourceChange() {
    lanMenuSelectionDeadline_ = {};
    lanSelectedPositionMs_ = 0;
    if (!lanPlaybackDialog_)
        return true;
    lanPlaybackDialog_->disarmRemoteStart();
    QScopedValueRollback changing(lanSourceChanging_, true);
    const bool resume = lanDiscResume_;
    lanDiscResume_ = false;
    if (lanPlaybackDialog_->stopSharingAndWait())
        return true;
    if (lanDiscSharing_)
        lanDiscResume_ = resume;
    status_->setText("모바일 공유 종료를 확인할 수 없습니다.");
    return false;
}

qint64 MainWindow::captionPosition()const{
    if(lanCaptionPositionMs_>=0 && lanPlaybackDialog_ && lanPlaybackDialog_->isSharing())
        return lanCaptionPositionMs_;
    return pendingSeek_>=0?pendingSeek_:active_?active_->state().positionMs:0;
}

void MainWindow::receiveLanCaptionClock(qint64 positionMs){
    if(!active_ || positionMs<0 || positionMs>active_->state().durationMs)return;
    const auto previous=lanCaptionPositionMs_;
    const auto elapsed=lanCaptionClock_.isValid()?lanCaptionClock_.restart():0;
    if(!lanCaptionClock_.isValid())lanCaptionClock_.start();
    lanCaptionPositionMs_=positionMs;
    if(previous>=0 && (positionMs<previous-1'200 || positionMs>previous+std::max<qint64>(2'500,elapsed*2+1'000))){
        lanAudioRequest_.clear();
        job_.cancel();
        coverage_.cancel();
        aiFailed_=false;
        nextAiAttemptMs_=0;
        aiRetryCount_=0;
    }
    scheduleSubtitles();
    scheduleTranslations();
    publishLanCaptions();
}

void MainWindow::receiveLanCaptionAudio(const QString &id,const QString &state,const QByteArray &pcm){
    if(lanAudioRequest_.isEmpty() || id!=lanAudioRequest_)return;
    const auto start=lanAudioStartMs_,duration=lanAudioDurationMs_;
    lanAudioRequest_.clear();
    if(!lanDiscSharing_ || !liveCaptioningActive()){coverage_.cancel();return;}
    if(state=="ready" && pcm.size()==duration*32){
        job_.startPcm(start,pcm,language_->currentData().toString(),model_);
        return;
    }
    coverage_.cancel();
    if(state=="ended"){
        aiFailed_=true;
        aiStatus_->setText("현재 송출에서 자막용 음성을 읽을 수 없습니다.");
    }
}

void MainWindow::publishLanCaptions(){
    if(!lanPlaybackDialog_ || !lanPlaybackDialog_->isSharing() || !active_)return;
    const auto position=captionPosition();
    const auto ahead=coverage_.bufferedAhead(position,active_->state().durationMs);
    const bool canAI=captionTimelineAvailable();
    const bool enabled=liveCaptioningActive();
    const QString state=preparedApplied_?"prepared":!externalCues_.isEmpty()?"file":
        !enabled?"off":aiFailed_ && nextAiAttemptMs_<=0?"failed":!canAI?"waiting":
        job_.busy()?"processing":ahead>0?"ready":"waiting";
    const auto nearby=displayCues(position-30'000,position+90'000);
    const auto activeBackend=job_.selectedBackend();
    const QString backend=activeBackend.startsWith("NPU",Qt::CaseInsensitive)?"NPU":
        activeBackend.startsWith("GPU",Qt::CaseInsensitive)?"GPU":
        activeBackend.startsWith("CPU",Qt::CaseInsensitive)?"CPU":QString{};
    lanPlaybackDialog_->publishCaptions({{"state",state},{"enabled",enabled},{"canAI",canAI},
        {"original",originalEnabled_->isChecked()},{"reading",readingEnabled_->isChecked()},
        {"translation",translationEnabled_->isChecked()},
        {"aheadMs",std::min<qint64>(ahead,120'000)},{"backend",backend},
        {"translationState",!translationEnabled_->isChecked()?"off":translationFailed_?"failed":
            !translations_.isEmpty()?"ready":translation_->busy()?"processing":"waiting"},
        {"vtt",lanCaptionTrack(nearby)}});
}

}
