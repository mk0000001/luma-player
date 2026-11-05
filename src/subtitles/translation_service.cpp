#include "translation_service.h"
#include "translation_support.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QTcpServer>
#include <QTimer>
#include <QUuid>
namespace luma {
struct TranslationService::Impl {
    TranslationService *q;
    QString runtime,model,selected,log,token;
    AiPreferences preferences;
    TranslationQueue queue;
    QVector<TranslationBackend> backends;
    int backendIndex=-1,startupMs=75000,requestMs=45000;
    quint64 launchSerial=0;
    quint16 port=0;
    bool ready=false,pumpScheduled=false;
    QPointer<QProcess> process;
    QPointer<QNetworkReply> reply;
    QNetworkAccessManager network;
    QTimer poll,startupDeadline,requestDeadline;
    std::optional<TranslationTask> active;
    static constexpr qint64 maximumResponseBytes=262144;

    Impl(QString root,TranslationService *owner):q(owner),runtime(std::move(root)){
        if(runtime.isEmpty())runtime=QCoreApplication::applicationDirPath()+"/runtime";
        model=QDir(runtime).filePath("translation/models/Qwen3-4B-Q4_K_M.gguf");
        network.setProxy(QNetworkProxy::NoProxy);poll.setInterval(200);startupDeadline.setSingleShot(true);requestDeadline.setSingleShot(true);
        QObject::connect(&poll,&QTimer::timeout,q,[this]{health();});
        QObject::connect(&startupDeadline,&QTimer::timeout,q,[this]{nextBackend("모델 시작 시간이 초과되었습니다.");});
        QObject::connect(&requestDeadline,&QTimer::timeout,q,[this]{
            if(!active)return;
            skipCue(*active,"로컬 번역 처리 시간이 초과되어 이 자막을 건너뜁니다.");
        });
    }
    ~Impl(){
        ++launchSerial;poll.stop();startupDeadline.stop();requestDeadline.stop();abortReply();
        if(process){QObject::disconnect(process,nullptr,q,nullptr);process->kill();process->waitForFinished(1000);delete process.data();}
    }
    void abortReply(){if(reply){auto *old=reply.data();reply=nullptr;old->abort();old->deleteLater();}}
    void stop(){
        ++launchSerial;ready=false;poll.stop();startupDeadline.stop();requestDeadline.stop();abortReply();
        if(process){
            auto *old=process.data();process=nullptr;QObject::disconnect(old,nullptr,q,nullptr);
            if(old->state()==QProcess::NotRunning)old->deleteLater();
            else {QObject::connect(old,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),old,&QObject::deleteLater);old->kill();}
        }
    }
    void schedule(){if(pumpScheduled)return;pumpScheduled=true;QTimer::singleShot(0,q,[this]{pumpScheduled=false;pump();});}
    void clear(const bool cache){
        queue.cancel(cache);active.reset();requestDeadline.stop();
        // A seek cancels caption work, not model initialization. Its health check is
        // independent of the source generation, so subsequent cues share the load.
        if(ready)abortReply();
    }
    void failAll(const QString &message){stop();queue.cancel();active.reset();backends.clear();backendIndex=-1;selected.clear();q->failed(message);}
    void skipCue(TranslationTask task,const QString &message){
        if(task.generation!=queue.generation() || !active || active->generation!=task.generation || active->source!=task.source)return;
        requestDeadline.stop();abortReply();queue.release(task);active.reset();
        q->skipped(task.generation,task.source,message);schedule();
    }
    void pump(){
        if(reply)return;
        if(!active)active=queue.take();if(!active)return;
        const auto task=*active;
        if(const auto cached=queue.cached(task.source)){
            queue.complete(task,*cached);active.reset();q->translated(task.generation,task.source,*cached);schedule();return;
        }
        if(!QFileInfo::exists(model)){failAll("한국어 번역 모델이 없습니다. 선택 기능의 로컬 모델을 설치해 주세요.");return;}
        if(ready){infer(task);return;}
        if(process)return;
        nextBackend();
    }
    void nextBackend(const QString &reason={}){
        const QString previous=backendIndex>=0&&backendIndex<backends.size()?backends[backendIndex].device:QString();
        stop();if(!selected.isEmpty()){selected.clear();q->backendSelected({});}if(!reason.isEmpty())q->progress(previous+" 번역 사용 불가: "+reason);
        if(backends.isEmpty())backends=translationBackends(runtime,preferences);
        while(++backendIndex<backends.size()){
            const auto &candidate=backends[backendIndex];
            if(!QFileInfo::exists(candidate.program)){q->progress(candidate.device+" 번역 실행기가 없습니다.");continue;}
            QTcpServer reservation;if(!reservation.listen(QHostAddress::LocalHost,0)){failAll("로컬 번역 포트를 열 수 없습니다.");return;}
            port=reservation.serverPort();reservation.close();token=QUuid::createUuid().toString(QUuid::Id128);
            const auto command=translationLaunch(candidate,model,port,token);
            log.clear();process=new QProcess(q);auto *worker=process.data();const auto serial=launchSerial;
            worker->setWorkingDirectory(QFileInfo(command.program).absolutePath());worker->setProcessEnvironment(command.environment);worker->setProcessChannelMode(QProcess::MergedChannels);
            QObject::connect(worker,&QProcess::readyRead,q,[this,worker,serial]{if(serial==launchSerial&&process==worker){
                log+=QString::fromUtf8(worker->readAll());if(log.size()>65536)log=log.right(65536);
                if(log.contains("NPU_COMPILER_DYNAMIC_QUANTIZATION"))nextBackend("현재 NPU 드라이버가 모델 컴파일 옵션을 지원하지 않습니다.");
                else if(log.contains("GGML OpenVINO backend ov::Exception"))nextBackend("OpenVINO 모델 컴파일 또는 추론에 실패했습니다.");
            }});
            QObject::connect(worker,&QProcess::started,q,[this,serial]{if(serial==launchSerial){poll.start();health();}});
            QObject::connect(worker,&QProcess::errorOccurred,q,[this,serial](QProcess::ProcessError error){if(serial==launchSerial&&error==QProcess::FailedToStart)nextBackend("네이티브 번역 실행기를 시작하지 못했습니다.");});
            QObject::connect(worker,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),q,[this,worker,serial](int code,QProcess::ExitStatus){
                if(serial!=launchSerial||process!=worker)return;
                log+=QString::fromUtf8(worker->readAll());const auto detail=log.contains("NPU_COMPILER_DYNAMIC_QUANTIZATION")?QString("현재 NPU 드라이버가 모델 컴파일 옵션을 지원하지 않습니다."):QString("실행기가 종료되었습니다 (코드 %1).").arg(code);
                nextBackend(detail);
            });
            q->progress(candidate.device+" 로컬 번역 모델을 불러오는 중…");startupDeadline.start(startupMs);worker->start(command.program,command.arguments);return;
        }
        failAll("사용 가능한 로컬 한국어 번역 실행기가 없습니다. 번역 모델·런타임과 선택한 장치를 확인해 주세요.");
    }
    QNetworkRequest endpoint(const QString &path,int timeout){
        QNetworkRequest request(QUrl(QString("http://127.0.0.1:%1%2").arg(port).arg(path)));request.setRawHeader("Authorization","Bearer "+token.toUtf8());request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);request.setTransferTimeout(timeout);return request;
    }
    void health(){
        if(ready||reply||!process)return;
        const auto serial=launchSerial;auto *response=network.get(endpoint("/health",1000));reply=response;
        QObject::connect(response,&QNetworkReply::finished,q,[this,response,serial]{
            response->deleteLater();if(serial!=launchSerial||reply!=response)return;reply=nullptr;
            if(response->error()!=QNetworkReply::NoError||response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200)return;
            if(QJsonDocument::fromJson(response->readAll()).object().value("status").toString()!="ok")return;
            ready=true;poll.stop();startupDeadline.stop();schedule();
        });
    }
    void infer(const TranslationTask &task){
        const auto serial=launchSerial;auto *response=network.post(endpoint("/v1/chat/completions",requestMs),QJsonDocument(translationPayload(task.source)).toJson(QJsonDocument::Compact));reply=response;
        requestDeadline.start(requestMs);
        QObject::connect(response,&QNetworkReply::readyRead,q,[this,response,serial,task]{
            if(serial==launchSerial && reply==response && response->bytesAvailable()>maximumResponseBytes)skipCue(task,"번역 응답이 너무 커서 이 자막을 건너뜁니다.");
        });
        QObject::connect(response,&QNetworkReply::finished,q,[this,response,serial,task]{
            response->deleteLater();if(serial!=launchSerial||reply!=response||task.generation!=queue.generation())return;reply=nullptr;requestDeadline.stop();
            const auto networkError=response->error();const int status=response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if(networkError==QNetworkReply::TimeoutError || networkError==QNetworkReply::OperationCanceledError || networkError==QNetworkReply::RemoteHostClosedError){skipCue(task,"번역 응답이 중단되어 이 자막을 건너뜁니다.");return;}
            if(status==400 || status==413 || status==422){skipCue(task,"번역 실행기가 이 자막 요청을 처리하지 못해 건너뜁니다.");return;}
            if(networkError!=QNetworkReply::NoError || status!=200){nextBackend("로컬 번역 추론 요청이 실패했습니다.");return;}
            const auto body=response->readAll();if(body.size()>maximumResponseBytes){skipCue(task,"번역 응답이 너무 커서 이 자막을 건너뜁니다.");return;}
            QString error;const auto korean=parseTranslationResponse(body,&error);
            if(korean.isEmpty()){skipCue(task,error);return;}
            if(selected.isEmpty()){
                if(process)log+=QString::fromUtf8(process->readAll());const auto actual=confirmedTranslationBackend(backends[backendIndex],log);
                if(actual.isEmpty()){nextBackend("실제 실행 장치를 확인하지 못했습니다.");return;}
                selected=actual;q->backendSelected(actual);
            }
            if(queue.complete(task,korean)){active.reset();q->translated(task.generation,task.source,korean);}schedule();
        });
    }
};
TranslationService::TranslationService(QString runtime,QObject *parent):QObject(parent),d(std::make_unique<Impl>(std::move(runtime),this)){}
TranslationService::~TranslationService()=default;
void TranslationService::setPreferences(const AiPreferences &preferences){
    if(d->preferences.mode==preferences.mode&&d->preferences.gpuDevice==preferences.gpuDevice)return;
    d->clear(false);d->stop();d->preferences=preferences;d->backends.clear();d->backendIndex=-1;d->selected.clear();emit backendSelected({});
}
AiPreferences TranslationService::preferences()const{return d->preferences;}
void TranslationService::setModel(QString model){d->clear(true);d->stop();d->model=model.isEmpty()?QDir(d->runtime).filePath("translation/models/Qwen3-4B-Q4_K_M.gguf"):std::move(model);d->backends.clear();d->backendIndex=-1;d->selected.clear();emit backendSelected({});}
void TranslationService::request(const QStringList &sources){
    const auto generation=d->queue.generation();QStringList bounded;
    for(const auto &source:sources){
        if(source.size()>800){emit skipped(generation,source,"너무 긴 자막은 로컬 번역에서 제외했습니다 (최대 800자).");if(generation!=d->queue.generation())return;continue;}
        bounded.append(source);if(bounded.size()>=128)break;
    }
    d->queue.enqueue(bounded);d->schedule();
}
void TranslationService::cancel(){d->clear(false);}
quint64 TranslationService::generation()const{return d->queue.generation();}
QString TranslationService::selectedBackend()const{return d->selected;}
bool TranslationService::busy()const{return d->queue.busy();}
void TranslationService::setTimeouts(int startupMs,int requestMs){d->startupMs=qMax(100,startupMs);d->requestMs=qMax(10,requestMs);}
}
