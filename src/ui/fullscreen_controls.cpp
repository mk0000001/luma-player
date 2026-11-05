#include "fullscreen_controls.h"
#include <QAbstractButton>
#include <QAbstractSlider>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QWidget>
namespace luma {
FullscreenControls::FullscreenControls(QWidget *window,QWidget *transport,QWidget *surface):QObject(window),window_(window),transport_(transport),surface_(surface){
    qApp->installEventFilter(this);timer_.setInterval(100);connect(&timer_,&QTimer::timeout,this,&FullscreenControls::poll);
}
void FullscreenControls::setActive(bool active){
    active_=active;pointer_=QCursor::pos();reveal();idle_.restart();
    if(active_)timer_.start();else timer_.stop();
}
void FullscreenControls::reveal(){
    if(transport_)transport_->show();if(window_)window_->unsetCursor();if(surface_)surface_->unsetCursor();
}
void FullscreenControls::activity(){if(!active_)return;reveal();idle_.restart();}
bool FullscreenControls::interactionInProgress()const{
    if(QApplication::activePopupWidget() || QApplication::activeModalWidget() || QApplication::mouseButtons()!=Qt::NoButton)return true;
    if(!window_)return false;
    for(auto *slider:window_->findChildren<QAbstractSlider*>())if(slider->isSliderDown())return true;
    for(auto *button:window_->findChildren<QAbstractButton*>())if(button->isDown())return true;
    return transport_ && transport_->isVisible() && QRect(transport_->mapToGlobal(QPoint()),transport_->size()).contains(QCursor::pos());
}
void FullscreenControls::poll(){
    if(!active_ || !window_ || !transport_)return;
    const auto current=QCursor::pos();
    if(current!=pointer_){pointer_=current;if(window_->frameGeometry().contains(current))activity();}
    if(interactionInProgress()){activity();return;}
    if(idle_.isValid() && idle_.elapsed()>=5000){transport_->hide();window_->setCursor(Qt::BlankCursor);if(surface_)surface_->setCursor(Qt::BlankCursor);}
}
bool FullscreenControls::eventFilter(QObject *object,QEvent *event){
    if(!active_)return false;
    auto *widget=qobject_cast<QWidget*>(object);
    if(!widget || !window_ || (widget!=window_ && !window_->isAncestorOf(widget)))return false;
    switch(event->type()){
    case QEvent::MouseMove:case QEvent::MouseButtonPress:case QEvent::MouseButtonRelease:case QEvent::Wheel:
    case QEvent::KeyPress:case QEvent::KeyRelease:case QEvent::ShortcutOverride:case QEvent::TouchBegin:activity();break;
    default:break;
    }
    return false;
}
}
