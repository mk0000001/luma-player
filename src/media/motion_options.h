#pragma once
#include <QByteArray>
#include <QString>

namespace luma {
enum class MotionMode { Off, DisplayBlend, OpticalFlow };
struct MotionOptions {
    MotionMode mode=MotionMode::Off;
    // Optical-flow processing target, never a claim about display presentation.
    int targetFps=120;
};
struct MotionPlan {
    MotionOptions options;
    bool supported=true;
    // Software lavfi needs CPU-readable frames: use auto-copy or software decode.
    // Restore the user's decoder selection when leaving optical-flow mode.
    bool requiresCopyBack=false;
    QByteArray interpolation="no";
    QByteArray videoSync="audio";
    // One mpv API argument, including its label and graph brackets. No shell quotes.
    // Remove only @luma_motion when replacing/removing this managed filter.
    QString videoFilter;
    QString status;
};
MotionOptions normalizeMotionOptions(MotionOptions options);
// The current libVLC Blu-ray/menu backend has no implemented motion path.
MotionPlan motionPlan(MotionOptions options,bool fileBackend=true);
MotionPlan motionPlanForSource(MotionOptions options,bool fileBackend,const QString &gamma,const QString &matrix,double signalPeak);
QString motionModeLabel(MotionMode mode);
}
