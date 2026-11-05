#include "ai_backend.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
namespace luma {
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
        const bool hasEncoder=QFileInfo::exists(encoder+".xml") && QFileInfo::exists(encoder+".bin");
        if(hasEncoder && (preferences.mode=="auto" || preferences.mode=="npu"))result.append({"NPU",worker,"NPU",encoder+".xml"});
        if(preferences.mode=="auto" || preferences.mode=="gpu")result.append({"GPU",worker,preferences.gpuDevice.isEmpty()?"GPU":preferences.gpuDevice,hasEncoder?encoder+".xml":QString{}});
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
