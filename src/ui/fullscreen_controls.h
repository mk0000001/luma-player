#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QPoint>
#include <QTimer>
class QWidget;
namespace luma {
class FullscreenControls final:public QObject {
public:
    FullscreenControls(QWidget *window,QWidget *transport,QWidget *surface);
    void setActive(bool active);
    void activity();
    bool active()const{return active_;}
protected:
    bool eventFilter(QObject *object,QEvent *event)override;
private:
    QPointer<QWidget> window_,transport_,surface_;
    QTimer timer_;
    QElapsedTimer idle_;
    QPoint pointer_;
    bool active_=false;
    bool interactionInProgress()const;
    void poll();
    void reveal();
};
}
