#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>
#include <vector>
#include "core/subtitle.h"
#include "ai_backend.h"
namespace luma {
class PcmBuffer;
struct RecognitionRequest { QString source,language="ja",playlist; qint64 startMs=0,durationMs=30000; bool disc=false; int title=1,audioIndex=0,audioTrackId=-1; };
struct ProcessInvocation { QString program; QStringList arguments; };
ProcessInvocation extractionCommand(const QString &runtime,const RecognitionRequest &request,const QString &output);
ProcessInvocation recognitionCommand(const QString &runtime,const QString &model,const QString &language,const QString &wav,const QString &prefix);
QString writeSrt(const QVector<SubtitleCue> &cues);
class SubtitleJob : public QObject {
    Q_OBJECT
public:
    explicit SubtitleJob(QString runtime={},QObject *parent=nullptr);
    ~SubtitleJob() override;
    quint64 start(const RecognitionRequest &request,const QString &model={});
    void cancel();
    void warmUp(const QString &model={});
    void shutdown();
    void setStageTimeouts(int extractionMs,int recognitionMs);
    void setAcceleratorTimeout(int milliseconds);
    QString selectedBackend()const{return selectedBackend_;}
    void setPreferences(const AiPreferences &preferences);
    AiPreferences preferences()const{return preferences_;}
    RecognitionRequest currentRequest()const{return request_;}
    bool busy() const;
    quint64 generation() const { return generation_; }
signals:
    void progress(QString message);
    void ready(quint64 generation,QVector<luma::SubtitleCue> cues);
    void failed(QString message);
    void backendSelected(QString backend);
    void warmedUp();
    void recognitionStarted(quint64 generation);
private:
    QString runtime_,model_,lastOutput_;
    QProcess process_,worker_;
    std::unique_ptr<PcmBuffer> pcmBuffer_;
    QString workerSignature_,workerPrefix_,workerBackend_,queuedWorkerRequest_;
    QByteArray workerBuffer_;
    bool workerBusy_=false,workerWarming_=false;
    bool warmupEnabled_=false;
    quint64 warmupEpoch_=0;
    std::vector<std::unique_ptr<QTemporaryDir>> retiredTemps_;
    QTimer warmupTimeout_;
    void stopWorker();
    void workerOutput();
    void sendQueuedRequest();
    QTimer timeout_;
    int extractionTimeoutMs_=90000,recognitionTimeoutMs_=120000;
    int acceleratorTimeoutMs_=90000;
    QString selectedBackend_;
    AiPreferences preferences_;
    QString resultPrefix_,rejectionModel_;
    QVector<AiBackend> backends_;
    QSet<QString> rejected_;
    int backendIndex_=-1;
    std::unique_ptr<QTemporaryDir> temp_;
    RecognitionRequest request_;
    quint64 generation_=0;
    int stage_=0;
    void run(const ProcessInvocation &command);
    void completed(int code,QProcess::ExitStatus status);
    void nextRecognition(const QString &reason={});
    void audioPrepared();
};
}
