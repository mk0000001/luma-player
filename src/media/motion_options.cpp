#include "motion_options.h"
#include <algorithm>

namespace luma {
MotionOptions normalizeMotionOptions(MotionOptions options){
    switch(options.mode){
    case MotionMode::Off:
    case MotionMode::DisplayBlend:
    case MotionMode::OpticalFlow:break;
    default:options.mode=MotionMode::Off;break;
    }
    options.targetFps=std::clamp(options.targetFps,24,360);
    return options;
}
QString motionModeLabel(MotionMode mode){
    switch(mode){
    case MotionMode::DisplayBlend:return QStringLiteral("디스플레이 동기 혼합");
    case MotionMode::OpticalFlow:return QStringLiteral("광학 흐름 · CPU 실험 기능");
    default:return QStringLiteral("프레임 보간 꺼짐");
    }
}
MotionPlan motionPlan(MotionOptions options,bool fileBackend){
    MotionPlan plan;
    plan.options=normalizeMotionOptions(options);
    if(!fileBackend && plan.options.mode!=MotionMode::Off){
        plan.supported=false;
        plan.options.mode=MotionMode::Off;
        plan.status=QStringLiteral("Blu-ray 메뉴 재생(libVLC)에서는 프레임 보간을 지원하지 않습니다.");
        return plan;
    }
    switch(plan.options.mode){
    case MotionMode::Off:
        plan.status=motionModeLabel(MotionMode::Off);
        break;
    case MotionMode::DisplayBlend:
        plan.interpolation="yes";
        plan.videoSync="display-resample";
        plan.status=QStringLiteral("디스플레이 동기 혼합 · 실제 모니터 주사율 사용");
        break;
    case MotionMode::OpticalFlow:
        plan.requiresCopyBack=true;
        plan.videoFilter=QString("@luma_motion:lavfi=[minterpolate=fps=%1:mi_mode=mci:mc_mode=aobmc:me_mode=bidir:vsbmc=1]").arg(plan.options.targetFps);
        plan.status=QStringLiteral("광학 흐름 %1 fps 목표 · CPU 고부하 실험 기능").arg(plan.options.targetFps);
        break;
    }
    return plan;
}
MotionPlan motionPlanForSource(MotionOptions options,bool fileBackend,const QString &gamma,const QString &matrix,double signalPeak){
    auto plan=motionPlan(options,fileBackend);if(!plan.requiresCopyBack)return plan;
    if(gamma.isEmpty() || gamma=="auto" || gamma=="unknown" || matrix.isEmpty()){
        plan=motionPlan({MotionMode::Off,options.targetFps},fileBackend);plan.status="영상 색상 정보를 확인한 뒤 광학 흐름 보간을 시작합니다.";
    }else if(gamma=="pq" || gamma=="hlg" || matrix=="dolbyvision" || signalPeak>1.05){
        plan=motionPlan({MotionMode::Off,options.targetFps},fileBackend);plan.status="HDR/Dolby Vision 영상에서는 광학 흐름 보간을 끕니다. 디스플레이 동기 혼합을 사용하세요.";
    }
    return plan;
}
}
