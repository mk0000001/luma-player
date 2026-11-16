#pragma once
#include <QString>
#include <QVector>
#include "luma_version.h"
namespace luma {
inline constexpr char heartTranscriptorModelFile[]=LUMA_HEART_MODEL_FILE;
inline constexpr qint64 heartTranscriptorModelBytes=LUMA_HEART_MODEL_BYTES;
bool isWhisperGgmlModel(const QString &path);
QString defaultRecognitionModel(const QString &runtime);
struct AiPreferences {QString mode="auto",gpuDevice;};
struct AiBackend {QString key,program,device,encoder;};
enum class NpuCalibrationState { Missing, Invalid, Stale, Rejected, Qualified };
struct NpuCalibrationStatus {
    NpuCalibrationState state=NpuCalibrationState::Missing;
    bool eligible=false,inferenceVerified=false;
    QString reason="not-calibrated";
    double maxWarmInferenceMs=0,maxWarmWallMs=0;
    int completedRequests=0;
};
struct NpuDriverIdentity {
    QString pnpDeviceId,driverKey,driverVersion,driverDate,infPath,matchingDeviceId;
    bool operator==(const NpuDriverIdentity &) const=default;
};
NpuCalibrationStatus npuCalibrationStatus(const QString &runtime,const QString &model,const NpuDriverIdentity &currentDriver={});
QVector<AiBackend> recognitionBackends(const QString &runtime,const QString &model,const AiPreferences &preferences={});
QVector<AiBackend> recognitionBackends(const QString &runtime,const QString &model,const AiPreferences &preferences,const NpuDriverIdentity &currentDriver);
bool confirmsBackend(const AiBackend &backend,const QString &output);
}
