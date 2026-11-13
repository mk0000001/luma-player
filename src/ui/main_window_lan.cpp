#include "main_window.h"
#include "lan_playback_dialog.h"
#include "lan_receiver_dialog.h"
#include "player_controls.h"
#include "video_surface.h"

#include <QFileInfo>
#include <QCheckBox>
#include <QComboBox>
#include <QHash>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSlider>
#include <QTimer>
#include <algorithm>
#include <iterator>
#include <utility>

namespace luma {

LanPlaybackDialog *MainWindow::lanPlaybackDialog() {
    if (!lanPlaybackDialog_) {
        lanPlaybackDialog_ = new LanPlaybackDialog(this);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::shareRequested, this, &MainWindow::startLanShare);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::remoteControlRequested, this, &MainWindow::applyLanRemoteControl);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::streamDiagnostic, this, [this](const QString &reason,const QString &message) {
            appendLog(message);
            recordDeveloperEvent("lan-stream",reason.toLatin1().constData());
        });
        connect(lanPlaybackDialog_, &LanPlaybackDialog::receiveRequested, this, &MainWindow::showLanReceiver);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::shareStopped, this, &MainWindow::finishLanShare);
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
    if (action == "disc-open") { if (!lanDiscSharing_) playConnectedDisc(); return; }
    if (action == "resume") { if (!lanDiscSharing_) resumeLatest(); return; }
    if (action == "fullscreen") { toggleFullScreen(); return; }
    if (action == "picture-in-picture") { togglePictureInPicture(); return; }
    if (action == "mute") { toggleMute(); return; }
    const QHash<QString,QCheckBox *> captionChoices{
        {"caption-ai",aiEnabled_}, {"caption-original",originalEnabled_},
        {"caption-reading",readingEnabled_}, {"caption-translation",translationEnabled_}
    };
    if (auto *check = captionChoices.value(action)) {
        if (check->isEnabled()) check->toggle();
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
    if (const auto found = navigation.constFind(action); found != navigation.cend())
        active_->navigate(discActionCode(found.value()));
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
    advanceLanDiscShare();
}

void MainWindow::startLanShare() {
    if (lanSourceChanging_ || !lanPlaybackDialog_->canShare() || !lanSourceAvailable())
        return;
    if (active_ != discEngine_.get()) {
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
    media.chaptersMs = state.chapterOffsetsMs;
    if(media.chaptersMs.size()==state.chapters.size()){
        for(const auto &chapter:state.chapters){
            const auto fallback=QString("Chapter %1").arg(chapter.id+1);
            media.chapterTitles.append(chapter.label==fallback?QString{}:chapter.label);
        }
    }
    media.videoRange = state.sourceVideoRange;
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
    const auto file=std::exchange(pendingLanFile_,QString{});
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
        if (liveCaptioningActive())
            holdForCaptionBuffer();
        if (!bufferHolding_)
            active_->setPaused(false);
    }
}

bool MainWindow::stopLanShareForSourceChange() {
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

}
