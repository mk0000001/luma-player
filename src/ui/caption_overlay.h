#pragma once
#include <QWidget>
#include <QPointer>
#include "core/subtitle.h"
#include "caption_layout.h"
namespace luma {
class CaptionOverlay:public QWidget {
    Q_OBJECT
public:
    explicit CaptionOverlay(QWidget *anchor);
    void setCues(const QVector<SubtitleCue> &cues);
    void setPosition(qint64 positionMs);
    void setBottomInset(int pixels);
    void setCaptionStyle(const CaptionStyle &style);
    void setCaptionsVisible(bool visible);
    QString captionText()const{return text_;}
protected:
    bool eventFilter(QObject *object,QEvent *event)override;
    void paintEvent(QPaintEvent *event)override;
private:
    QPointer<QWidget> anchor_;
    SubtitleTimeline timeline_;
    QString text_;
    qint64 positionMs_=0;
    CaptionStyle style_;
    int bottomInset_=0;
    bool syncPending_=false,captionsVisible_=true;
    void syncGeometry();
};
}
