#include "caption_overlay.h"
#include <QEvent>
#include <QPainter>
#include <QFontMetrics>
#include <QTimer>
#include <algorithm>
namespace luma {
CaptionOverlay::CaptionOverlay(QWidget *anchor):QWidget(anchor->window(),Qt::Tool|Qt::FramelessWindowHint|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus),anchor_(anchor){
    setObjectName("captionOverlay");setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_ShowWithoutActivating);setFocusPolicy(Qt::NoFocus);
    anchor->installEventFilter(this);if(anchor->window()!=anchor)anchor->window()->installEventFilter(this);
}
void CaptionOverlay::setCues(const QVector<SubtitleCue> &cues){const auto generation=timeline_.reset();timeline_.attach(generation,cues);text_.clear();hide();}
void CaptionOverlay::setPosition(qint64 position){const auto text=timeline_.at(position);if(text!=text_){text_=text;update();}syncGeometry();}
bool CaptionOverlay::eventFilter(QObject *,QEvent *event){
    switch(event->type()){
    case QEvent::Move:case QEvent::Resize:case QEvent::Show:case QEvent::Hide:case QEvent::WindowStateChange:
        QTimer::singleShot(0,this,&CaptionOverlay::syncGeometry);break;
    default:break;
    }return false;
}
void CaptionOverlay::syncGeometry(){
    if(!anchor_ || !anchor_->isVisible() || anchor_->window()->isMinimized() || text_.isEmpty()){hide();return;}
    const QRect target(anchor_->mapToGlobal(QPoint(0,0)),anchor_->size());if(geometry()!=target)setGeometry(target);
    if(!isVisible())show();
}
void CaptionOverlay::paintEvent(QPaintEvent *){
    if(text_.isEmpty())return;QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
    QFont font;font.setFamilies({"Noto Sans CJK JP","Noto Sans CJK KR","Noto Sans"});font.setPixelSize(std::clamp(width()/30,18,34));font.setWeight(QFont::DemiBold);painter.setFont(font);
    const QRect bounds(24,20,std::max(1,width()-48),std::max(1,height()-40));const int flags=Qt::AlignHCenter|Qt::AlignVCenter|Qt::TextWordWrap;
    QRect textBounds=QFontMetrics(font).boundingRect(QRect(0,0,bounds.width()-28,bounds.height()),flags,text_);
    const int boxHeight=std::min(bounds.height(),textBounds.height()+20);const int boxWidth=std::min(bounds.width(),textBounds.width()+28);
    QRect box((width()-boxWidth)/2,height()-boxHeight-std::max(24,height()/18),boxWidth,boxHeight);
    painter.setPen(Qt::NoPen);painter.setBrush(QColor(0,0,0,180));painter.drawRoundedRect(box,10,10);
    painter.setPen(Qt::white);painter.drawText(box.adjusted(14,7,-14,-7),flags,text_);
}
}
