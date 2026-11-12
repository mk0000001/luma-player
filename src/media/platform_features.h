#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>
namespace luma {
QJsonArray videoAdapters();
QJsonObject displayCapabilities(WId windowId);
class CaptureGuard:public QObject {
    Q_OBJECT
public:
    explicit CaptureGuard(QObject *parent=nullptr);
    void watch(QWidget *window);
    void setEnabled(bool enabled, QWidget *onlyWindow=nullptr);
    bool enabled()const{return enabled_;}
    bool applied()const;
    QString status()const;
protected:
    bool eventFilter(QObject *object,QEvent *event)override;
private:
    QList<QPointer<QWidget>> windows_;
    QPointer<QWidget> onlyWindow_;
    bool enabled_=false;
    void apply();
};
}
