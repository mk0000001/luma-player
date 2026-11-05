#pragma once
#include <QJsonArray>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVector>

namespace luma {
// Device IDs belong to the translation runtime, independently of ASR IDs.
QJsonArray parseTranslationDevices(const QByteArray &output,const QString &backend);
class TranslationDeviceProbe : public QObject {
    Q_OBJECT
public:
    explicit TranslationDeviceProbe(QString runtime={},QObject *parent=nullptr);
    ~TranslationDeviceProbe() override;
    void start();
    void cancel();
    void setTimeout(int milliseconds);
signals:
    // Entries contain id, name, backend, type, memoryMiB and freeMiB.
    void devicesReady(QJsonArray devices);
    // Per-runtime diagnostic; another installed backend may still succeed.
    void failed(QString reason);
private:
    struct Candidate {QString backend,program;};
    void next();
    void scheduleNext();
    void readOutput();
    void finished(int code,QProcess::ExitStatus status);
    QString runtime_;
    QVector<Candidate> candidates_;
    int index_=-1;
    QProcess process_;
    QTimer timer_;
    QByteArray output_;
    QJsonArray devices_;
    bool active_=false,timedOut_=false,oversized_=false;
    quint64 generation_=0;
};
}
