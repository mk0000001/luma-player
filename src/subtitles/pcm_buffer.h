#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QTemporaryDir>
#include <memory>
namespace luma {
struct RecognitionRequest;
class PcmBuffer:public QObject {
    Q_OBJECT
public:
    explicit PcmBuffer(QString runtime,QObject *parent=nullptr);
    ~PcmBuffer()override;
    void prepare(const RecognitionRequest &request,const QString &wav);
    void cancel();
signals:
    void ready();
    void failed(QString message);
private:
    QString runtime_,key_,wav_,error_;
    qint64 cacheStart_=0,start_=0,duration_=0;
    bool pending_=false;
    QProcess reader_;
    QTimer poll_;
    std::unique_ptr<QTemporaryDir> cache_;
    void check();
};
}
