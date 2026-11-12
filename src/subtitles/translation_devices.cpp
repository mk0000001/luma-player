#include "translation_devices.h"
#include "translation_support.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <algorithm>
namespace luma {
QJsonArray parseTranslationDevices(const QByteArray &output,const QString &backend){
    if(output.size()>65536 || (backend!="CUDA" && backend!="Vulkan" && backend!="OpenVINO"))return {};
    QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);const QString text=decoder(output);if(decoder.hasError())return {};
    static const QRegularExpression row(R"(^\s*((?:CUDA|Vulkan|(?i:OpenVINO))\d+): (.+) \((\d+) MiB, (\d+) MiB free\)\s*$)");
    static const QRegularExpression openvino(R"(^GGML_OPENVINO_DEVICE=((?:GPU|NPU)(?:\.\d+)?)(?: \(selected\))? - (.+)$)");
    QJsonArray devices;QSet<QString> seen;bool inventory=false;
    for(const auto &line:text.split('\n')){
        if(line.trimmed()=="Available devices:"){inventory=true;continue;}
        if(!inventory)continue;const auto match=row.match(line);if(!match.hasMatch() || !match.captured(1).startsWith(backend,Qt::CaseInsensitive))continue;
        auto id=match.captured(1),name=match.captured(2);QString type="gpu";
        if(backend=="OpenVINO"){
            const auto device=openvino.match(name);if(!device.hasMatch())continue;
            id="OpenVINO:"+device.captured(1);name=device.captured(2);if(device.captured(1).startsWith("NPU"))type="npu";
        }
        if(seen.contains(id))continue;bool totalOk=false,freeOk=false;
        const auto total=match.captured(3).toLongLong(&totalOk),free=match.captured(4).toLongLong(&freeOk);if(!totalOk || !freeOk || free>total)continue;
        seen.insert(id);devices.append(QJsonObject{{"id",id},{"name",name},{"backend",backend},{"type",type},{"memoryMiB",total},{"freeMiB",free}});
    }
    return devices;
}
TranslationDeviceProbe::TranslationDeviceProbe(QString runtime,QObject *parent):QObject(parent),runtime_(std::move(runtime)){
    if(runtime_.isEmpty())runtime_=QCoreApplication::applicationDirPath()+"/runtime";
    timer_.setSingleShot(true);timer_.setInterval(10000);process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_,&QProcess::readyRead,this,&TranslationDeviceProbe::readOutput);
    connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&TranslationDeviceProbe::finished);
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(active_ && error==QProcess::FailedToStart){const auto generation=generation_;timer_.stop();emit failed(candidates_[index_].backend+" 번역 기기 검색 실행기를 시작하지 못했습니다.");if(generation==generation_)scheduleNext();}
    });
    connect(&timer_,&QTimer::timeout,this,[this]{if(active_){timedOut_=true;process_.kill();}});
}
TranslationDeviceProbe::~TranslationDeviceProbe(){cancel();process_.blockSignals(true);if(process_.state()!=QProcess::NotRunning)process_.waitForFinished(1500);}
void TranslationDeviceProbe::setTimeout(int milliseconds){timer_.setInterval(std::clamp(milliseconds,50,30000));}
void TranslationDeviceProbe::start(){
    if(active_ || process_.state()!=QProcess::NotRunning)return;
    active_=true;++generation_;index_=-1;devices_={};candidates_.clear();
#ifdef Q_OS_WIN
    const QString suffix=".exe";
#else
    const QString suffix;
#endif
    for(const auto &backend:{QString("CUDA"),QString("Vulkan"),QString("OpenVINO")}){
        const auto program=QDir(runtime_).filePath("translation/"+backend.toLower()+"/llama-server"+suffix);
        if(QFileInfo::exists(program))candidates_.append({backend,program});
    }
    scheduleNext();
}
void TranslationDeviceProbe::cancel(){active_=false;++generation_;timer_.stop();if(process_.state()!=QProcess::NotRunning)process_.kill();}
void TranslationDeviceProbe::scheduleNext(){const auto generation=generation_;QTimer::singleShot(0,this,[this,generation]{if(generation==generation_)next();});}
void TranslationDeviceProbe::next(){
    if(!active_)return;
    if(++index_>=candidates_.size()){active_=false;emit devicesReady(devices_);return;}
    const auto &candidate=candidates_[index_];output_.clear();timedOut_=false;oversized_=false;
    const auto launch=translationLaunch({candidate.backend=="CUDA"?QString("cuda"):QString("gpu"),candidate.program,{},false},{},0,{});
    process_.setWorkingDirectory(QFileInfo(candidate.program).absolutePath());process_.setProcessEnvironment(launch.environment);
    process_.setProgram(candidate.program);process_.setArguments({"--list-devices"});timer_.start();process_.start();
}
void TranslationDeviceProbe::readOutput(){
    const auto bytes=process_.readAll();if(!active_)return;
    if(output_.size()+bytes.size()>65536){oversized_=true;process_.kill();return;}output_+=bytes;
}
void TranslationDeviceProbe::finished(int code,QProcess::ExitStatus status){
    if(!active_)return;const auto generation=generation_;timer_.stop();readOutput();const auto &candidate=candidates_[index_];
    if(timedOut_ || oversized_ || code!=0 || status!=QProcess::NormalExit || !output_.contains("Available devices:")){
        emit failed(candidate.backend+(timedOut_?" 번역 기기 검색 시간이 초과되었습니다.":oversized_?" 번역 기기 검색 결과가 너무 큽니다.":" 번역 기기 목록을 확인하지 못했습니다."));
    }else for(const auto &device:parseTranslationDevices(output_,candidate.backend))devices_.append(device);
    if(generation==generation_)scheduleNext();
}
}
