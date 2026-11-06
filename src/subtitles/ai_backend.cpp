#include "ai_backend.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSettings>
#include <QDateTime>
#include <QSet>
#include <cmath>
#include <optional>
namespace luma {
namespace {
bool encoderCandidate(const QFileInfo &model,const QString &stem){
    const QFileInfo xml(stem+".xml"),bin(stem+".bin");
    if(!xml.isFile() || !bin.isFile() || xml.size()<=0 || bin.size()<=0)return false;
    QFile metadata(stem+".json");
    // The original bundled base encoder predates the native exporter manifest.
    if(!metadata.exists())return model.completeBaseName()=="ggml-base";
    if(metadata.size()>65536 || !metadata.open(QIODevice::ReadOnly))return false;
    QJsonParseError error;const auto document=QJsonDocument::fromJson(metadata.readAll(),&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject())return false;
    const auto info=document.object();
    static const QRegularExpression hash("^[a-f0-9]{64}$");
    // Fast candidate screening on the UI thread. The isolated worker hashes the
    // model and both artifacts once before loading, rather than blocking video.
    return info["format"]=="luma-whisper-encoder-v1" &&
        info["sourceModelSha256"]=="4a3b92192b5d3578ff854a5876213e2e27af0c2d357492c2d14271e82c303658" &&
        info["sourceModelBytes"].toInteger(-1)==537819875 && model.isFile() && model.size()==537819875 &&
        info["encoderXmlBytes"].toInteger(-1)==xml.size() && info["encoderBinBytes"].toInteger(-1)==bin.size() &&
        hash.match(info["encoderXmlSha256"].toString()).hasMatch() && hash.match(info["encoderBinSha256"].toString()).hasMatch() &&
        info["inputName"]=="mel" && info["inputShape"].toArray()==QJsonArray{1,128,3000} &&
        info["outputName"]=="output_features" && info["outputShape"].toArray()==QJsonArray{1,1500,1280};
}
const QRegularExpression runtimeDependency("^(?:whisper|ggml|ggml-base|ggml-cpu-[a-z0-9_-]+|openvino|openvino_ir_frontend|openvino_intel_npu_[a-z0-9_-]+|tbb12|tbbmalloc|tbbmalloc_proxy)\\.dll$",QRegularExpression::CaseInsensitiveOption);
const QRegularExpression sha256("^[a-f0-9]{64}$");
const QRegularExpression pnpIdentity("^PCI\\\\[A-Za-z0-9_&-]+\\\\[A-Za-z0-9_&-]+$");
const QRegularExpression driverMapping("^\\{[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}\\}\\\\[0-9]{4}$");
std::optional<QJsonObject> boundedObject(const QString &path){
    QFile file(path);const QFileInfo info(file);
    if(!info.isFile() || info.isSymLink() || info.size()<=0 || info.size()>65536 || !file.open(QIODevice::ReadOnly))return {};
    QJsonParseError error;const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject())return {};
    return document.object();
}
bool integer(const QJsonValue &value,qint64 &result){
    if(!value.isDouble())return false;
    const auto number=value.toDouble();
    if(!std::isfinite(number) || std::floor(number)!=number || number<0 || number>9007199254740991.0)return false;
    result=static_cast<qint64>(number);return true;
}
bool exactInteger(const QJsonObject &object,const QString &key,qint64 expected){qint64 value=0;return integer(object[key],value) && value==expected;}
bool positiveNumber(const QJsonObject &object,const QString &key,double &result){
    if(!object[key].isDouble())return false;result=object[key].toDouble();return std::isfinite(result) && result>0;
}
bool fileRecord(const QJsonObject &object,const QString &hashKey,const QString &bytesKey,const QString &mtimeKey){
    qint64 bytes=0,mtime=0;
    return sha256.match(object[hashKey].toString()).hasMatch() && integer(object[bytesKey],bytes) && bytes>0 && bytes<=qint64(4)*1024*1024*1024 && integer(object[mtimeKey],mtime) && mtime>0;
}
bool matchesFile(const QString &path,const QJsonObject &record,const QString &bytesKey="bytes",const QString &mtimeKey="mtimeMs"){
    const QFileInfo file(path);return file.isFile() && !file.isSymLink() && file.size()==record[bytesKey].toInteger(-1) && file.lastModified().toMSecsSinceEpoch()==record[mtimeKey].toInteger(-1);
}
bool validDriver(const NpuDriverIdentity &driver){
    return driver.pnpDeviceId.size()<=512 && pnpIdentity.match(driver.pnpDeviceId).hasMatch() && driverMapping.match(driver.driverKey).hasMatch() &&
        QRegularExpression("^[0-9]{1,5}(?:\\.[0-9]{1,5}){3}$").match(driver.driverVersion).hasMatch() && !driver.driverDate.isEmpty() && driver.driverDate.size()<=64 &&
        QRegularExpression("^[A-Za-z0-9_-]+\\.inf$",QRegularExpression::CaseInsensitiveOption).match(driver.infPath).hasMatch() && driver.matchingDeviceId.size()<=512 &&
        QRegularExpression("^pci\\\\[A-Za-z0-9_&-]+$",QRegularExpression::CaseInsensitiveOption).match(driver.matchingDeviceId).hasMatch();
}
NpuDriverIdentity readDriver(const QString &pnp){
    NpuDriverIdentity result;
#ifdef Q_OS_WIN
    if(pnp.size()>512 || !pnpIdentity.match(pnp).hasMatch())return result;
    // Only the exact recorded PnP device and its validated Class mapping are
    // read. No process, CIM scan, device initialization or binary hashing.
    QSettings device("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Enum\\"+pnp,QSettings::NativeFormat);
    const auto key=device.value("Driver").toString();
    if(device.status()!=QSettings::NoError || !driverMapping.match(key).hasMatch())return result;
    QSettings driver("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\"+key,QSettings::NativeFormat);
    result={pnp,key,driver.value("DriverVersion").toString(),driver.value("DriverDate").toString(),driver.value("InfPath").toString(),driver.value("MatchingDeviceId").toString()};
    if(driver.status()!=QSettings::NoError || !validDriver(result))return {};
#else
    Q_UNUSED(pnp);
#endif
    return result;
}
NpuCalibrationStatus calibrationFailure(NpuCalibrationState state,const QString &reason){NpuCalibrationStatus result;result.state=state;result.reason=reason;return result;}
}
NpuCalibrationStatus npuCalibrationStatus(const QString &runtime,const QString &model,const NpuDriverIdentity &currentDriver){
    const QFileInfo modelInfo(model);const auto stem=modelInfo.dir().filePath(modelInfo.completeBaseName()+"-encoder-openvino");
    const auto path=stem+".npu-profile.json";
    if(!QFileInfo::exists(path))return {};
    const auto invalid=calibrationFailure(NpuCalibrationState::Invalid,"invalid-profile");
    const auto stale=calibrationFailure(NpuCalibrationState::Stale,"identity-changed");
    const auto parsed=boundedObject(path);if(!parsed)return invalid;const auto profile=*parsed;
    if(profile["format"]!="luma-npu-profile-v1" || profile["policy"]!="synthetic-16s-4requests-maxwarm3000-v1" || profile["stimulus"]!="deterministic-pcm-v1" ||
       !profile["eligible"].isBool() || !profile["inferenceVerified"].isBool() || !exactInteger(profile,"audioMs",16000) || !exactInteger(profile,"requestCount",4) ||
       !exactInteger(profile,"warmRequestCount",3) || !exactInteger(profile,"workerThreads",6) || profile["language"]!="ja" ||
       !QDateTime::fromString(profile["testedAtUtc"].toString(),Qt::ISODate).isValid() || !profile["runs"].isArray())return invalid;
    const bool eligible=profile["eligible"].toBool(),verified=profile["inferenceVerified"].toBool();const auto reason=profile["reason"].toString();
    const auto runs=profile["runs"].toArray();
#ifdef Q_OS_WIN
    const QString workerName="luma-whisper-worker.exe";
#else
    const QString workerName="luma-whisper-worker";
#endif
    // Even rejected profiles must not carry paths outside the known ai folder.
    if(!profile["worker"].isObject() || profile["worker"].toObject()["relativePath"]!=workerName || !profile["runtimeFiles"].isArray() || profile["runtimeFiles"].toArray().size()>64)return invalid;
    for(const auto &value:profile["runtimeFiles"].toArray())if(!value.isObject() || !runtimeDependency.match(value.toObject()["relativePath"].toString()).hasMatch())return invalid;
    if(!verified){
        const QStringList failures{"runtime-incomplete","preparation-failed","timeout","worker-failed","invalid-measurement"};
        if(eligible || !failures.contains(reason) || runs.size()>4)return invalid;
        return calibrationFailure(NpuCalibrationState::Rejected,reason);
    }
    double ready=0,first=0,maxInference=0,maxWall=0;
    if(runs.size()!=4 || !positiveNumber(profile,"readyMs",ready) || ready>60000 || !positiveNumber(profile,"firstInferenceMs",first))return invalid;
    for(int index=0;index<runs.size();++index){
        if(!runs[index].isObject())return invalid;const auto run=runs[index].toObject();double inference=0,wall=0,encoder=0;qint64 calls=0;
        if(!exactInteger(run,"index",index) || !exactInteger(run,"audioMs",16000) || !integer(run["encoderCalls"],calls) || calls<=0 || run["backend"]!="NPU" ||
           !QRegularExpression("^NPU(?:\\.[0-9]+)?$").match(run["device"].toString()).hasMatch() || !positiveNumber(run,"inferenceMs",inference) ||
           !positiveNumber(run,"requestWallMs",wall) || !positiveNumber(run,"encoderTotalMs",encoder))return invalid;
        if(index==0){if(first!=inference)return invalid;}else{maxInference=qMax(maxInference,inference);maxWall=qMax(maxWall,wall);}
    }
    if(!profile["warmMaxInferenceMs"].isDouble() || !profile["warmMaxWallMs"].isDouble() || profile["warmMaxInferenceMs"].toDouble()!=maxInference || profile["warmMaxWallMs"].toDouble()!=maxWall)return invalid;
    const bool measuredFast=maxInference<=3000 && maxWall<=3000;
    if(eligible!=measuredFast || reason!=(eligible?"qualified":"too-slow"))return invalid;
    const auto source=profile["source"].toObject(),encoder=profile["encoder"].toObject(),worker=profile["worker"].toObject();
    if(!fileRecord(source,"sha256","bytes","mtimeMs") || !fileRecord(encoder,"xmlSha256","xmlBytes","xmlMtimeMs") || !fileRecord(encoder,"binSha256","binBytes","binMtimeMs") ||
       !fileRecord(worker,"sha256","bytes","mtimeMs") || !profile["runtimeFiles"].isArray() || !profile["device"].isObject())return invalid;
    const auto device=profile["device"].toObject();const NpuDriverIdentity recorded{device["pnpDeviceId"].toString(),device["driverKey"].toString(),device["driverVersion"].toString(),device["driverDate"].toString(),device["infPath"].toString(),device["matchingDeviceId"].toString()};
    if(!validDriver(recorded))return invalid;
    if(worker["relativePath"]!=workerName)return invalid;
    const auto ai=QDir(runtime).filePath("ai");const auto aiCanonical=QFileInfo(ai).canonicalFilePath();
    const auto files=profile["runtimeFiles"].toArray();if(files.isEmpty() || files.size()>64)return invalid;
    QSet<QString> recordedFiles;
    for(const auto &value:files){
        if(!value.isObject())return invalid;const auto record=value.toObject();const auto name=record["relativePath"].toString();
        if(!runtimeDependency.match(name).hasMatch() || !fileRecord(record,"sha256","bytes","mtimeMs") || recordedFiles.contains(name.toLower()))return invalid;
        recordedFiles.insert(name.toLower());
    }
    for(const auto &required:QStringList{"whisper.dll","ggml.dll","ggml-base.dll","openvino.dll","openvino_ir_frontend.dll","openvino_intel_npu_plugin.dll","tbb12.dll"})if(!recordedFiles.contains(required))return invalid;
    bool hasCpu=false;for(const auto &name:recordedFiles)if(name.startsWith("ggml-cpu-"))hasCpu=true;if(!hasCpu)return invalid;
    if(!encoderCandidate(modelInfo,stem))return stale;
    const auto manifest=boundedObject(stem+".json");if(!manifest)return stale;
    if(source["sha256"]!=(*manifest)["sourceModelSha256"] || source["bytes"]!=(*manifest)["sourceModelBytes"] || encoder["xmlSha256"]!=(*manifest)["encoderXmlSha256"] || encoder["binSha256"]!=(*manifest)["encoderBinSha256"] ||
       !matchesFile(model,source) || !matchesFile(stem+".xml",encoder,"xmlBytes","xmlMtimeMs") || !matchesFile(stem+".bin",encoder,"binBytes","binMtimeMs") || !matchesFile(QDir(ai).filePath(workerName),worker))return stale;
    QSet<QString> actualFiles;for(const auto &entry:QDir(ai).entryInfoList(QDir::Files))if(runtimeDependency.match(entry.fileName()).hasMatch())actualFiles.insert(entry.fileName().toLower());
    if(actualFiles!=recordedFiles)return stale;
    for(const auto &value:files){
        const auto record=value.toObject();const auto file=QDir(ai).filePath(record["relativePath"].toString());
        if(aiCanonical.isEmpty() || QFileInfo(file).canonicalPath()!=aiCanonical || !matchesFile(file,record))return stale;
    }
    const auto detected=currentDriver.pnpDeviceId.isEmpty()?readDriver(recorded.pnpDeviceId):currentDriver;
    if(!validDriver(detected) || detected!=recorded)return calibrationFailure(NpuCalibrationState::Stale,"driver-changed");
    NpuCalibrationStatus result;result.state=eligible?NpuCalibrationState::Qualified:NpuCalibrationState::Rejected;result.eligible=eligible;result.inferenceVerified=true;result.reason=reason;
    result.completedRequests=4;result.maxWarmInferenceMs=maxInference;result.maxWarmWallMs=maxWall;return result;
}
QVector<AiBackend> recognitionBackends(const QString &runtime,const QString &model,const AiPreferences &preferences){
    return recognitionBackends(runtime,model,preferences,{});
}
QVector<AiBackend> recognitionBackends(const QString &runtime,const QString &model,const AiPreferences &preferences,const NpuDriverIdentity &currentDriver){
    QVector<AiBackend> result;
#ifdef Q_OS_WIN
    const QString suffix=".exe";
#else
    const QString suffix;
#endif
    const QString worker=QDir(runtime).filePath("ai/luma-whisper-worker"+suffix);
    const QFileInfo modelInfo(model);const QString encoder=modelInfo.dir().filePath(modelInfo.completeBaseName()+"-encoder-openvino");
    if(QFileInfo::exists(worker)){
        const bool hasEncoder=encoderCandidate(modelInfo,encoder);
        const bool automaticNpu=preferences.mode=="auto" && hasEncoder && (modelInfo.completeBaseName()=="ggml-base" || npuCalibrationStatus(runtime,model,currentDriver).eligible);
        if(hasEncoder && ((preferences.mode=="auto" && automaticNpu) || preferences.mode=="npu"))result.append({"NPU",worker,"NPU",encoder+".xml"});
        if(preferences.mode=="auto" || preferences.mode=="npu" || preferences.mode=="gpu")result.append({"GPU",worker,preferences.gpuDevice.isEmpty()?"GPU":preferences.gpuDevice,hasEncoder?encoder+".xml":QString{}});
    }
    QString cpu=QDir(runtime).filePath("whisper/whisper-cli"+suffix);
#ifndef Q_OS_WIN
    if(!QFileInfo::exists(cpu))cpu=QStandardPaths::findExecutable("whisper-cli");
#endif
    if(QFileInfo::exists(worker))result.append({"CPU",worker,"CPU",{}});
    // A native loader/runtime failure must not remove the independent CLI fallback.
    if(QFileInfo::exists(cpu) || !QFileInfo::exists(worker))result.append({"CPU",cpu,{},{}});
    return result;
}
bool confirmsBackend(const AiBackend &backend,const QString &output){
    if(backend.key=="CPU" && backend.device.isEmpty())return true;
    return QRegularExpression("(?:^|[\\r\\n])LUMA_BACKEND="+QRegularExpression::escape(backend.key)+"(?:[\\r\\n]|$)").match(output).hasMatch();
}
}
