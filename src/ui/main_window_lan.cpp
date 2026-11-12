#include "main_window.h"
#include "lan_playback_dialog.h"
#include "lan_receiver_dialog.h"
#include "video_surface.h"

#include <QFileInfo>
#include <QLabel>
#include <QScopedValueRollback>
#include <QSlider>
#include <QTimer>
#include <algorithm>

namespace luma {

LanPlaybackDialog *MainWindow::lanPlaybackDialog() {
    if (!lanPlaybackDialog_) {
        lanPlaybackDialog_ = new LanPlaybackDialog(this);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::shareRequested, this, &MainWindow::startLanShare);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::receiveRequested, this, &MainWindow::showLanReceiver);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::deviceAuthorized, this, &MainWindow::startLanShare);
        connect(lanPlaybackDialog_, &LanPlaybackDialog::shareStopped, this, &MainWindow::finishLanShare);
        updateLanPlaybackAvailability();
    }
    return lanPlaybackDialog_;
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
    if (!externalCues_.isEmpty())
        media.subtitles = lanCaptionTrack(displayCues());
    return lanPlaybackDialog_->shareMedia(media);
}

void MainWindow::finishLanShare(bool cleanupConfirmed) {
    if (!lanDiscSharing_)
        return;
    const bool resume = lanDiscResume_ && cleanupConfirmed;
    lanDiscSharing_ = false;
    lanDiscAwaitingPause_ = false;
    lanDiscResume_ = false;
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
