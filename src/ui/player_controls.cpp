#include "player_controls.h"
#include <QApplication>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>
#include <algorithm>

static void initializeLumaResources(){Q_INIT_RESOURCE(luma);}

namespace luma {
namespace {
void paintMark(QPainter &p,const QRectF &rect){
    p.save();p.translate(rect.topLeft());p.scale(rect.width()/64.,rect.height()/64.);
    p.setPen(Qt::NoPen);p.setBrush(QColor("#2189ff"));p.drawRoundedRect(QRectF(0,0,64,64),19,19);
    p.setBrush(Qt::white);p.drawPolygon(QPolygonF{{19,15},{19,49},{46,49},{46,41},{27,41},{27,15}});
    p.drawPolygon(QPolygonF{{35,15},{50,24},{35,33}});p.restore();
}
void paintIcon(QPainter &p,PlayerIcon icon){
    switch(icon){
    case PlayerIcon::Play:p.setBrush(p.pen().color());p.setPen(Qt::NoPen);p.drawPolygon(QPolygonF{{-5,-8},{8,0},{-5,8}});break;
    case PlayerIcon::Pause:p.drawLine(QPointF(-4,-7),QPointF(-4,7));p.drawLine(QPointF(4,-7),QPointF(4,7));break;
    case PlayerIcon::Previous:case PlayerIcon::Next:{
        const qreal direction=icon==PlayerIcon::Next?1.:-1.;p.scale(direction,1);p.drawLine(QPointF(7,-7),QPointF(7,7));
        p.setBrush(p.pen().color());p.setPen(Qt::NoPen);p.drawPolygon(QPolygonF{{-7,-7},{3,0},{-7,7}});break;}
    case PlayerIcon::Rewind:case PlayerIcon::Forward:{
        const bool back=icon==PlayerIcon::Rewind;p.drawArc(QRectF(-8,-9,16,16),back?30*16:150*16,back?280*16:-280*16);
        p.drawLine(back?QPointF(7,-9):QPointF(-7,-9),back?QPointF(7,-3):QPointF(-7,-3));
        QFont f=p.font();f.setPixelSize(9);f.setBold(true);p.setFont(f);p.drawText(QRectF(-10,-3,20,14),Qt::AlignCenter,"10");break;}
    case PlayerIcon::Fullscreen:case PlayerIcon::ExitFullscreen:{
        const qreal sign=icon==PlayerIcon::Fullscreen?1.:-1.;
        for(const auto corner:{QPointF(-1,-1),QPointF(1,-1),QPointF(-1,1),QPointF(1,1)}){
            const QPointF at=corner*8;const QPointF origin=icon==PlayerIcon::Fullscreen?at:at*.45;
            p.drawLine(origin,origin+QPointF(-corner.x()*5*sign,0));p.drawLine(origin,origin+QPointF(0,-corner.y()*5*sign));}break;}
    case PlayerIcon::Volume:p.drawPolyline(QPolygonF{{-9,-3},{-5,-3},{0,-7},{0,7},{-5,3},{-9,3},{-9,-3}});p.drawArc(QRectF(-5,-6,12,12),-65*16,130*16);p.drawArc(QRectF(-7,-10,20,20),-55*16,110*16);break;
    case PlayerIcon::Folder:p.drawPolyline(QPolygonF{{-10,7},{-10,-7},{-3,-7},{0,-4},{10,-4},{10,7},{-10,7}});break;
    case PlayerIcon::Disc:p.drawEllipse(QRectF(-9,-9,18,18));p.drawEllipse(QRectF(-2,-2,4,4));p.drawArc(QRectF(-6,-6,12,12),20*16,70*16);break;
    case PlayerIcon::Sun:p.drawEllipse(QRectF(-4,-4,8,8));for(int i=0;i<8;++i){p.save();p.rotate(i*45);p.drawLine(QPointF(0,-7),QPointF(0,-10));p.restore();}break;
    case PlayerIcon::Moon:{QPainterPath shape;shape.moveTo(4,-9);shape.cubicTo(-12,-8,-10,12,5,8);shape.cubicTo(-2,7,-4,-2,4,-9);p.drawPath(shape);break;}
    case PlayerIcon::Up:p.drawPolyline(QPolygonF{{-6,3},{0,-3},{6,3}});break;
    case PlayerIcon::Down:p.drawPolyline(QPolygonF{{-6,-3},{0,3},{6,-3}});break;
    case PlayerIcon::Left:p.drawPolyline(QPolygonF{{3,-6},{-3,0},{3,6}});break;
    case PlayerIcon::Right:p.drawPolyline(QPolygonF{{-3,-6},{3,0},{-3,6}});break;
    case PlayerIcon::Confirm:p.drawPolyline(QPolygonF{{-6,0},{-1,5},{7,-5}});break;
    }
}
}
QIcon playerApplicationIcon(){
    static const bool initialized=[] {initializeLumaResources();return true;}();Q_UNUSED(initialized);
    QIcon icon;for(int size:{16,24,32,48,64,128,256}){QPixmap pixmap(size,size);pixmap.fill(Qt::transparent);QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);paintMark(p,QRectF(0,0,size,size));p.end();icon.addPixmap(pixmap);}return icon;
}
BrandMark::BrandMark(QWidget *parent):QWidget(parent){setFixedSize(137,36);setAccessibleName("LumaPlayer");setAttribute(Qt::WA_TranslucentBackground);}
void BrandMark::paintEvent(QPaintEvent *){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);paintMark(p,QRectF(0,0,36,36));
    QFont font=p.font();font.setPixelSize(23);font.setWeight(QFont::Bold);font.setLetterSpacing(QFont::AbsoluteSpacing,2);p.setFont(font);p.setPen(palette().color(QPalette::WindowText));p.drawText(QRect(46,0,91,36),Qt::AlignVCenter,"LUMA");
}
IconButton::IconButton(PlayerIcon icon,const QString &description,QWidget *parent):QPushButton(parent),icon_(icon){
    setObjectName("iconButton");setAccessibleName(description);setToolTip(description);setCursor(Qt::PointingHandCursor);setFixedSize(34,34);
}
void IconButton::setPlayerIcon(PlayerIcon icon){if(icon_!=icon){icon_=icon;update();}}
void IconButton::paintEvent(QPaintEvent *event){
    QPushButton::paintEvent(event);QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.translate(width()/2.,height()/2.);
    const QColor color=objectName()=="playButton"?QColor(Qt::white):palette().color(isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::ButtonText);
    p.setPen(QPen(color,1.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);paintIcon(p,icon_);
}
ChoiceBox::ChoiceBox(QWidget *parent):QComboBox(parent){setCursor(Qt::PointingHandCursor);setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);setMinimumContentsLength(6);setMinimumHeight(38);}
QSize ChoiceBox::sizeHint()const{return QSize(std::max(80,QComboBox::sizeHint().width()),38);}
QSize ChoiceBox::minimumSizeHint()const{return QSize(68,34);}
void ChoiceBox::paintEvent(QPaintEvent *){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(hasFocus()?palette().highlight().color():palette().mid().color(),1));p.setBrush(palette().button());p.drawRoundedRect(QRectF(.5,.5,width()-1.,height()-1.),height()/2.,height()/2.);
    p.setPen(palette().color(isEnabled()?QPalette::Active:QPalette::Disabled,QPalette::ButtonText));
    const QString text=count()?currentText():QString("선택 없음");p.drawText(QRect(13,0,std::max(1,width()-38),height()),Qt::AlignVCenter|Qt::AlignLeft,fontMetrics().elidedText(text,Qt::ElideRight,std::max(1,width()-38)));
    p.setPen(QPen(palette().color(QPalette::ButtonText),1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));const qreal x=width()-17,y=height()/2.;p.drawPolyline(QPolygonF{{x-3,y-1},{x,y+2},{x+3,y-1}});
}
void ChoiceBox::showPopup(){
    if(popup_ || !count())return;
    auto *menu=new QMenu(this);popup_=menu;menu->setObjectName("choiceMenu");menu->setAttribute(Qt::WA_DeleteOnClose);menu->setMinimumWidth(width());
    for(int i=0;i<count();++i){auto *action=menu->addAction(itemText(i));action->setCheckable(true);action->setChecked(i==currentIndex());action->setEnabled(model()->flags(model()->index(i,modelColumn(),rootModelIndex())).testFlag(Qt::ItemIsEnabled));connect(action,&QAction::triggered,this,[this,i]{setCurrentIndex(i);emit activated(i);});}
    connect(menu,&QMenu::aboutToHide,this,[this]{popup_=nullptr;QComboBox::hidePopup();update();});menu->popup(mapToGlobal(QPoint(0,height()+5)));
}
void ChoiceBox::hidePopup(){if(popup_)popup_->close();QComboBox::hidePopup();}
}
