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
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QTabBar>
#include <QTextEdit>
#include <QTimer>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace luma {

bool MainWindow::eventFilter(QObject *object,QEvent *event){
    if(event->type()!=QEvent::KeyPress || QApplication::activePopupWidget() || QApplication::activeModalWidget())return QMainWindow::eventFilter(object,event);
    auto *target=qobject_cast<QWidget*>(object);if(!target)return QMainWindow::eventFilter(object,event);
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
    if(modifiers==Qt::NoModifier && (code==Qt::Key_F11 || (code==Qt::Key_Escape && full_))){toggleFullScreen();if(fullscreenControls_)fullscreenControls_->activity();return true;}
    if(auto *focused=QApplication::focusWidget();focused && focused->window()==target->window())target=focused;
    for(auto *owner=target;owner && owner!=this;owner=owner->parentWidget())if(qobject_cast<QLineEdit*>(owner) || qobject_cast<QTextEdit*>(owner) || qobject_cast<QPlainTextEdit*>(owner) || qobject_cast<QComboBox*>(owner) || qobject_cast<QAbstractSpinBox*>(owner) || qobject_cast<QAbstractScrollArea*>(owner) || qobject_cast<QTabBar*>(owner) || qobject_cast<QSlider*>(owner))return false;
    if(modifiers==Qt::ControlModifier){
        if(code==Qt::Key_O)findChild<QPushButton*>("openMediaButton")->click();
        else if(code==Qt::Key_D)discButton_->click();
        else if(active_==discEngine_.get()){
            std::optional<DiscAction> action;
            if(code==Qt::Key_Up)action=DiscAction::Up;else if(code==Qt::Key_Down)action=DiscAction::Down;else if(code==Qt::Key_Left)action=DiscAction::Left;else if(code==Qt::Key_Right)action=DiscAction::Right;else if(code==Qt::Key_Return || code==Qt::Key_Enter)action=DiscAction::Activate;else return false;
            active_->navigate(discActionCode(*action));
        }else return false;
    }else if(modifiers==Qt::ShiftModifier && (code==Qt::Key_Left || code==Qt::Key_Right)){if(active_)seekTo(active_->state().positionMs+(code==Qt::Key_Left?-10000:10000));}
    else if(modifiers!=Qt::NoModifier)return false;
    else if(code==Qt::Key_F || code==Qt::Key_Return || code==Qt::Key_Enter)toggleFullScreen();
    else if(code==Qt::Key_Left || code==Qt::Key_Right){if(active_)seekTo(active_->state().positionMs+(code==Qt::Key_Left?-10000:10000));}
    else if(code==Qt::Key_Up || code==Qt::Key_Down){if(volume_->isEnabled())volume_->setValue(std::clamp(volume_->value()+(code==Qt::Key_Up?5:-5),0,100));}
    else if(code==Qt::Key_M)toggleMute();else if(code==Qt::Key_Space){if(qobject_cast<QAbstractButton*>(target))return false;togglePlayback();}else return false;
    if(fullscreenControls_)fullscreenControls_->activity();return true;
}

bool MainWindow::nativeEvent(const QByteArray &eventType,void *message,qintptr *result){
#ifdef Q_OS_WIN
    if(eventType=="windows_generic_MSG" && static_cast<const MSG *>(message)->message==WM_DISPLAYCHANGE)
        QTimer::singleShot(0,this,&MainWindow::updateDisplayStatus);
    if(eventType=="windows_generic_MSG" && static_cast<const MSG *>(message)->message==WM_MOVING &&
       surface_ && movePlaybackWindows(this,eventType,message,result,
            {static_cast<VideoSurface*>(surface_)->videoWindow(),captions_,transport_,performance_}))
        return true;
#endif
    return QMainWindow::nativeEvent(eventType,message,result);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event){if(event->mimeData()->hasUrls())event->acceptProposedAction();}

void MainWindow::openDroppedUrls(const QList<QUrl> &urls){
    if(urls.isEmpty())return;
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

void MainWindow::dropEvent(QDropEvent *event){if(event->mimeData()->hasUrls()){openDroppedUrls(event->mimeData()->urls());event->acceptProposedAction();}}

}
