#pragma once
#include <QWidget>
#include <QPointer>
#include "core/subtitle.h"
namespace luma {
class CaptionOverlay:public QWidget {
    Q_OBJECT
public:
    explicit CaptionOverlay(QWidget *anchor);
    void setCues(const QVector<SubtitleCue> &cues);
    void setPosition(qint64 positionMs);
    QString captionText()const{return text_;}
protected:
    bool eventFilter(QObject *object,QEvent *event)override;
    void paintEvent(QPaintEvent *event)override;
private:
    QPointer<QWidget> anchor_;
    SubtitleTimeline timeline_;
    QString text_;
    void syncGeometry();
};
}
