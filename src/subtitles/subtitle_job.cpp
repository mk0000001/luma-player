#include "subtitle_job.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
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
}
ProcessInvocation extractionCommand(const QString &runtime,const RecognitionRequest &r,const QString &output){
    const auto start=QString::number(double(std::max<qint64>(0,r.startMs))/1000,'f',3);
    const auto duration=QString::number(double(std::clamp<qint64>(r.durationMs,1,60000))/1000,'f',3);
    if(r.disc){
        auto url=r.source.section('#',0,0);
        if(!url.startsWith("bluray://")){auto path=QDir::fromNativeSeparators(url);if(!path.endsWith('/'))path+='/';url="bluray://"+(path.startsWith('/')?QString{}:QString("/"))+path;}
        url+="#"+QString::number(std::max(1,r.title));
        return {executable(runtime,"vlc/vlc","cvlc"),{"-I","dummy","--no-one-instance","--no-video","--avcodec-hw=none","--no-osd","--play-and-exit","--bluray-menu","--bluray-region=A","--start-time="+start,"--stop-time="+QString::number(double(r.startMs+r.durationMs)/1000,'f',3),"--run-time="+duration,"--sout=#transcode{vcodec=none,acodec=s16l,channels=1,samplerate=16000}:std{access=file,mux=wav}","--sout-std-dst="+output,url,"vlc://quit"}};
    }
    return {executable(runtime,"tools/ffmpeg","ffmpeg"),{"-hide_banner","-loglevel","error","-ss",start,"-i",r.source,"-t",duration,"-map","0:a:0","-vn","-ac","1","-ar","16000","-c:a","pcm_s16le","-y",output}};
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
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{lastOutput_+=QString::fromUtf8(process_.readAllStandardOutput());if(lastOutput_.size()>16000)lastOutput_=lastOutput_.right(16000);});
    connect(&process_,&QProcess::finished,this,&SubtitleJob::completed);
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(stage_ && error==QProcess::FailedToStart){stage_=0;emit failed("AI 도구를 실행할 수 없습니다: "+process_.errorString());}});
}
SubtitleJob::~SubtitleJob(){cancel();}
bool SubtitleJob::busy()const{return stage_!=0;}
void SubtitleJob::cancel(){stage_=0;++generation_;if(process_.state()!=QProcess::NotRunning){process_.kill();process_.waitForFinished(1500);}temp_.reset();}
quint64 SubtitleJob::start(const RecognitionRequest &request,const QString &model){
    cancel();request_=request;request_.startMs=std::max<qint64>(0,request_.startMs);request_.durationMs=std::clamp<qint64>(request_.durationMs,1,60000);model_=model.isEmpty()?QDir(runtime_).filePath("models/ggml-base.bin"):model;
    if(!QFileInfo::exists(model_)){emit failed("음성인식 모델을 찾을 수 없습니다. Whisper 모델을 선택하세요.");return generation_;}
    temp_=std::make_unique<QTemporaryDir>(QDir::tempPath()+"/luma-recognition-XXXXXX");
    if(!temp_->isValid()){emit failed("AI 자막 임시 폴더를 만들 수 없습니다.");return generation_;}
    stage_=1;emit progress("음성 구간을 준비하는 중…");run(extractionCommand(runtime_,request_,temp_->filePath("audio.wav")));return generation_;
}
void SubtitleJob::run(const ProcessInvocation &command){lastOutput_.clear();process_.setProgram(command.program);process_.setArguments(command.arguments);process_.start();}
void SubtitleJob::completed(int code,QProcess::ExitStatus status){
    if(!stage_)return;
    lastOutput_+=QString::fromUtf8(process_.readAllStandardOutput());
    if(code!=0 || status!=QProcess::NormalExit){stage_=0;emit failed("AI 자막 처리 실패: "+lastOutput_.right(600));return;}
    if(stage_==1){
        if(QFileInfo(temp_->filePath("audio.wav")).size()<=44){stage_=0;emit failed("이 구간의 음성을 읽을 수 없습니다. 본편 타이틀에서 다시 시도하세요.");return;}
        stage_=2;emit progress("로컬 AI가 자막을 생성하는 중…");run(recognitionCommand(runtime_,model_,request_.language,temp_->filePath("audio.wav"),temp_->filePath("result")));return;
    }
    QFile file(temp_->filePath("result.srt"));if(!file.open(QIODevice::ReadOnly) || file.size()>8*1024*1024){stage_=0;emit failed("생성된 자막을 읽을 수 없습니다.");return;}
    auto cues=parseSrt(QString::fromUtf8(file.readAll()));for(auto &cue:cues){cue.startMs+=request_.startMs;cue.endMs=std::min(cue.endMs+request_.startMs,request_.startMs+request_.durationMs);}
    stage_=0;emit ready(generation_,cues);
}
}
