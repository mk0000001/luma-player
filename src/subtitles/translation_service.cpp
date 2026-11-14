#include "translation_service.h"
#include "translation_support.h"
#include "core/private_directory.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <algorithm>
namespace luma {
struct TranslationService::Impl {
    struct WorkerIdentityFiles {
        QDir directory;
        ~WorkerIdentityFiles(){directory.removeRecursively();}
    };
    TranslationService *q;
    QString runtime,model,selected,log,token;
    AiPreferences preferences;
    TranslationGlossary glossary;
    TranslationProfile profile=TranslationProfile::LegacyQwenJson;
    TranslationQueue queue;
    QVector<SubtitleCue> contextCues;
    QHash<QString,SubtitleCue> targets;
    QHash<QString,QByteArray> fingerprints;
    QByteArray activeFingerprint;
    QVector<TranslationBackend> backends;
    int backendIndex=-1,startupMs=75000,requestMs=45000;
    quint64 launchSerial=0;
    quint16 port=0;
    std::shared_ptr<WorkerIdentityFiles> identityFiles;
    QSslCertificate workerCertificate;
    bool ready=false,pumpScheduled=false;
    QPointer<QProcess> process;
    QList<QPointer<QProcess>> retiringProcesses;
    QPointer<QNetworkReply> reply;
    QNetworkAccessManager network;
    QTimer poll,startupDeadline,requestDeadline;
    std::optional<TranslationTask> active;
    bool liveWindow=false;
    static constexpr qint64 maximumResponseBytes=262144;

    Impl(QString root,TranslationService *owner):q(owner),runtime(std::move(root)){
        if(runtime.isEmpty())runtime=QCoreApplication::applicationDirPath()+"/runtime";
        model=QDir(runtime).filePath("translation/models/Qwen3-4B-Q4_K_M.gguf");
        network.setProxy(QNetworkProxy::NoProxy);poll.setInterval(200);startupDeadline.setSingleShot(true);requestDeadline.setSingleShot(true);
        QObject::connect(&network,&QNetworkAccessManager::sslErrors,q,[this](QNetworkReply *response,const QList<QSslError> &errors){
            if(response!=reply || workerCertificate.isNull() || response->sslConfiguration().peerCertificate()!=workerCertificate)
                return;
            const bool privateTrustOnly=std::all_of(errors.begin(),errors.end(),[](const QSslError &error){
                return error.error()==QSslError::SelfSignedCertificate || error.error()==QSslError::CertificateUntrusted ||
                    error.error()==QSslError::UnableToGetLocalIssuerCertificate || error.error()==QSslError::UnableToVerifyFirstCertificate;
            });
            if(privateTrustOnly)
                response->ignoreSslErrors(errors);
        });
        QObject::connect(&network,&QNetworkAccessManager::encrypted,q,[this](QNetworkReply *response){
            if(response!=reply || workerCertificate.isNull() || response->sslConfiguration().peerCertificate()!=workerCertificate)
                response->abort();
        });
        QObject::connect(&poll,&QTimer::timeout,q,[this]{health();});
        QObject::connect(&startupDeadline,&QTimer::timeout,q,[this]{nextBackend("모델 시작 시간이 초과되었습니다.");});
        QObject::connect(&requestDeadline,&QTimer::timeout,q,[this]{
            if(!active)return;
            skipCue(*active,"로컬 번역 처리 시간이 초과되어 이 자막을 건너뜁니다.");
        });
    }
    ~Impl(){
        ++launchSerial;poll.stop();startupDeadline.stop();requestDeadline.stop();abortReply();
        const auto waitForStop=[this](QProcess *worker){
            if(!worker)return;
            QObject::disconnect(worker,nullptr,q,nullptr);
            if(worker->state()!=QProcess::NotRunning){worker->kill();worker->waitForFinished(2500);}
            delete worker;
        };
        waitForStop(process);
        for(const auto &worker:retiringProcesses)waitForStop(worker);
    }
    void abortReply(){if(reply){auto *old=reply.data();reply=nullptr;old->abort();old->deleteLater();}}
    void stop(){
        ++launchSerial;ready=false;poll.stop();startupDeadline.stop();requestDeadline.stop();abortReply();
        network.clearConnectionCache();workerCertificate=QSslCertificate();port=0;
        if(process){
            auto *old=process.data();process=nullptr;QObject::disconnect(old,nullptr,q,nullptr);
            if(old->state()==QProcess::NotRunning)old->deleteLater();
            else {
                // Keep the PEM files alive until a worker that is still starting has exited.
                auto retiredIdentity=std::move(identityFiles);
                retiringProcesses.erase(std::remove_if(retiringProcesses.begin(),retiringProcesses.end(),[](const auto &worker){return worker.isNull();}),retiringProcesses.end());
                retiringProcesses.append(old);
                QObject::connect(old,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),old,[old,retiredIdentity]{old->deleteLater();});
                old->kill();
            }
        }
        identityFiles.reset();
    }
    bool prepareIdentity(){
        QString helper="tools/lumaplayer-mobile-host";
#ifdef Q_OS_WIN
        helper+=".exe";
#endif
        auto executable=QDir(runtime).filePath(helper);
        if(!QFileInfo(executable).isFile())
            executable=QDir(QCoreApplication::applicationDirPath()+"/runtime").filePath(helper);
        QProcess generator;
        generator.start(executable,{"--translation-identity"},QIODevice::ReadOnly);
        if(!generator.waitForStarted(1000) || !generator.waitForFinished(2500)){
            generator.kill();generator.waitForFinished(1000);
            return false;
        }
        if(generator.exitStatus()!=QProcess::NormalExit || generator.exitCode()!=0)
            return false;
        const auto data=generator.readAllStandardOutput();
        if(data.size()>8192)
            return false;
        const auto identity=QJsonDocument::fromJson(data).object();
        const auto certificate=identity.value("certificate").toString().toLatin1();
        const auto key=identity.value("privateKey").toString().toLatin1();
        workerCertificate=QSslCertificate(certificate);
        if(workerCertificate.isNull() || key.isEmpty())
            return false;
        const auto dataDirectory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if(dataDirectory.isEmpty() || !QDir().mkpath(dataDirectory))
            return false;
        const auto directory=createPrivateDirectory(dataDirectory,"translation-");
        if(directory.isEmpty())
            return false;
        identityFiles=std::make_shared<WorkerIdentityFiles>(QDir(directory));
        for(const auto &[name,bytes]:{std::pair{"certificate.pem",certificate},std::pair{"key.pem",key}}){
            QFile file(identityFiles->directory.filePath(name));
            if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) ||
               !file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner) ||
               file.write(bytes)!=bytes.size())
                return false;
        }
        return true;
    }
    void schedule(){if(pumpScheduled)return;pumpScheduled=true;QTimer::singleShot(0,q,[this]{pumpScheduled=false;pump();});}
    void clear(){
        queue.cancel(true);active.reset();liveWindow=false;requestDeadline.stop();contextCues.clear();targets.clear();fingerprints.clear();activeFingerprint.clear();
        // A seek cancels caption work, not model initialization. Its health check is
        // independent of the source generation, so subsequent cues share the load.
        if(ready)abortReply();
    }
    void failAll(const QString &message){stop();clear();backends.clear();backendIndex=-1;selected.clear();q->failed(message);}
    void pruneTargets(){
        for(auto entry=targets.begin();entry!=targets.end();)if(!queue.retains(entry.key()))entry=targets.erase(entry);else ++entry;
        for(auto entry=fingerprints.begin();entry!=fingerprints.end();)if(!queue.cached(entry.key()))entry=fingerprints.erase(entry);else ++entry;
    }
    void publish(const TranslationTask &task,const QString &korean){
        const auto target=targets.constFind(task.source);
        if(target==targets.cend())q->translated(task.generation,task.source,korean);
        else q->translatedCue(task.generation,task.source,target->text,korean);
    }
    QJsonObject payloadFor(const TranslationTask &task,int promptByteBudget=-1)const{
        // Native launch uses 8192 tokens, or the legacy 512-token NPU profile.
        // A UTF-8 byte ceiling plus output/template reserves bounds token use.
        const bool npu=backendIndex>=0 && backendIndex<backends.size() && backends[backendIndex].key=="npu";
        const auto budget=promptByteBudget>=0?promptByteBudget:npu?std::max(0,512-(profile==TranslationProfile::HyMt2Plain?512:256)-128):6144;
        const auto target=targets.constFind(task.source);
        const bool cpu=backendIndex>=0 && backendIndex<backends.size() && backends[backendIndex].key=="cpu";
        // Prefilling many near-duplicate cues dominates short CPU translations.
        // Keep the nearest context during playback and the full window for preparation.
        const int contextCueLimit=liveWindow && cpu?4:24;
        return target==targets.cend()?translationPayload(SubtitleCue{0,0,task.source},{},profile,budget,glossary):translationPayload(*target,contextCues,profile,budget,glossary,contextCueLimit);
    }
    static QByteArray fingerprint(const QJsonObject &payload){return QCryptographicHash::hash(QJsonDocument(payload).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);}
    void skipCue(TranslationTask task,const QString &message){
        if(task.generation!=queue.generation() || !active || active->generation!=task.generation || active->source!=task.source)return;
        requestDeadline.stop();abortReply();queue.release(task);active.reset();
        const auto target=targets.constFind(task.source);
        if(target==targets.cend())q->skipped(task.generation,task.source,message);else q->skippedCue(task.generation,task.source,target->text,message);
        pruneTargets();schedule();
    }
    void updateLivePosition(qint64 position){
        liveWindow=true;
        if(!active)return;
        const auto target=targets.constFind(active->source);
        if(target==targets.cend() || (reply && ready && target->endMs>position))return;
        // Unstarted work is replaceable during model loading. For a running
        // request, expiration lets the next visible cue take over the slot.
        requestDeadline.stop();
        if(ready)abortReply();
        queue.release(*active);active.reset();activeFingerprint.clear();
    }
    void pump(){
        if(reply)return;
        if(!active)active=queue.take();if(!active)return;
        const auto task=*active;
        if(const auto cached=queue.cached(task.source)){
            const auto payload=targets.contains(task.source)?payloadFor(task):QJsonObject{};
            if(!targets.contains(task.source) || (!payload.isEmpty() && fingerprints.value(task.source)==fingerprint(payload))){
                queue.complete(task,*cached);active.reset();publish(task,*cached);pruneTargets();schedule();return;
            }
        }
        if(!QFileInfo::exists(model)){failAll("한국어 번역 모델이 없습니다. 선택 기능의 로컬 모델을 설치해 주세요.");return;}
        if(ready && workerCertificate.expiryDate()<=QDateTime::currentDateTimeUtc().addSecs(60)){
            --backendIndex;
            nextBackend();
            return;
        }
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
            if(!QFileInfo::exists(candidate.program)){
                if(!candidate.vulkanCompatibility)q->progress(candidate.device+" 번역 실행기가 없습니다.");
                continue;
            }
            if(!prepareIdentity()){failAll("로컬 번역 실행기의 보안 연결을 준비할 수 없습니다.");return;}
            token=QUuid::createUuid().toString(QUuid::Id128);
            auto command=translationLaunch(candidate,model,0,token);
            command.arguments.append({"--ssl-key-file",identityFiles->directory.filePath("key.pem"),
                                      "--ssl-cert-file",identityFiles->directory.filePath("certificate.pem")});
            log.clear();process=new QProcess(q);auto *worker=process.data();const auto serial=launchSerial;
            worker->setWorkingDirectory(QFileInfo(command.program).absolutePath());worker->setProcessEnvironment(command.environment);worker->setProcessChannelMode(QProcess::MergedChannels);
            QObject::connect(worker,&QProcess::readyRead,q,[this,worker,serial]{if(serial==launchSerial&&process==worker){
                log+=QString::fromUtf8(worker->readAll());if(log.size()>65536)log=log.right(65536);
                if(!port){
                    static const QRegularExpression address(R"(\blistening on https://127\.0\.0\.1:([0-9]{1,5})\r?\n)");
                    port=address.match(log).captured(1).toUShort();
                    if(port)health();
                }
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
            q->progress(candidate.device+(candidate.vulkanCompatibility?" 저VRAM 번역을 시도하는 중…":" 로컬 번역 모델을 불러오는 중…"));
            startupDeadline.start(startupMs);worker->start(command.program,command.arguments);return;
        }
        failAll("사용 가능한 로컬 한국어 번역 실행기가 없습니다. 번역 모델·런타임과 선택한 장치를 확인해 주세요.");
    }
    QNetworkRequest endpoint(const QString &path,int timeout){
        QNetworkRequest request(QUrl(QString("https://127.0.0.1:%1%2").arg(port).arg(path)));
        auto tls=QSslConfiguration::defaultConfiguration();
        tls.setCaCertificates({});
        tls.setPeerVerifyMode(QSslSocket::VerifyPeer);
        tls.setProtocol(QSsl::TlsV1_2OrLater);
        request.setSslConfiguration(tls);
        request.setRawHeader("Authorization","Bearer "+token.toUtf8());request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute,false);
        request.setTransferTimeout(timeout);return request;
    }
    void health(){
        if(ready||reply||!process||!port)return;
        const auto serial=launchSerial;auto *response=network.get(endpoint("/health",1000));reply=response;
        response->setReadBufferSize(maximumResponseBytes+1);
        QObject::connect(response,&QNetworkReply::readyRead,q,[this,response,serial]{
            if(serial==launchSerial && reply==response && response->bytesAvailable()>maximumResponseBytes)response->abort();
        });
        QObject::connect(response,&QNetworkReply::finished,q,[this,response,serial]{
            response->deleteLater();if(serial!=launchSerial||reply!=response)return;reply=nullptr;
            if(response->error()!=QNetworkReply::NoError||response->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200||response->bytesAvailable()>maximumResponseBytes)return;
            if(QJsonDocument::fromJson(response->readAll()).object().value("status").toString()!="ok")return;
            ready=true;poll.stop();startupDeadline.stop();schedule();
        });
    }
    void infer(const TranslationTask &task){
        // Build now: queued cues must see original captions that arrived while waiting.
        const auto payload=payloadFor(task);
        if(payload.isEmpty()){
            if(backends[backendIndex].key=="npu" && !payloadFor(task,6144).isEmpty()){
                nextBackend("현재 NPU 실행기의 입력 한도로 문맥 번역을 처리할 수 없어 다음 장치를 시도합니다.");return;
            }
            skipCue(task,"대상 자막이 현재 번역 모델의 입력 한도를 넘어 이 자막을 건너뜁니다.");return;
        }
        activeFingerprint=fingerprint(payload);
        const auto serial=launchSerial;auto *response=network.post(endpoint("/v1/chat/completions",requestMs),QJsonDocument(payload).toJson(QJsonDocument::Compact));reply=response;
        response->setReadBufferSize(maximumResponseBytes+1);
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
            // Offline preparation can wait for the exact current context.
            // Live playback accepts the context snapshot used at dispatch.
            if(!liveWindow && targets.contains(task.source) && fingerprint(payloadFor(task))!=activeFingerprint){
                queue.release(task);active.reset();activeFingerprint.clear();queue.enqueue({task.source});schedule();return;
            }
            const auto target=targets.constFind(task.source);const auto source=target==targets.cend()?task.source:target->text;
            QString error;const auto korean=parseTranslationResponse(body,&error,profile,source);
            if(korean.isEmpty()){skipCue(task,error);return;}
            if(selected.isEmpty()){
                if(process)log+=QString::fromUtf8(process->readAll());const auto actual=confirmedTranslationBackend(backends[backendIndex],log);
                if(actual.isEmpty()){nextBackend("실제 실행 장치를 확인하지 못했습니다.");return;}
                selected=actual;q->backendSelected(actual);
            }
            if(queue.complete(task,korean)){if(targets.contains(task.source))fingerprints.insert(task.source,activeFingerprint);active.reset();publish(task,korean);pruneTargets();}schedule();
        });
    }
};
TranslationService::TranslationService(QString runtime,QObject *parent):QObject(parent),d(std::make_unique<Impl>(std::move(runtime),this)){}
TranslationService::~TranslationService()=default;
void TranslationService::setPreferences(const AiPreferences &preferences){
    if(d->preferences.mode==preferences.mode&&d->preferences.gpuDevice==preferences.gpuDevice)return;
    d->clear();d->stop();d->preferences=preferences;d->backends.clear();d->backendIndex=-1;d->selected.clear();emit backendSelected({});
}
AiPreferences TranslationService::preferences()const{return d->preferences;}
void TranslationService::setModel(QString model){d->clear();d->stop();d->model=model.isEmpty()?QDir(d->runtime).filePath("translation/models/Qwen3-4B-Q4_K_M.gguf"):std::move(model);d->profile=translationProfileForModel(d->model);d->backends.clear();d->backendIndex=-1;d->selected.clear();emit backendSelected({});}
void TranslationService::setGlossary(const TranslationGlossary &glossary){
    if(d->glossary==glossary)return;
    d->clear();
    d->glossary=glossary;
}
TranslationGlossary TranslationService::glossary()const{return d->glossary;}
void TranslationService::request(const QStringList &sources){
    const auto generation=d->queue.generation();QStringList bounded;
    for(const auto &source:sources){
        if(source.size()>800){emit skipped(generation,source,"너무 긴 자막은 로컬 번역에서 제외했습니다 (최대 800자).");if(generation!=d->queue.generation())return;continue;}
        bounded.append(source);if(bounded.size()>=128)break;
    }
    d->queue.enqueue(bounded);d->schedule();
}
void TranslationService::setContextCues(const QVector<SubtitleCue> &originals){d->contextCues=originals;}
bool TranslationService::needsTranslation(const SubtitleCue &cue)const{
    const auto key=translationCueKey(cue);
    if(!d->targets.contains(key) || !d->queue.cached(key))return true;
    const auto payload=d->payloadFor({d->queue.generation(),key});
    return payload.isEmpty() || d->fingerprints.value(key)!=Impl::fingerprint(payload);
}
void TranslationService::requestCues(const QVector<SubtitleCue> &cues,std::optional<qint64> playbackPositionMs){
    if(playbackPositionMs)d->updateLivePosition(*playbackPositionMs);
    const auto generation=d->queue.generation();QStringList keys;
    for(const auto &cue:cues){
        if(playbackPositionMs && cue.endMs<=*playbackPositionMs)continue;
        const auto key=translationCueKey(cue);
        if(cue.startMs<0 || cue.endMs<=cue.startMs || cue.text.size()>800){
            emit skippedCue(generation,key,cue.text,"시간 정보가 잘못되었거나 너무 긴 자막은 로컬 번역에서 제외했습니다 (최대 800자).");
            if(generation!=d->queue.generation())return;continue;
        }
        if(cue.text.trimmed().isEmpty())continue;
        d->targets.insert(key,cue);keys.append(key);if(keys.size()>=128)break;
    }
    d->queue.enqueue(keys,!playbackPositionMs);d->pruneTargets();d->schedule();
}
void TranslationService::cancel(){d->clear();}
void TranslationService::warmUp(){
    if(!d->ready && !d->process && QFileInfo(d->model).isFile()){
        d->backendIndex=-1;
        d->nextBackend();
    }
}
void TranslationService::shutdown(){
    d->clear();
    d->stop();
    d->backendIndex=-1;
    if(!d->selected.isEmpty()){
        d->selected.clear();
        emit backendSelected({});
    }
}
quint64 TranslationService::generation()const{return d->queue.generation();}
QString TranslationService::selectedBackend()const{return d->selected;}
bool TranslationService::selectedVulkanCompatibility()const{
    return !d->selected.isEmpty() && d->backendIndex>=0 && d->backendIndex<d->backends.size()
        && d->backends[d->backendIndex].vulkanCompatibility;
}
bool TranslationService::busy()const{return d->queue.busy();}
void TranslationService::setTimeouts(int startupMs,int requestMs){d->startupMs=qMax(100,startupMs);d->requestMs=qMax(10,requestMs);}
}
