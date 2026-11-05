#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>
#include "core/subtitle.h"
namespace luma {
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
    void setStageTimeouts(int extractionMs,int recognitionMs);
    bool busy() const;
    quint64 generation() const { return generation_; }
signals:
    void progress(QString message);
    void ready(quint64 generation,QVector<luma::SubtitleCue> cues);
    void failed(QString message);
private:
    QString runtime_,model_,lastOutput_;
    QProcess process_;
    QTimer timeout_;
    int extractionTimeoutMs_=90000,recognitionTimeoutMs_=120000;
    std::unique_ptr<QTemporaryDir> temp_;
    RecognitionRequest request_;
    quint64 generation_=0;
    int stage_=0;
    void run(const ProcessInvocation &command);
    void completed(int code,QProcess::ExitStatus status);
};
}
Q_DECLARE_METATYPE(QVector<luma::SubtitleCue>)
