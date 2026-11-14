#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>
namespace luma {
QJsonArray videoAdapters(bool includeDriverVersion=false);
QJsonObject displayCapabilities(WId windowId);
class CaptureGuard:public QObject {
    Q_OBJECT
public:
    explicit CaptureGuard(QObject *parent=nullptr);
    void watch(QWidget *window);
    void setEnabled(bool enabled);
    bool enabled()const{return enabled_;}
    bool applied()const;
    QString status()const;
protected:
    bool eventFilter(QObject *object,QEvent *event)override;
private:
    QList<QPointer<QWidget>> windows_;
    bool enabled_=false;
    void apply();
};
}
