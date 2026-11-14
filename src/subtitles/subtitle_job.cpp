#include "subtitle_job.h"
#include "runtime_environment.h"
#include "pcm_buffer.h"
#include "recognition_timing.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QRegularExpression>
#include <QUrl>
#include <QSignalBlocker>
#include <QtEndian>
#include <algorithm>
namespace luma {
namespace {
QString executable(const QString &runtime,const QString &relative,const QString &systemName){
#ifdef Q_OS_WIN
    Q_UNUSED(systemName);return QDir(runtime).filePath(relative+".exe");
#else
    const auto bundled=QDir(runtime).filePath(relative);return QFileInfo::exists(bundled)?bundled:QStandardPaths::findExecutable(systemName);
#endif
}
QString srtTime(qint64 milliseconds){
    const auto ms=std::max<qint64>(0,milliseconds);return QString("%1:%2:%3,%4").arg(ms/3600000,2,10,QChar('0')).arg((ms/60000)%60,2,10,QChar('0')).arg((ms/1000)%60,2,10,QChar('0')).arg(ms%1000,3,10,QChar('0'));
}

QString extractionProtocols(const QString &scheme,bool disc){
    if(disc)return "bluray,file";
    if(scheme=="http" || scheme=="https")return "http,https,tcp,tls,crypto,httpproxy";
    if(scheme=="rtsp" || scheme=="rtsps")return "rtsp,rtsps,rtp,tcp,tls,udp";
    if(scheme=="rtmp" || scheme=="rtmps")return "rtmp,rtmps,tcp,tls";
    if(scheme=="srt" || scheme=="udp")return scheme;
    return "file,crypto";
}
}
ProcessInvocation extractionCommand(const QString &runtime,const RecognitionRequest &r,const QString &output){
    const auto start=QString::number(double(std::max<qint64>(0,r.startMs))/1000,'f',3);
    const auto duration=QString::number(double(std::clamp<qint64>(r.durationMs,1,60000))/1000,'f',3);
    const auto scheme=QUrl(r.source).scheme();
    QStringList args={"-hide_banner","-loglevel","error","-nostdin","-protocol_whitelist",extractionProtocols(scheme,r.disc)};
    if(!r.disc && (scheme=="http" || scheme=="https"))args.append({"-tls_verify","1"});
    QString source=r.source;
    if(r.disc){args.append("-playlist");args.append(QString::number(r.playlist.toUInt()));if(source.startsWith("bluray://")){QUrl url(source);url.setScheme("file");source=url.toLocalFile();}source="bluray:"+QDir::fromNativeSeparators(source);}
    const auto mapping=r.disc && r.audioTrackId>=0?"0:i:"+QString::number(r.audioTrackId):"0:a:"+QString::number(std::max(0,r.audioIndex));
    args.append({"-ss",start,"-i",source,"-t",duration,"-map",mapping,"-vn","-ac","1","-ar","16000","-c:a","pcm_s16le","-y",output});
    return {executable(runtime,"tools/ffmpeg","ffmpeg"),args};
}
ProcessInvocation recognitionCommand(const QString &runtime,const QString &model,const QString &language,const QString &wav,const QString &prefix){
    return {executable(runtime,"whisper/whisper-cli","whisper-cli"),{"-m",model,"-f",wav,"-l",language,"-osrt","-of",prefix,"-t",QString::number(std::clamp(QThread::idealThreadCount()/2,2,8)),"-ml","48","-bs","1","-bo","1","-np"}};
}
QString writeSrt(const QVector<SubtitleCue> &input){
    auto cues=input;std::stable_sort(cues.begin(),cues.end(),[](const auto &a,const auto &b){return a.startMs<b.startMs;});QString text;int i=1;
    for(const auto &cue:cues)if(cue.endMs>cue.startMs && !cue.text.isEmpty())text+=QString::number(i++)+'\n'+srtTime(cue.startMs)+" --> "+srtTime(cue.endMs)+'\n'+cue.text+"\n\n";
    return text;
}
SubtitleJob::SubtitleJob(QString runtime,QObject *parent):QObject(parent),runtime_(runtime.isEmpty()?QCoreApplication::applicationDirPath()+"/runtime":std::move(runtime)){
    pcmBuffer_=std::make_unique<PcmBuffer>(runtime_);
    connect(pcmBuffer_.get(),&PcmBuffer::ready,this,&SubtitleJob::audioPrepared);
    connect(pcmBuffer_.get(),&PcmBuffer::failed,this,[this](const QString &message){if(stage_==1){timeout_.stop();stage_=0;emit failed(message);}});
    process_.setProcessChannelMode(QProcess::MergedChannels);
    process_.setProcessEnvironment(localRuntimeEnvironment(runtime_));
    worker_.setProcessChannelMode(QProcess::MergedChannels);
    worker_.setProcessEnvironment(localRuntimeEnvironment(runtime_));
    connect(&worker_,&QProcess::readyReadStandardOutput,this,&SubtitleJob::workerOutput);
    connect(&worker_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){
        workerOutput();const auto backend=workerBackend_;const bool warming=workerWarming_;const auto epoch=warmupEpoch_;
        warmupTimeout_.stop();workerSignature_.clear();workerBusy_=false;workerWarming_=false;workerPrefix_.clear();retiredTemps_.clear();
        if(stage_==2){completed(code?code:23,status);return;}
        if(warming){rejected_.insert(backend);emit progress(backend+" 모델 준비 실패 · 다음 장치를 시도합니다.");if(!stage_)QTimer::singleShot(0,this,[this,epoch]{if(warmupEnabled_ && epoch==warmupEpoch_)warmUp(model_);});}
    });
    connect(&worker_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error!=QProcess::FailedToStart)return;const auto backend=workerBackend_;const auto epoch=warmupEpoch_;stopWorker();rejected_.insert(backend);if(stage_==2){timeout_.stop();nextRecognition("실행할 수 없음");}else if(!stage_)QTimer::singleShot(0,this,[this,epoch]{if(warmupEnabled_ && epoch==warmupEpoch_)warmUp(model_);});});
    warmupTimeout_.setSingleShot(true);warmupTimeout_.setInterval(60000);connect(&warmupTimeout_,&QTimer::timeout,this,[this]{
        const auto backend=workerBackend_;const auto epoch=warmupEpoch_;stopWorker();rejected_.insert(backend);emit progress(backend+" 모델 준비 시간 초과 · 다음 장치를 시도합니다.");if(epoch!=warmupEpoch_ || !warmupEnabled_)return;if(stage_==2)nextRecognition("모델 준비 시간 초과");else if(!stage_)warmUp(model_);
    });
    timeout_.setSingleShot(true);connect(&timeout_,&QTimer::timeout,this,[this]{
        if(!stage_)return;
        if(stage_==2 && backendIndex_>=0 && backends_[backendIndex_].key!="CPU"){
            stage_=0;process_.kill();process_.waitForFinished(1500);stopWorker();stage_=2;nextRecognition("시간 초과");
        }else{const auto phase=stage_==1?QString("음성 읽기"):QString("음성 인식");cancel();emit failed(phase+" 시간이 초과되었습니다. 다시 시도하세요.");}
    });
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{lastOutput_+=QString::fromUtf8(process_.readAllStandardOutput());if(lastOutput_.size()>16000)lastOutput_=lastOutput_.right(16000);});
    connect(&process_,&QProcess::finished,this,&SubtitleJob::completed);
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(stage_ && error==QProcess::FailedToStart){timeout_.stop();if(stage_==2)nextRecognition("실행할 수 없음");else{stage_=0;emit failed("AI 도구를 실행할 수 없습니다: "+process_.errorString());}}});
}
SubtitleJob::~SubtitleJob(){shutdown();}
bool SubtitleJob::busy()const{return stage_!=0;}
QString SubtitleJob::currentPhase()const{
    if(stage_==1)return "reading";
    if(workerWarming_ || !queuedWorkerRequest_.isEmpty())return "loading";
    return stage_==2?"recognizing":"idle";
}
void SubtitleJob::stopWorker(){
    warmupTimeout_.stop();QSignalBlocker blocked(worker_);workerSignature_.clear();queuedWorkerRequest_.clear();workerBuffer_.clear();workerPrefix_.clear();workerBusy_=false;workerWarming_=false;
    if(worker_.state()!=QProcess::NotRunning){worker_.kill();worker_.waitForFinished(1500);}retiredTemps_.clear();
}
void SubtitleJob::cancel(bool keepDiscAudio){
    timeout_.stop();stage_=0;++generation_;queuedWorkerRequest_.clear();
    if(keepDiscAudio)pcmBuffer_->cancelRequest();else pcmBuffer_->cancel();
    if(process_.state()!=QProcess::NotRunning){process_.kill();process_.waitForFinished(1500);}
    if(workerBusy_ && !workerPrefix_.isEmpty()){QFile marker(workerPrefix_+".cancel");if(marker.open(QIODevice::WriteOnly))marker.close();}
    // Only the in-flight request needs its WAV/output directory retained.
    if(workerBusy_ && temp_ && workerPrefix_.startsWith(temp_->path()+'/'))retiredTemps_.push_back(std::move(temp_));else temp_.reset();
}
void SubtitleJob::shutdown(){warmupEnabled_=false;++warmupEpoch_;cancel();stopWorker();}
void SubtitleJob::workerOutput(){
    workerBuffer_+=worker_.readAllStandardOutput();if(workerBuffer_.size()>65536)workerBuffer_=workerBuffer_.right(65536);
    qsizetype end=0;while((end=workerBuffer_.indexOf('\n'))>=0){
        auto line=QString::fromUtf8(workerBuffer_.left(end));workerBuffer_.remove(0,end+1);if(line.endsWith('\r'))line.chop(1);
        if(line.startsWith("LUMA_WORKING=") && stage_==2 && line.mid(13)==resultPrefix_){emit recognitionStarted(generation_);}
        else if(line=="LUMA_READY"){
            workerBusy_=false;workerWarming_=false;warmupTimeout_.stop();emit progress(workerBackend_+" 모델 준비됨 · 음성을 기다리는 중…");emit warmedUp();sendQueuedRequest();
        }else if(line.startsWith("LUMA_DONE=") || line.startsWith("LUMA_ABORTED=")){
            const bool aborted=line.startsWith("LUMA_ABORTED=");const bool current=stage_==2 && line.mid(aborted?13:10)==resultPrefix_;workerBusy_=false;retiredTemps_.clear();
            if(current && !aborted)completed(0,QProcess::NormalExit);else if(current){timeout_.stop();stage_=0;emit failed("음성 인식이 취소되었습니다. 다시 시도하세요.");}else sendQueuedRequest();
        }else if(stage_==2 && workerPrefix_==resultPrefix_){lastOutput_+=line+'\n';if(lastOutput_.size()>32000)lastOutput_=lastOutput_.right(32000);}
    }
}
void SubtitleJob::sendQueuedRequest(){
    if(stage_!=2 || workerBusy_ || queuedWorkerRequest_.isEmpty() || worker_.state()!=QProcess::Running)return;
    lastOutput_.clear();workerPrefix_=resultPrefix_;workerBusy_=true;worker_.write(queuedWorkerRequest_.toUtf8());queuedWorkerRequest_.clear();
}
void SubtitleJob::warmUp(const QString &model){
    if(busy() || preferences_.mode=="cpu")return;
    const auto selected=model.isEmpty()?QDir(runtime_).filePath("models/ggml-base.bin"):model;if(!QFileInfo::exists(selected))return;
    if(rejectionModel_!=selected){rejected_.clear();rejectionModel_=selected;++warmupEpoch_;}model_=selected;warmupEnabled_=true;
    const auto candidates=recognitionBackends(runtime_,model_,preferences_);
    for(const auto &backend:candidates){
        if(backend.key=="CPU" || rejected_.contains(backend.key))continue;
        const auto signature=backend.program+'\n'+model_+'\n'+backend.device+'\n'+backend.encoder;
        if(worker_.state()!=QProcess::NotRunning && workerSignature_==signature)return;
        stopWorker();workerSignature_=signature;workerBackend_=backend.key;workerBusy_=true;workerWarming_=true;
        auto command=recognitionCommand(runtime_,model_,"ja","unused.wav","unused");command.program=backend.program;
        const auto cache=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/openvino";QDir().mkpath(cache);
        command.arguments.append({"--device",backend.device,"--cache",cache,"--server","--warmup"});if(!backend.encoder.isEmpty())command.arguments.append({"--encoder",backend.encoder});
        const auto epoch=warmupEpoch_;emit progress(backend.key+" 모델을 미리 준비하는 중…");if(!warmupEnabled_ || epoch!=warmupEpoch_)return;worker_.setProgram(command.program);worker_.setArguments(command.arguments);warmupTimeout_.start();worker_.start();return;
    }
}
void SubtitleJob::setStageTimeouts(int extractionMs,int recognitionMs){extractionTimeoutMs_=std::clamp(extractionMs,1,900000);recognitionTimeoutMs_=std::clamp(recognitionMs,1,900000);}
void SubtitleJob::setAcceleratorTimeout(int milliseconds){acceleratorTimeoutMs_=std::clamp(milliseconds,1,900000);}
void SubtitleJob::setPreferences(const AiPreferences &preferences){
    if(preferences_.mode==preferences.mode && preferences_.gpuDevice==preferences.gpuDevice)return;
    shutdown();preferences_=preferences;rejected_.clear();selectedBackend_.clear();
    selectedAccelerator_.clear();selectedDevice_.clear();
}
bool SubtitleJob::begin(const RecognitionRequest &request,const QString &model){
    if(busy())cancel();else{++generation_;temp_.reset();}
    request_=request;request_.startMs=std::max<qint64>(0,request_.startMs);request_.durationMs=std::clamp<qint64>(request_.durationMs,1,60000);model_=model.isEmpty()?QDir(runtime_).filePath("models/ggml-base.bin"):model;
    if(request_.disc && !QRegularExpression("^[0-9]{5}$").match(request_.playlist).hasMatch()){emit failed("선택된 디스크 타이틀의 플레이리스트를 확인할 수 없습니다.");return false;}
    if(!QFileInfo::exists(model_)){emit failed("음성인식 모델을 찾을 수 없습니다. Whisper 모델을 선택하세요.");return false;}
    if(rejectionModel_!=model_){rejected_.clear();rejectionModel_=model_;++warmupEpoch_;}warmupEnabled_=true;
    backends_=recognitionBackends(runtime_,model_,preferences_);backendIndex_=-1;
    temp_=std::make_unique<QTemporaryDir>(QDir::tempPath()+"/luma-recognition-XXXXXX");
    if(!temp_->isValid()){emit failed("AI 자막 임시 폴더를 만들 수 없습니다.");return false;}
    return true;
}
quint64 SubtitleJob::start(const RecognitionRequest &request,const QString &model){
    if(!begin(request,model))return generation_;
    stage_=1;emit progress(request_.disc?"디스크 음성을 앞서 읽는 중…":"음성 구간을 준비하는 중…");
    if(request_.disc){timeout_.start(extractionTimeoutMs_);pcmBuffer_->prepare(request_,temp_->filePath("audio.wav"));}
    else run(extractionCommand(runtime_,request_,temp_->filePath("audio.wav")));
    return generation_;
}
quint64 SubtitleJob::startPcm(qint64 positionMs,const QByteArray &pcm,const QString &language,const QString &model){
    if(pcm.isEmpty() || pcm.size()%32 || pcm.size()>15'000*32)return generation_;
    RecognitionRequest request;
    request.startMs=positionMs;
    request.durationMs=pcm.size()/32;
    request.language=language;
    if(!begin(request,model))return generation_;
    if(!writeRecognitionWave(temp_->filePath("audio.wav"),pcm)){
        emit failed("송출 음성을 음성인식에 전달하지 못했습니다.");
        return generation_;
    }
    stage_=1;
    audioPrepared();
    return generation_;
}
void SubtitleJob::run(const ProcessInvocation &command){lastOutput_.clear();process_.setProgram(command.program);process_.setArguments(command.arguments);timeout_.start(stage_==1?extractionTimeoutMs_:backends_[backendIndex_].key=="CPU"?recognitionTimeoutMs_:acceleratorTimeoutMs_);process_.start();}
void SubtitleJob::nextRecognition(const QString &reason){
    if(!reason.isEmpty() && backendIndex_>=0){
        const auto previous=backends_[backendIndex_].key;if(previous!="CPU")rejected_.insert(previous);
        const auto generation=generation_;emit progress(previous+" 사용 불가 ("+reason+") · 다음 장치로 전환 중…");if(generation_!=generation || stage_!=2)return;
    }
    do{++backendIndex_;}while(backendIndex_<backends_.size() && rejected_.contains(backends_[backendIndex_].key));
    if(backendIndex_>=backends_.size()){stage_=0;emit failed("사용 가능한 로컬 음성인식 장치를 찾지 못했습니다.");return;}
    const auto &backend=backends_[backendIndex_];resultPrefix_=temp_->filePath("result-"+QString::number(backendIndex_)+"-"+backend.key);
    auto command=recognitionCommand(runtime_,model_,request_.language,temp_->filePath("audio.wav"),resultPrefix_);command.program=backend.program;
    if(backend.key=="CPU" && backend.device.isEmpty())command.arguments.append("-ng");
    else if(!backend.device.isEmpty()){
        const auto cache=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/openvino";QDir().mkpath(cache);
        command.arguments.append({"--device",backend.device,"--cache",cache});
        if(!backend.encoder.isEmpty())command.arguments.append({"--encoder",backend.encoder});
    }
    emit progress(backend.key+"에서 로컬 AI 자막 생성 중…");
    if(backend.key=="CPU"){stage_=0;stopWorker();stage_=2;run(command);return;}
    const auto signature=backend.program+'\n'+model_+'\n'+backend.device+'\n'+backend.encoder;
    lastOutput_.clear();timeout_.start(acceleratorTimeoutMs_);
    if(worker_.state()!=QProcess::NotRunning && workerSignature_==signature){
        queuedWorkerRequest_=temp_->filePath("audio.wav")+'\t'+resultPrefix_+'\t'+request_.language+'\t'+QString::number(std::clamp(QThread::idealThreadCount()/2,2,8))+'\n';sendQueuedRequest();
    }else{
        stage_=0;stopWorker();stage_=2;workerSignature_=signature;
        workerBackend_=backend.key;workerPrefix_=resultPrefix_;workerBusy_=true;command.arguments.append("--server");worker_.setProgram(command.program);worker_.setArguments(command.arguments);worker_.start();
    }
}
void SubtitleJob::audioPrepared(){
    if(stage_!=1)return;timeout_.stop();
    QFile wave(temp_->filePath("audio.wav"));if(wave.open(QIODevice::ReadOnly) && wave.size()<=4*1024*1024){
        const auto data=wave.readAll();if(data.size()>=44 && data.startsWith("RIFF") && data.mid(8,4)=="WAVE"){
            for(qsizetype offset=12;offset+8<=data.size();){const auto size=qFromLittleEndian<quint32>(data.constData()+offset+4);const auto start=offset+8;if(size>quint64(data.size()-start))break;
                if(data.mid(offset,4)=="data" && size && std::all_of(data.cbegin()+start,data.cbegin()+start+size,[](char sample){return sample==0;})){stage_=0;emit progress("무음 구간 · 자막 버퍼 준비됨");emit ready(generation_,{});return;}offset=start+size+(size&1);
            }
        }
    }
    stage_=2;nextRecognition();
}
void SubtitleJob::completed(int code,QProcess::ExitStatus status){
    if(!stage_)return;
    timeout_.stop();
    if(process_.isOpen())lastOutput_+=QString::fromUtf8(process_.readAllStandardOutput());
    if(code!=0 || status!=QProcess::NormalExit){if(stage_==2){nextRecognition("처리 실패");return;}stage_=0;emit failed("AI 자막 처리 실패: "+lastOutput_.right(600));return;}
    if(stage_==1){
        if(QFileInfo(temp_->filePath("audio.wav")).size()<=44){stage_=0;emit failed("이 구간의 음성을 읽을 수 없습니다. 본편 타이틀에서 다시 시도하세요.");return;}
        audioPrepared();return;
    }
    if(!confirmsBackend(backends_[backendIndex_],lastOutput_)){nextRecognition("가속 실행 확인 실패");return;}
    QFile file(resultPrefix_+".srt");if(!file.open(QIODevice::ReadOnly) || file.size()>8*1024*1024){nextRecognition("자막 결과 없음");return;}
    auto cues=parseSrt(QString::fromUtf8(file.readAll()));for(auto &cue:cues){cue.startMs+=request_.startMs;cue.endMs=std::min(cue.endMs+request_.startMs,request_.startMs+request_.durationMs);}
    selectedAlignment_="segment";QFile timing(resultPrefix_+".timings.json");
    if(timing.open(QIODevice::ReadOnly) && timing.size()<=1024*1024){
        if(auto aligned=parseRecognitionTiming(timing.readAll(),request_.startMs,request_.durationMs)){
            cues=std::move(aligned->cues);selectedAlignment_=aligned->alignment;
        }
    }
    const auto &backend=backends_[backendIndex_];
    selectedBackend_=backend.key;
    if(backend.key=="CPU"){
        selectedAccelerator_=backend.device.isEmpty()?"whisper-cli":"whisper-worker";
        selectedDevice_="CPU";
    }else{
        static const QRegularExpression accelerator(R"((?:\A|\n)LUMA_ACCELERATOR=(Vulkan GPU|CUDA GPU|OpenVINO encoder)\r?(?=\n|\z))");
        static const QRegularExpression device(R"((?:\A|\n)LUMA_DEVICE=((?:OpenVINO:)?(?:GPU|NPU)(?:\.[0-9]{1,3})?|(?:Vulkan|CUDA)[0-9]{1,3})\r?(?=\n|\z))");
        const auto confirmed=accelerator.match(lastOutput_).captured(1);
        selectedAccelerator_=confirmed=="Vulkan GPU"?QString("vulkan"):confirmed=="CUDA GPU"?QString("cuda"):
                             confirmed=="OpenVINO encoder"?QString("openvino"):QString{};
        selectedDevice_=device.match(lastOutput_).captured(1);
    }
    const auto completedGeneration=generation_;stage_=0;emit backendSelected(selectedBackend_);emit ready(completedGeneration,cues);
}
}
