#pragma once
#include <QString>
#include <QVector>
namespace luma {
struct AiPreferences {QString mode="auto",gpuDevice;};
struct AiBackend {QString key,program,device,encoder;};
QVector<AiBackend> recognitionBackends(const QString &runtime,const QString &model,const AiPreferences &preferences={});
bool confirmsBackend(const AiBackend &backend,const QString &output);
}
