#include "preparation_media_probe.h"
#include "runtime_environment.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <optional>

namespace luma {
namespace {
constexpr qsizetype maximumOutputBytes=512*1024;
constexpr qint64 maximumDurationMs=24*60*60*1000;
constexpr int timeoutMs=30000;

std::optional<PreparationMediaInfo> parseMetadata(const QString &output){
    static const QRegularExpression duration(R"((?:^|[\r\n])[ \t]*Duration:[ \t]*([0-9]{1,3}):([0-5][0-9]):([0-5][0-9])(?:\.([0-9]{1,9}))?,)");
    const auto time=duration.match(output);if(!time.hasMatch())return {};
    const auto fraction=time.captured(4).left(3).leftJustified(3,'0');
    PreparationMediaInfo info;
    info.durationMs=((time.captured(1).toLongLong()*60+time.captured(2).toLongLong())*60+time.captured(3).toLongLong())*1000+fraction.toLongLong();
    if(info.durationMs<=0 || info.durationMs>maximumDurationMs)return {};
    // Exactly one input is opened. Its printed stream order is the audio
    // ordinal order used by FFmpeg's 0:a:<ordinal> mapping during preparation.
    static const QRegularExpression audio(R"((?:^|[\r\n])[ \t]*Stream #0:[0-9]+(?:\[[^\]\r\n]*\])?(?:\(([^)\r\n]*)\))?[ \t]*:[ \t]*Audio:[ \t]*([^\r\n]+))");
    auto streams=audio.globalMatch(output);
    while(streams.hasNext()){
        if(info.audioTracks.size()>=256)return {};
        const auto stream=streams.next();const auto language=stream.captured(1).trimmed();const auto description=stream.captured(2).trimmed();
        if(description.isEmpty())return {};
        auto label=QString("오디오 %1").arg(info.audioTracks.size()+1);
        if(!language.isEmpty())label+=" · "+language.left(64);
        label+=" · "+description.left(256);info.audioTracks.append(std::move(label));
    }
    if(info.audioTracks.isEmpty())return {};
    return info;
}
QString diagnostic(const QString &reason,const QByteArray &output={}){
    auto detail=QString::fromUtf8(output).trimmed();
    // Diagnostics remain local and bounded even for malformed container metadata.
    for(auto &unit:detail)if(unit.unicode()<32 && unit!='\n' && unit!='\r' && unit!='\t')unit=QChar(' ');
    return (reason+(detail.isEmpty()?QString{}:"\n"+detail)).left(600);
}
}
struct PreparationMediaProbe::Impl {
    PreparationMediaProbe *q;
    QString runtime;
    quint64 generation=0;
    bool busy=false;
    QPointer<QProcess> process;
    QByteArray output;

    Impl(QString root,PreparationMediaProbe *owner):q(owner),runtime(std::move(root)){
        if(runtime.isEmpty())runtime=QCoreApplication::applicationDirPath()+"/runtime";
    }
    ~Impl(){
        stopProcess();
        // Cancellation stays asynchronous. At object destruction, also reap
        // killed requests whose finished signal has not reached the event loop.
        const auto children=q->findChildren<QProcess*>(QString{},Qt::FindDirectChildrenOnly);
        for(auto *child:children)if(child->state()!=QProcess::NotRunning){child->kill();child->waitForFinished(1000);}
    }
    void stopProcess(){
        if(!process)return;
        auto *old=process.data();process=nullptr;QObject::disconnect(old,nullptr,q,nullptr);
        if(old->state()==QProcess::NotRunning)old->deleteLater();
        else{
            QObject::connect(old,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),old,&QObject::deleteLater);
            old->kill();
        }
    }
    bool current(quint64 request,QProcess *worker)const{return busy && request==generation && process==worker;}
    void fail(quint64 request,const QString &message){
        if(!busy || request!=generation)return;
        busy=false;stopProcess();output.clear();emit q->failed(request,message.left(600));
    }
    bool read(quint64 request,QProcess *worker){
        if(!current(request,worker))return false;
        const auto bytes=worker->read(maximumOutputBytes-output.size()+1);
        if(bytes.size()>maximumOutputBytes-output.size() || worker->bytesAvailable()>0){
            fail(request,"미디어 검사 출력이 한도를 넘었습니다 / Media inspection output exceeded its limit.");return false;
        }
        output+=bytes;return true;
    }
    void inspect(quint64 request,const QString &localFile){
        static const QRegularExpression protocol(R"(^[A-Za-z][A-Za-z0-9+.-]*://)");
        const QFileInfo input(localFile);const auto canonical=input.canonicalFilePath();
        if(localFile.isEmpty() || localFile.contains(QChar(0)) || protocol.match(localFile).hasMatch() || localFile.startsWith("file:",Qt::CaseInsensitive) || !input.isFile() || canonical.isEmpty()){
            fail(request,"검사할 로컬 미디어 파일을 찾을 수 없습니다 / Select an existing local media file.");return;
        }
#ifdef Q_OS_WIN
        const auto program=QDir(runtime).filePath("tools/ffmpeg.exe");
#else
        const auto program=QDir(runtime).filePath("tools/ffmpeg");
#endif
        if(!QFileInfo(program).isFile()){
            fail(request,"로컬 미디어 검사 실행기가 없습니다 / The bundled FFmpeg inspection tool is missing.");return;
        }
        auto *worker=new QProcess(q);process=worker;
        worker->setProcessEnvironment(localRuntimeEnvironment(runtime));
        worker->setProcessChannelMode(QProcess::MergedChannels);
        worker->setWorkingDirectory(QFileInfo(program).absolutePath());
        QObject::connect(worker,&QProcess::readyReadStandardOutput,q,[this,worker,request]{read(request,worker);});
        QObject::connect(worker,&QProcess::errorOccurred,q,[this,worker,request](QProcess::ProcessError error){
            if(current(request,worker) && error==QProcess::FailedToStart)fail(request,diagnostic("미디어 검사를 시작하지 못했습니다 / Could not start media inspection.",worker->errorString().toUtf8()));
        });
        QObject::connect(worker,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),q,[this,worker,request](int code,QProcess::ExitStatus status){
            if(!read(request,worker))return;
            const auto text=QString::fromUtf8(output);
            static const QRegularExpression noOutput(R"((?:^|[\r\n])At least one output file must be specified[ \t]*(?:[\r\n]|$))");
            const auto info=parseMetadata(text);
            // FFmpeg has no output arguments, so this specific exit-one result
            // follows a successful header inspection. Other failures stay failures.
            if(status!=QProcess::NormalExit || code!=1 || !noOutput.match(text).hasMatch() || !info){
                fail(request,diagnostic("미디어 길이와 오디오 정보를 확인하지 못했습니다 / Could not inspect duration and audio tracks.",output));return;
            }
            busy=false;stopProcess();output.clear();emit q->ready(request,*info);
        });
        QTimer::singleShot(timeoutMs,Qt::PreciseTimer,worker,[this,worker,request]{
            if(current(request,worker))fail(request,"미디어 검사 시간이 초과되었습니다 / Media inspection timed out.");
        });
        worker->start(program,{"-hide_banner","-nostdin","-protocol_whitelist","file","-i",canonical});
    }
};
PreparationMediaProbe::PreparationMediaProbe(QString runtime,QObject *parent):QObject(parent),d(std::make_unique<Impl>(std::move(runtime),this)){qRegisterMetaType<PreparationMediaInfo>();}
PreparationMediaProbe::~PreparationMediaProbe()=default;
quint64 PreparationMediaProbe::inspect(const QString &localFile){
    cancel();d->busy=true;const auto request=d->generation;
    QTimer::singleShot(0,this,[this,request,localFile]{if(d->busy && request==d->generation)d->inspect(request,localFile);});return request;
}
void PreparationMediaProbe::cancel(){++d->generation;d->busy=false;d->stopProcess();d->output.clear();}
quint64 PreparationMediaProbe::generation()const{return d->generation;}
bool PreparationMediaProbe::busy()const{return d->busy;}
}
