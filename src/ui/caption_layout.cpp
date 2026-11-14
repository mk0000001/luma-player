#include "caption_layout.h"
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QTextOption>
#include <algorithm>

namespace luma {
namespace {
std::unique_ptr<QTextLayout> shapeCaption(const QString &text,const QFont &font,qreal width,QPaintDevice *device){
    auto result=std::make_unique<QTextLayout>(text,font,device);
    QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);option.setFlags(QTextOption::IncludeTrailingSpaces);
    result->setTextOption(option);result->beginLayout();qreal y=0;
    for(auto line=result->createLine();line.isValid();line=result->createLine()){
        line.setLineWidth(width);line.setPosition(QPointF(0,y));y+=line.height();
    }
    result->endLayout();return result;
}
QSizeF textSize(const QTextLayout &text){
    qreal width=0,height=0;
    for(int i=0;i<text.lineCount();++i){const auto line=text.lineAt(i);width=std::max(width,line.naturalTextWidth());height=std::max(height,line.y()+line.height());}
    return QSizeF(width,height);
}
}
CaptionLayout layoutCaption(const QString &source,const QSizeF &viewport,qreal bottomInset,QPaintDevice *device,const CaptionStyle &style){
    CaptionLayout result;
    const qreal width=viewport.width(),height=std::max(qreal(0),viewport.height()-std::max(qreal(0),bottomInset));
    if(source.isEmpty() || width<=0 || height<=0)return result;
    QString text=source;text.replace("\r\n","\n");text.replace('\r','\n');text.replace('\n',QChar::LineSeparator);
    const qreal horizontalMargin=std::min(qreal(24),width*.04),topMargin=std::min(qreal(20),height*.05);
    const qreal bottomMargin=std::min(height*.12,std::max(qreal(4),height/18));
    const qreal paddingX=std::min(qreal(9),width*.03),paddingY=std::min(qreal(4),height*.03);
    const qreal textWidth=std::max(qreal(.01),width-2*horizontalMargin-2*paddingX),textHeight=std::max(qreal(.01),height-topMargin-bottomMargin-2*paddingY);
    QStringList families{"Noto Sans CJK JP","Noto Sans CJK KR","Noto Sans","Noto Color Emoji","Segoe UI Emoji"};
    if(!style.fontFamily.isEmpty()){
        families.removeAll(style.fontFamily);
        families.prepend(style.fontFamily);
    }
    QFont font;
    font.setFamilies(families);
    font.setWeight(QFont::DemiBold);
    const int preferredPixels=qRound(std::clamp(qRound(width/30),18,34)*std::clamp(style.sizePercent,50,200)/100.);
    QSizeF size;
    for(int pixels=preferredPixels;pixels>=1;--pixels){
        font.setPixelSize(pixels);result.text=shapeCaption(text,font,std::max(qreal(1),textWidth),device);size=textSize(*result.text);
        if(size.height()<=textHeight || pixels==1)break;
    }
    if(!result.text || result.text->lineCount()==0)return result;
    for(int i=0;i<result.text->lineCount();++i){auto line=result.text->lineAt(i);line.setPosition(QPointF((size.width()-line.naturalTextWidth())/2,line.y()));}
    result.scale=std::min({qreal(1),textWidth/std::max(qreal(.01),size.width()),textHeight/std::max(qreal(.01),size.height())});
    const QSizeF box(size.width()*result.scale+2*paddingX,size.height()*result.scale+2*paddingY);
    result.background=QRectF(QPointF((width-box.width())/2,height-bottomMargin-box.height()),box);
    result.origin=result.background.topLeft()+QPointF(paddingX,paddingY);return result;
}
void paintCaption(QPainter &painter,const CaptionLayout &layout,const CaptionStyle &style){
    if(!layout.text || layout.background.isEmpty())return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0,0,0,qRound(std::clamp(style.backgroundOpacity,0,100)*255/100.)));
    if(style.backgroundOpacity>0){
        const QPointF padding=layout.origin-layout.background.topLeft();
        QPainterPath backdrop;
        backdrop.setFillRule(Qt::WindingFill);
        for(int i=0;i<layout.text->lineCount();++i){
            const auto line=layout.text->lineAt(i).naturalTextRect();
            if(line.width()<=0)continue;
            const QRectF scaled(layout.origin+line.topLeft()*layout.scale,line.size()*layout.scale);
            backdrop.addRoundedRect(scaled.adjusted(-padding.x(),-padding.y(),padding.x(),padding.y()),5,5);
        }
        painter.drawPath(backdrop);
    }
    painter.setPen(Qt::white);
    painter.setOpacity(std::clamp(style.textOpacity,0,100)/100.);
    painter.translate(layout.origin);
    painter.scale(layout.scale,layout.scale);
    layout.text->draw(&painter,QPointF());painter.restore();
}
}
