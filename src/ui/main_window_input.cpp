#include "main_window.h"

#include "video_surface.h"
#include "native_playback_move.h"
#include "fullscreen_controls.h"
#include "caption_overlay.h"
#include "performance_overlay.h"
#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTabBar>
#include <QTextEdit>
#include <QTimer>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbt.h>
#endif

namespace luma {

bool MainWindow::eventFilter(QObject *object,QEvent *event){
    if(event->type()==QEvent::Wheel && panel_){
        if(auto *widget=qobject_cast<QWidget*>(object);widget && panel_->isAncestorOf(widget) &&
           (qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractSpinBox*>(widget) || qobject_cast<QSlider*>(widget))){
            event->ignore();
            return true;
        }
    }
    if(event->type()!=QEvent::KeyPress || QApplication::activePopupWidget() || QApplication::activeModalWidget())return QMainWindow::eventFilter(object,event);
    auto *target=qobject_cast<QWidget*>(object);if(!target || qobject_cast<QDialog*>(target->window()))return QMainWindow::eventFilter(object,event);
    bool belongs=false;for(auto *owner=target;owner;owner=owner->parentWidget())if(owner==this){belongs=true;break;}if(!belongs)return false;
    auto *key=static_cast<QKeyEvent*>(event);const auto modifiers=key->modifiers() & ~Qt::KeypadModifier;const auto code=key->key();
    if(developerApproved_ && modifiers==(Qt::ControlModifier|Qt::ShiftModifier) && code==Qt::Key_D){
        if(!key->isAutoRepeat())performanceEnabled_->toggle();
        return true;
    }
    if((modifiers==(Qt::ControlModifier|Qt::ShiftModifier) && code==Qt::Key_P) ||
       (modifiers==Qt::NoModifier && code==Qt::Key_Escape && pictureInPicture_)){
        if(!key->isAutoRepeat())togglePictureInPicture();
        return true;
    }
    if(modifiers==Qt::NoModifier && (code==Qt::Key_F11 || (code==Qt::Key_Escape && full_))){
        if(!key->isAutoRepeat())toggleFullScreen();
        if(fullscreenControls_)fullscreenControls_->activity();
        return true;
    }
    if(auto *focused=QApplication::focusWidget();focused && focused->window()==target->window())target=focused;
    for(auto *owner=target;owner && owner!=this;owner=owner->parentWidget())
        if(qobject_cast<QLineEdit*>(owner) || qobject_cast<QTextEdit*>(owner) || qobject_cast<QPlainTextEdit*>(owner) ||
           qobject_cast<QComboBox*>(owner) || qobject_cast<QAbstractSpinBox*>(owner) || qobject_cast<QAbstractScrollArea*>(owner) ||
           qobject_cast<QTabBar*>(owner) || (owner!=seek_ && qobject_cast<QSlider*>(owner)))return false;
    const auto relativeSeek=[this](qint64 delta){
        if(active_ && active_->state().timelineReady)seekTo((pendingSeek_>=0?pendingSeek_:active_->state().positionMs)+delta);
    };
    const bool chapterKey=code==Qt::Key_Left || code==Qt::Key_Right;
    const bool disc=active_ && active_==discEngine_.get() && !lanDiscSharing_;
    const bool repeat=key->isAutoRepeat();
    const bool slower=code==Qt::Key_Less || (modifiers==Qt::ShiftModifier && code==Qt::Key_Comma);
    const bool faster=code==Qt::Key_Greater || (modifiers==Qt::ShiftModifier && code==Qt::Key_Period);
    const bool help=code==Qt::Key_Question || (modifiers==Qt::ShiftModifier && code==Qt::Key_Slash);
    if(modifiers==Qt::AltModifier && disc && chapterKey){
        active_->navigate(discActionCode(code==Qt::Key_Left?DiscAction::Left:DiscAction::Right));
        return true;
    }
    if(modifiers==Qt::ControlModifier){
        if(code==Qt::Key_O){if(!repeat)findChild<QPushButton*>("openMediaButton")->click();}
        else if(code==Qt::Key_D){if(!repeat)discButton_->click();}
        else if(chapterKey && (!disc || !active_->state().navigationTitle))selectAdjacentChapter(code==Qt::Key_Left?-1:1);
        else if(disc){
            std::optional<DiscAction> action;
            if(code==Qt::Key_Up)action=DiscAction::Up;else if(code==Qt::Key_Down)action=DiscAction::Down;else if(code==Qt::Key_Left)action=DiscAction::Left;else if(code==Qt::Key_Right)action=DiscAction::Right;else if(code==Qt::Key_Return || code==Qt::Key_Enter)action=DiscAction::Activate;else return false;
            if(!repeat || *action!=DiscAction::Activate)active_->navigate(discActionCode(*action));
        }else return false;
    }else if((modifiers==Qt::NoModifier || modifiers==Qt::ShiftModifier) && (slower || faster)){
        const int rate=std::clamp(rate_->currentIndex()+(faster?1:-1),0,rate_->count()-1);
        rate_->setCurrentIndex(rate);
        if(active_)active_->setRate(rate_->currentData().toDouble());
    }else if((modifiers==Qt::NoModifier || modifiers==Qt::ShiftModifier) && help){
        if(!repeat)showPlaybackShortcuts();
    }else if(modifiers==Qt::ShiftModifier && chapterKey){
        relativeSeek(code==Qt::Key_Left?-10'000:10'000);
    }else if(modifiers==Qt::ShiftModifier && (code==Qt::Key_N || code==Qt::Key_P)){
        if(!repeat)selectAdjacentMedia(code==Qt::Key_N?1:-1);
    }else if(modifiers==Qt::NoModifier){
        switch(code){
        case Qt::Key_F:case Qt::Key_Return:case Qt::Key_Enter:
            if(!repeat)toggleFullScreen();
            break;
        case Qt::Key_Left:case Qt::Key_Right:
            relativeSeek(code==Qt::Key_Left?-5'000:5'000);
            break;
        case Qt::Key_J:case Qt::Key_L:
            relativeSeek(code==Qt::Key_J?-10'000:10'000);
            break;
        case Qt::Key_Up:case Qt::Key_Down:
            if(volume_->isEnabled())volume_->setValue(volume_->value()+(code==Qt::Key_Up?5:-5));
            break;
        case Qt::Key_M:if(!repeat)toggleMute();break;
        case Qt::Key_C:if(!repeat)setCaptionsVisible(!captionsVisible_);break;
        case Qt::Key_I:if(!repeat)togglePictureInPicture();break;
        case Qt::Key_Slash:if(!repeat)findChild<QPushButton*>("openMediaButton")->click();break;
        case Qt::Key_Space:
            if(qobject_cast<QAbstractButton*>(target))return false;
            [[fallthrough]];
        case Qt::Key_K:case Qt::Key_MediaTogglePlayPause:
            if(!repeat)togglePlayback();
            break;
        case Qt::Key_MediaPlay:case Qt::Key_MediaPause:
            if(!repeat && wantsPlayback()!=(code==Qt::Key_MediaPlay))togglePlayback();
            break;
        case Qt::Key_MediaStop:
            if(wantsPlayback())togglePlayback();
            if(active_ && active_->state().timelineReady)seekTo(0);
            break;
        case Qt::Key_MediaNext:case Qt::Key_MediaPrevious:
            if(!repeat)selectAdjacentMedia(code==Qt::Key_MediaNext?1:-1);
            break;
        case Qt::Key_Period:case Qt::Key_Comma:
            if(!lanDiscSharing_ && active_ && !wantsPlayback() && !bufferHolding_ && pendingSeek_<0 &&
               !active_->stepFrame(code==Qt::Key_Comma?-1:1) && disc && code==Qt::Key_Comma)
                status_->setText("Blu-ray에서는 일시정지한 상태에서 다음 프레임으로 이동할 수 있습니다.");
            break;
        default:
            if(code!=Qt::Key_Home && code!=Qt::Key_End && (code<Qt::Key_0 || code>Qt::Key_9))return false;
            if(active_ && active_->state().timelineReady){
                const auto duration=active_->state().durationMs;
                seekTo(code==Qt::Key_Home?0:code==Qt::Key_End?std::max<qint64>(0,duration-500):duration*(code-Qt::Key_0)/10);
            }
        }
    }else return false;
    if(fullscreenControls_)fullscreenControls_->activity();return true;
}

void MainWindow::showPlaybackShortcuts(){
    QMessageBox::information(this,"재생 단축키",
        "K / Space   재생·일시정지\n"
        "J / L   10초 뒤로·앞으로\n"
        "← / →   5초 뒤로·앞으로\n"
        "↑ / ↓   음량 5% 조절\n"
        "M   음소거\n"
        "F / Enter / F11   전체 화면    Esc   나가기\n"
        "C   자막 표시 전환    I   작은 화면\n"
        "0–9   재생 위치 0–90%    Home / End   처음·끝\n"
        "< / >   배속 낮추기·높이기\n"
        ", / .   일시정지 중 이전·다음 프레임 (Blu-ray는 다음만)\n"
        "Ctrl+← / Ctrl+→   이전·다음 챕터\n"
        "Shift+P / Shift+N   이전·다음 파일 또는 Blu-ray 챕터\n"
        "/   파일 열기    Shift+?   이 목록\n\n"
        "Blu-ray 메인 메뉴는 Ctrl+방향키·Ctrl+Enter로 조작합니다.\n"
        "재생 중 팝업 메뉴에서 좌우는 Alt+← / Alt+→를 사용합니다.");
}

bool MainWindow::nativeEvent(const QByteArray &eventType,void *message,qintptr *result){
#ifdef Q_OS_WIN
    if(eventType=="windows_generic_MSG"){
        const auto *event=static_cast<const MSG *>(message);
        if(event->message==WM_DEVICECHANGE && (event->wParam==DBT_DEVICEARRIVAL || event->wParam==DBT_DEVICEREMOVECOMPLETE) && event->lParam){
            const auto *header=reinterpret_cast<const DEV_BROADCAST_HDR *>(event->lParam);
            if(header->dbch_size>=sizeof(DEV_BROADCAST_VOLUME) && header->dbch_devicetype==DBT_DEVTYP_VOLUME &&
               (reinterpret_cast<const DEV_BROADCAST_VOLUME *>(header)->dbcv_flags & DBTF_MEDIA))discMediaChanged();
        }
    }
    if(eventType=="windows_generic_MSG" && static_cast<const MSG *>(message)->message==WM_DISPLAYCHANGE)
        QTimer::singleShot(0,this,&MainWindow::updateDisplayStatus);
    if(eventType=="windows_generic_MSG" && static_cast<const MSG *>(message)->message==WM_MOVING &&
       surface_ && movePlaybackWindows(this,eventType,message,result,
            {static_cast<VideoSurface*>(surface_)->videoWindow(),captions_,transport_,performance_}))
        return true;
#endif
    return QMainWindow::nativeEvent(eventType,message,result);
}

void MainWindow::closeEvent(QCloseEvent *event){
    clearDiscPlayRequest();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event){if(event->mimeData()->hasUrls())event->acceptProposedAction();}

void MainWindow::openDroppedUrls(const QList<QUrl> &urls){
    if(urls.isEmpty())return;
    if(urls.size()>1){
        QStringList files;
        for(const auto &url:urls)if(url.isLocalFile())files.append(url.toLocalFile());
        if(!files.isEmpty()){openMediaSelection(files);return;}
    }
    const auto &url=urls.first();
    const auto path=url.toString(QUrl::FullyEncoded);
    if(url.isLocalFile()){
        const auto local=normalizedPlaybackSource(path,false);
        if(local && QStringList{"srt","ass","ssa","vtt","smi","sami"}.contains(QFileInfo(*local).suffix().toLower())){
            loadCaptionFile(*local);
            return;
        }
    }
    openSource(path);
}

void MainWindow::openMediaSelection(const QStringList &paths){
    QStringList queue;
    for(const auto &path:paths){
        const auto file=normalizedPlaybackSource(path,false);
        if(file && QFileInfo(*file).isFile() && !queue.contains(*file))queue.append(*file);
    }
    if(queue.isEmpty())return;
    playbackQueue_=queue;
    openSource(queue.first());
}

void MainWindow::dropEvent(QDropEvent *event){if(event->mimeData()->hasUrls()){openDroppedUrls(event->mimeData()->urls());event->acceptProposedAction();}}

}
