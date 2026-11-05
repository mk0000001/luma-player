#pragma once
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTextLayout>
#include <memory>
class QPainter;
class QPaintDevice;
namespace luma {
struct CaptionLayout {
    std::unique_ptr<QTextLayout> text;
    QRectF background;
    QPointF origin;
    qreal scale=1;
};
CaptionLayout layoutCaption(const QString &text,const QSizeF &viewport,qreal bottomInset=0,QPaintDevice *device=nullptr);
void paintCaption(QPainter &painter,const CaptionLayout &layout);
}
