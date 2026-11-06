#include "ai_backend.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
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
}
QVector<AiBackend> recognitionBackends(const QString &runtime,const QString &model,const AiPreferences &preferences){
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
        if(hasEncoder && (preferences.mode=="auto" || preferences.mode=="npu"))result.append({"NPU",worker,"NPU",encoder+".xml"});
        if(preferences.mode=="auto" || preferences.mode=="npu" || preferences.mode=="gpu")result.append({"GPU",worker,preferences.gpuDevice.isEmpty()?"GPU":preferences.gpuDevice,hasEncoder?encoder+".xml":QString{}});
    }
    QString cpu=QDir(runtime).filePath("whisper/whisper-cli"+suffix);
#ifndef Q_OS_WIN
    if(!QFileInfo::exists(cpu))cpu=QStandardPaths::findExecutable("whisper-cli");
#endif
    result.append({"CPU",cpu,{},{}});return result;
}
bool confirmsBackend(const AiBackend &backend,const QString &output){
    if(backend.key=="CPU")return true;
    return QRegularExpression("(?:^|[\\r\\n])LUMA_BACKEND="+QRegularExpression::escape(backend.key)+"(?:[\\r\\n]|$)").match(output).hasMatch();
}
}
