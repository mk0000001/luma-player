#include "fullscreen_controls.h"
#include "native_playback_move.h"
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QLayout>
#include <QWidget>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace luma {
FullscreenControls::FullscreenControls(QWidget *window,QWidget *transport,QWidget *surface):QObject(window),window_(window),transport_(transport),surface_(surface),captureInput_(QGuiApplication::platformName()==QStringLiteral("windows")){
    if(auto *parent=transport->parentWidget())if(auto *layout=parent->layout())layout->removeWidget(transport);
    transport->setParent(window,Qt::Tool|Qt::FramelessWindowHint);
    transport->setAttribute(Qt::WA_NativeWindow);transport->setAttribute(Qt::WA_ShowWithoutActivating);
    if(auto *layout=transport->layout())layout->setSizeConstraint(QLayout::SetNoConstraint);
    transport->setMinimumWidth(0);
    nativeInput_.observe({0,false,false,false});
    qApp->installEventFilter(this);timer_.setInterval(100);connect(&timer_,&QTimer::timeout,this,&FullscreenControls::poll);timer_.start();queueSync();
}
int FullscreenControls::reservedHeight()const{
    return transport_ && surface_ && window_ && transport_->isVisible() && surface_->isVisible() && window_->isVisible() && !window_->isMinimized()?transport_->height():0;
}
void FullscreenControls::notifyVisibility(){
    const int height=reservedHeight();const bool visible=height>0;
    if(visible!=lastVisible_){lastVisible_=visible;emit controlsVisibilityChanged(visible);}
    if(height!=lastReserved_){lastReserved_=height;emit reservedHeightChanged(height);}
}
void FullscreenControls::queueSync(){
    if(syncPending_)return;syncPending_=true;QTimer::singleShot(0,this,[this]{syncPending_=false;syncGeometry();});
}
void FullscreenControls::syncGeometry(){
    if(!window_ || !transport_ || !surface_)return;
    const bool visible=wanted_ && window_->isVisible() && !window_->isMinimized() && surface_->isVisible() && !QApplication::activeModalWidget();
    if(visible){
        const QRect target(surface_->mapToGlobal(QPoint(0,surface_->height()-transport_->height())),QSize(surface_->width(),transport_->height()));
        const bool moved=transport_->geometry()!=target,shown=!transport_->isVisible();
        if(moved)transport_->setGeometry(target);
        if(shown)transport_->show();if(shown || raisePending_)transport_->raise();
    }else transport_->hide();
    raisePending_=false;
    notifyVisibility();
}
void FullscreenControls::setActive(bool active){
    active_=active;pointer_=QCursor::pos();reveal();idle_.restart();
}
void FullscreenControls::reveal(){
    wanted_=true;raisePending_=true;if(window_)window_->unsetCursor();if(surface_)surface_->unsetCursor();syncGeometry();
}
void FullscreenControls::activity(){reveal();if(active_)idle_.restart();}
bool FullscreenControls::interactionInProgress()const{
    if(QApplication::activePopupWidget() || QApplication::activeModalWidget() || QApplication::mouseButtons()!=Qt::NoButton)return true;
    if(!window_)return false;
    for(auto *slider:window_->findChildren<QAbstractSlider*>())if(slider->isSliderDown())return true;
    for(auto *button:window_->findChildren<QAbstractButton*>())if(button->isDown())return true;
    return false;
}
bool FullscreenControls::nativeInputActivity(){
#ifdef Q_OS_WIN
    // VLC's HWND captures buttons on its input thread, including completed clicks
    // between polls. Process-scoped capture notifications exclude no-op synthetic
    // input which changes GetLastInputInfo without a real click. No raw-input
    // receiver or global mouse hook; native hit-testing avoids DPI mixing.
    if(QGuiApplication::platformName()!=QStringLiteral("windows"))return false;
    NativeInputSnapshot snapshot{captureInput_.serial()};
    snapshot.blocked=QApplication::activePopupWidget() || QApplication::activeModalWidget();
    if(window_ && window_->isVisible() && !window_->isMinimized() && !snapshot.blocked){
        const auto host=reinterpret_cast<HWND>(window_->winId());
        const auto root=GetAncestor(host,GA_ROOTOWNER);
        const auto inputWindow=reinterpret_cast<HWND>(captureInput_.window());
        snapshot.foregroundOwned=root && GetAncestor(GetForegroundWindow(),GA_ROOTOWNER)==root && GetAncestor(inputWindow,GA_ROOTOWNER)==root;
        POINT pointer;
        if(root && GetCursorPos(&pointer))snapshot.pointerOwned=GetAncestor(WindowFromPoint(pointer),GA_ROOTOWNER)==root;
    }
    return nativeInput_.observe(snapshot);
#else
    return false;
#endif
}
void FullscreenControls::poll(){
    if(!window_ || !transport_)return;
    if(nativeInputActivity())activity();
    if(!active_)return;
    const auto current=QCursor::pos();
    if(current!=pointer_){pointer_=current;if(window_->frameGeometry().contains(current))activity();}
    if(interactionInProgress()){activity();return;}
    if(idle_.isValid() && idle_.elapsed()>=5000){wanted_=false;syncGeometry();window_->setCursor(Qt::BlankCursor);if(surface_)surface_->setCursor(Qt::BlankCursor);}
}
bool FullscreenControls::eventFilter(QObject *object,QEvent *event){
    if(object==transport_ && event->type()==QEvent::Close && window_){event->ignore();window_->close();return true;}
    if(object==window_ || object==surface_ || object==transport_){
        switch(event->type()){
        case QEvent::Move:if(object!=transport_ && !playbackWindowsMoving(window_))syncGeometry();break;
        case QEvent::Resize:case QEvent::Show:case QEvent::Hide:case QEvent::WindowStateChange:case QEvent::ParentChange:
            if(object==window_ && (event->type()==QEvent::Show || event->type()==QEvent::WindowStateChange) && window_ && !window_->isMinimized()){wanted_=true;idle_.restart();window_->unsetCursor();if(surface_)surface_->unsetCursor();}
            queueSync();break;
        default:break;
        }
    }
    auto *widget=qobject_cast<QWidget*>(object);
    if(!widget || !window_)return false;
    bool owned=false;for(auto *owner=widget;owner;owner=owner->parentWidget())if(owner==window_){owned=true;break;}
    if(!owned)return false;
    if(widget->windowModality()!=Qt::NonModal && (event->type()==QEvent::Show || event->type()==QEvent::Hide))queueSync();
    if(widget!=transport_ && widget->windowType()==Qt::Tool){
        if(event->type()==QEvent::WindowActivate)activity();
        else if(event->type()==QEvent::Show || event->type()==QEvent::ZOrderChange){raisePending_=true;queueSync();}
    }
    switch(event->type()){
    case QEvent::MouseMove:case QEvent::MouseButtonPress:case QEvent::MouseButtonRelease:case QEvent::Wheel:
    case QEvent::KeyPress:case QEvent::KeyRelease:case QEvent::ShortcutOverride:case QEvent::TouchBegin:activity();break;
    default:break;
    }
    return false;
}
}
