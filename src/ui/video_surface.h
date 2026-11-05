#pragma once
#include <QPointer>
#include <QWidget>
#include <QUrl>
namespace luma {
class VideoSurface final:public QWidget {
    Q_OBJECT
public:
    explicit VideoSurface(QWidget *parent=nullptr);
    ~VideoSurface()override;
    WId videoWindowId();
    QWidget *videoWindow()const;
    void setVideoActive(bool active);
signals:
    void urlsDropped(QList<QUrl> urls);
protected:
    bool eventFilter(QObject *object,QEvent *event)override;
    void paintEvent(QPaintEvent *event)override;
private:
    QPointer<QWidget> video_,host_;
    bool active_=false,syncPending_=false;
    void syncWindow();
    void queueSync();
};
}
