#include "video_surface.h"
#include "native_playback_move.h"
#include <QApplication>
#include <QEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QTimer>
#ifdef Q_OS_WIN
#include <functional>
#include <utility>
#include <windows.h>
#endif

namespace luma {
namespace {
constexpr auto videoFlags=Qt::Tool|Qt::FramelessWindowHint;
#ifdef Q_OS_WIN
class NativeVideoWindow final : public QWidget {
public:
    explicit NativeVideoWindow(std::function<void()> stackingChanged)
        : QWidget(nullptr, videoFlags), stackingChanged_(std::move(stackingChanged)) {}
protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override {
        if (eventType == "windows_generic_MSG") {
            const auto &event = *static_cast<const MSG *>(message);
            if (event.message == WM_WINDOWPOSCHANGED && event.lParam) {
                const auto &position = *reinterpret_cast<const WINDOWPOS *>(event.lParam);
                if (!(position.flags & SWP_NOZORDER))
                    stackingChanged_();
            }
        }
        return QWidget::nativeEvent(eventType, message, result);
    }
private:
    std::function<void()> stackingChanged_;
};
#endif
}
VideoSurface::VideoSurface(QWidget *parent):QWidget(parent){
#ifdef Q_OS_WIN
    video_=new NativeVideoWindow([this]{queueStackingSync();});
#else
    video_=new QWidget(nullptr,videoFlags);
#endif
    setObjectName("videoSurface");setAttribute(Qt::WA_OpaquePaintEvent);setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
    video_->setObjectName("nativeVideoWindow");video_->setAttribute(Qt::WA_NativeWindow);video_->setAttribute(Qt::WA_ShowWithoutActivating);
    video_->setFocusPolicy(Qt::StrongFocus);video_->setMouseTracking(true);video_->setAutoFillBackground(true);video_->setAcceptDrops(true);
    QPalette black=video_->palette();black.setColor(QPalette::Window,Qt::black);video_->setPalette(black);
    video_->setStyleSheet("#nativeVideoWindow{background:#000000;}");
    menuPreviewIdle_.setSingleShot(true);
    menuPreviewIdle_.setInterval(1800);
    connect(&menuPreviewIdle_, &QTimer::timeout, this, [this] { menuPreview_.reset(); });
    qApp->installEventFilter(this);queueSync();
}
VideoSurface::~VideoSurface(){delete video_.data();}
WId VideoSurface::videoWindowId(){syncWindow();return video_?video_->winId():0;}
QWidget *VideoSurface::videoWindow()const{return video_.data();}
void VideoSurface::setVideoActive(bool active){
    active_=active;
    if (!active) menuPreview_.reset();
    syncWindow();
}
QImage VideoSurface::remoteDiscMenuFrame() {
    if (!video_ || !video_->isVisible()) return {};
    menuPreviewIdle_.start();
    return menuPreview_.latestFrame(video_->winId());
}
void VideoSurface::queueSync(){
    if(syncPending_)return;syncPending_=true;QTimer::singleShot(0,this,[this]{syncPending_=false;syncWindow();});
}
void VideoSurface::queueStackingSync(){
    if(stackingPending_)return;
    stackingPending_=true;
    QTimer::singleShot(0,this,[this]{stackingPending_=false;emit videoStackingChanged();});
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
        if(event->type()==QEvent::WindowActivate || event->type()==QEvent::ZOrderChange || event->type()==QEvent::Show)
            queueStackingSync();
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
        case QEvent::Move:if(!playbackWindowsMoving(host_))syncWindow();break;
        case QEvent::Resize:case QEvent::Show:case QEvent::Hide:case QEvent::ParentChange:case QEvent::WindowStateChange:queueSync();break;
        case QEvent::WinIdChange:case QEvent::WindowActivate:queueStackingSync();break;
        default:break;
        }
    }
    return false;
}
void VideoSurface::paintEvent(QPaintEvent *){QPainter painter(this);painter.fillRect(rect(),Qt::black);}
}
