#include "caption_overlay.h"
#include <QEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>
namespace luma {
CaptionOverlay::CaptionOverlay(QWidget *anchor):QWidget(anchor->window(),Qt::Tool|Qt::FramelessWindowHint|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus),anchor_(anchor){
    setObjectName("captionOverlay");setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_ShowWithoutActivating);setFocusPolicy(Qt::NoFocus);
    anchor->installEventFilter(this);if(anchor->window()!=anchor)anchor->window()->installEventFilter(this);
}
void CaptionOverlay::setCues(const QVector<SubtitleCue> &cues){const auto generation=timeline_.reset();timeline_.attach(generation,cues);text_.clear();hide();}
void CaptionOverlay::setPosition(qint64 position){const auto text=timeline_.at(position);if(text!=text_){text_=text;update();}syncGeometry();}
void CaptionOverlay::setBottomInset(int pixels){pixels=std::max(0,pixels);if(bottomInset_!=pixels){bottomInset_=pixels;update();}}
void CaptionOverlay::setCaptionStyle(const CaptionStyle &style){
    style_=style;
    update();
}
bool CaptionOverlay::eventFilter(QObject *,QEvent *event){
    switch(event->type()){
    case QEvent::Move:syncGeometry();break;
    case QEvent::Resize:case QEvent::Show:case QEvent::Hide:case QEvent::WindowStateChange:
        if(!syncPending_){syncPending_=true;QTimer::singleShot(0,this,[this]{syncPending_=false;syncGeometry();});}
        break;
    default:break;
    }return false;
}
void CaptionOverlay::syncGeometry(){
    if(!anchor_ || !anchor_->isVisible() || anchor_->window()->isMinimized() || text_.isEmpty()){hide();return;}
    const QRect target(anchor_->mapToGlobal(QPoint(0,0)),anchor_->size());if(geometry()!=target)setGeometry(target);
    if(!isVisible())show();
}
void CaptionOverlay::paintEvent(QPaintEvent *){
    if(text_.isEmpty())return;
    QPainter painter(this);
    paintCaption(painter,layoutCaption(text_,size(),bottomInset_,this,style_),style_);
}
}
