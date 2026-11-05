#include "ai_devices.h"
#include "runtime_environment.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
namespace luma {
AiDeviceProbe::AiDeviceProbe(QString runtime,QObject *parent):QObject(parent),runtime_(runtime.isEmpty()?QCoreApplication::applicationDirPath()+"/runtime":std::move(runtime)){
    timer_.setSingleShot(true);timer_.setInterval(15000);
    connect(&timer_,&QTimer::timeout,this,[this]{process_.kill();emit failed("AI 장치 검색 시간이 초과되었습니다.");});
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){timer_.stop();emit failed("로컬 AI 장치 검색 도구를 실행할 수 없습니다.");}});
    connect(&process_,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){
        const bool active=timer_.isActive();timer_.stop();if(!active)return;
        const auto result=QJsonDocument::fromJson(process_.readAllStandardOutput());
        if(code || status!=QProcess::NormalExit || !result.isObject() || !result.object().value("devices").isArray()){emit failed("AI 장치 목록을 확인할 수 없습니다.");return;}
        emit devicesReady(result.object().value("devices").toArray());
    });
}
void AiDeviceProbe::start(){
    if(process_.state()!=QProcess::NotRunning)return;
#ifdef Q_OS_WIN
    const QString suffix=".exe";
#else
    const QString suffix;
#endif
    process_.setProcessEnvironment(localRuntimeEnvironment(runtime_));
    process_.setProgram(QDir(runtime_).filePath("ai/luma-whisper-worker"+suffix));process_.setArguments({"--list-devices"});timer_.start();process_.start();
}
AiDeviceProbe::~AiDeviceProbe(){timer_.stop();process_.blockSignals(true);if(process_.state()!=QProcess::NotRunning){process_.kill();process_.waitForFinished(1500);}}
}
