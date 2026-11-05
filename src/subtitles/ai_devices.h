#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonArray>
namespace luma {
class AiDeviceProbe:public QObject {
    Q_OBJECT
public:
    explicit AiDeviceProbe(QString runtime={},QObject *parent=nullptr);
    ~AiDeviceProbe()override;
    void start();
signals:
    void devicesReady(QJsonArray devices);
    void failed(QString reason);
private:
    QString runtime_;
    QProcess process_;
    QTimer timer_;
};
}
