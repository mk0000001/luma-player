#include "video_surface.h"
#include <QApplication>
#include <QEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QTimer>

namespace luma {
namespace {constexpr auto videoFlags=Qt::Tool|Qt::FramelessWindowHint;}
VideoSurface::VideoSurface(QWidget *parent):QWidget(parent),video_(new QWidget(nullptr,videoFlags)){
    setObjectName("videoSurface");setAttribute(Qt::WA_OpaquePaintEvent);setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
    video_->setObjectName("nativeVideoWindow");video_->setAttribute(Qt::WA_NativeWindow);video_->setAttribute(Qt::WA_ShowWithoutActivating);
    video_->setFocusPolicy(Qt::StrongFocus);video_->setMouseTracking(true);video_->setAutoFillBackground(true);video_->setAcceptDrops(true);
    QPalette black=video_->palette();black.setColor(QPalette::Window,Qt::black);video_->setPalette(black);
    video_->setStyleSheet("#nativeVideoWindow{background:#000000;}");
    qApp->installEventFilter(this);queueSync();
}
VideoSurface::~VideoSurface(){delete video_.data();}
WId VideoSurface::videoWindowId(){syncWindow();return video_?video_->winId():0;}
QWidget *VideoSurface::videoWindow()const{const_cast<VideoSurface*>(this)->syncWindow();return video_.data();}
void VideoSurface::setVideoActive(bool active){active_=active;syncWindow();}
void VideoSurface::queueSync(){
    if(syncPending_)return;syncPending_=true;QTimer::singleShot(0,this,[this]{syncPending_=false;syncWindow();});
}
void VideoSurface::syncWindow(){
    if(!video_)return;
    QWidget *owner=window();if(owner==this && !parentWidget())owner=nullptr;
    if(host_!=owner){host_=owner;video_->setParent(owner,videoFlags);}
    const QRect target(mapToGlobal(QPoint()),size());
    if(video_->geometry()!=target)video_->setGeometry(target);
    const bool visible=active_ && host_ && host_->isVisible() && !host_->isMinimized() && isVisible() && !size().isEmpty();
    if(visible){if(!video_->isVisible()){video_->show();video_->lower();}}
    else video_->hide();
}
bool VideoSurface::eventFilter(QObject *object,QEvent *event){
    if(object==video_ && host_){
        if(event->type()==QEvent::Close){event->ignore();host_->close();return true;}
        if(event->type()==QEvent::DragEnter || event->type()==QEvent::Drop){
            auto *drop=static_cast<QDropEvent*>(event);
            if(!drop->mimeData()->hasUrls()){drop->ignore();return true;}
            drop->acceptProposedAction();
            // QApplication routes Drop to its saved drag target, even when
            // sendEvent names another widget. Forward values instead of events.
            if(event->type()==QEvent::Drop)emit urlsDropped(drop->mimeData()->urls());
            return true;
        }
    }
    if(object==this && event->type()==QEvent::CursorChange && video_){
        if(testAttribute(Qt::WA_SetCursor))video_->setCursor(cursor());else video_->unsetCursor();
    }
    if(object==this || object==host_){
        switch(event->type()){
        case QEvent::Move:syncWindow();break;
        case QEvent::Resize:case QEvent::Show:case QEvent::Hide:case QEvent::ParentChange:case QEvent::WindowStateChange:queueSync();break;
        default:break;
        }
    }
    return false;
}
void VideoSurface::paintEvent(QPaintEvent *){QPainter painter(this);painter.fillRect(rect(),Qt::black);}
}
