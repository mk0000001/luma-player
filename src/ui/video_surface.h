#pragma once
#include <QPointer>
#include <QWidget>
#include <QUrl>
#include <QTimer>
#include "disc_menu_preview.h"
namespace luma {
class VideoSurface final:public QWidget {
    Q_OBJECT
public:
    explicit VideoSurface(QWidget *parent=nullptr);
    ~VideoSurface()override;
    WId videoWindowId();
    QWidget *videoWindow()const;
    void setVideoActive(bool active);
    QImage remoteDiscMenuFrame();
signals:
    void urlsDropped(QList<QUrl> urls);
    void videoStackingChanged();
protected:
    bool eventFilter(QObject *object,QEvent *event)override;
    void paintEvent(QPaintEvent *event)override;
private:
    QPointer<QWidget> video_,host_;
    DiscMenuPreview menuPreview_;
    QTimer menuPreviewIdle_;
    bool active_=false,syncPending_=false,stackingPending_=false;
    void syncWindow();
    void queueSync();
    void queueStackingSync();
};
}
