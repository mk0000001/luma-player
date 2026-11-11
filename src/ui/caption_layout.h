#pragma once
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTextLayout>
#include <memory>
class QPainter;
class QPaintDevice;
namespace luma {
struct CaptionStyle {
    QString fontFamily;
    int sizePercent=100;
    int textOpacity=100;
    int backgroundOpacity=71;
};

struct CaptionLayout {
    std::unique_ptr<QTextLayout> text;
    QRectF background;
    QPointF origin;
    qreal scale=1;
};
CaptionLayout layoutCaption(const QString &text,const QSizeF &viewport,qreal bottomInset=0,QPaintDevice *device=nullptr,const CaptionStyle &style={});
void paintCaption(QPainter &painter,const CaptionLayout &layout,const CaptionStyle &style={});
}
