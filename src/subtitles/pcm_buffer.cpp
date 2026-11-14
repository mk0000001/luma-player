#include "pcm_buffer.h"
#include "runtime_environment.h"
#include "subtitle_job.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QtEndian>
#include <algorithm>
namespace luma {
namespace {
constexpr qint64 bytesPerMillisecond=32;
constexpr qint64 offlineWindowMs=5*60*1000;
constexpr qint64 liveWindowMs=4*60*1000;
constexpr qint64 liveBurstMs=CaptionCoverage::prefetchMs+CaptionCoverage::chunkMs;
}
bool writeRecognitionWave(const QString &path,const QByteArray &pcm){
    if(pcm.isEmpty() || pcm.size()>60'000*bytesPerMillisecond || pcm.size()%2)return false;
    QFile output(path);
    if(!output.open(QIODevice::WriteOnly))return false;
    QByteArray header("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x80\x3e\0\0\0\x7d\0\0\x02\0\x10\0data\0\0\0\0",44);
    qToLittleEndian<quint32>(quint32(pcm.size()+36),header.data()+4);
    qToLittleEndian<quint32>(quint32(pcm.size()),header.data()+40);
    return output.write(header)==44 && output.write(pcm)==pcm.size() && output.flush();
}
PcmBuffer::PcmBuffer(QString runtime,QObject *parent):QObject(parent),runtime_(std::move(runtime)){
    reader_.setProcessEnvironment(localRuntimeEnvironment(runtime_));
    reader_.setProcessChannelMode(QProcess::MergedChannels);poll_.setInterval(100);
    connect(&poll_,&QTimer::timeout,this,&PcmBuffer::check);
    connect(&reader_,&QProcess::readyReadStandardOutput,this,[this]{error_+=QString::fromUtf8(reader_.readAllStandardOutput());if(error_.size()>2000)error_=error_.right(2000);});
    connect(&reader_,&QProcess::finished,this,[this]{check();if(pending_){pending_=false;poll_.stop();emit failed("음성 버퍼를 채우지 못했습니다. "+error_.right(300));}});
    connect(&reader_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart && pending_){pending_=false;poll_.stop();emit failed("음성 버퍼 도구를 실행할 수 없습니다.");}});
}
PcmBuffer::~PcmBuffer(){cancel();}
void PcmBuffer::cancelRequest(){pending_=false;poll_.stop();}
void PcmBuffer::cancel(){cancelRequest();if(reader_.state()!=QProcess::NotRunning){reader_.kill();reader_.waitForFinished(1500);}}
void PcmBuffer::prepare(const RecognitionRequest &r,const QString &wav){
    const auto key=r.source+'\n'+r.playlist+'\n'+QString::number(r.audioTrackId)+'\n'+QString::number(r.audioIndex)+'\n'+QString::number(r.readAtFullSpeed);
    const auto cachedBytes=cache_?QFileInfo(cache_->filePath("audio.pcm")).size():0;
    const auto availableEnd=cacheStart_+cachedBytes/bytesPerMillisecond;
    const auto windowMs=r.readAtFullSpeed?offlineWindowMs:liveWindowMs;
    const bool covers=key_==key && r.startMs>=cacheStart_ && r.startMs+r.durationMs<=availableEnd;
    const bool growing=key_==key && reader_.state()!=QProcess::NotRunning && r.startMs>=cacheStart_ && r.startMs<=availableEnd+15000
        && r.startMs+r.durationMs<=cacheStart_+windowMs;
    if(!covers && !growing){
        cancel();key_=key;cacheStart_=r.startMs;cache_=std::make_unique<QTemporaryDir>(QDir::tempPath()+"/luma-audio-buffer-XXXXXX");
        if(!cache_->isValid()){emit failed("음성 버퍼 임시 폴더를 만들 수 없습니다.");return;}
        auto command=extractionCommand(runtime_,r,cache_->filePath("audio.pcm"));
        const auto length=command.arguments.indexOf("-t");
        if(length>=0)command.arguments[length+1]=QString::number(windowMs/1000.0,'f',3);
        const auto codec=command.arguments.indexOf("-c:a");command.arguments.insert(codec+2,"s16le");command.arguments.insert(codec+2,"-f");
        if(!r.readAtFullSpeed){const auto seek=command.arguments.indexOf("-ss");command.arguments=command.arguments.mid(0,seek)+QStringList{"-readrate","1","-readrate_initial_burst",QString::number(liveBurstMs/1000)}+command.arguments.mid(seek);}
        error_.clear();reader_.setProgram(command.program);reader_.setArguments(command.arguments);reader_.start();
    }
    start_=r.startMs;duration_=std::clamp<qint64>(r.durationMs,1,60000);wav_=wav;pending_=true;poll_.start();check();
}
void PcmBuffer::check(){
    if(!pending_ || !cache_)return;
    QFile raw(cache_->filePath("audio.pcm"));const auto offset=(start_-cacheStart_)*bytesPerMillisecond;
    const auto count=duration_*bytesPerMillisecond;
    if(offset<0)return;
    if(raw.size()<offset+count){
        if(reader_.state()!=QProcess::NotRunning)return;
        if(reader_.exitCode()!=0)return;
    }
    if(!raw.open(QIODevice::ReadOnly) || !raw.seek(offset))return;
    auto pcm=raw.read(count);
    // Successful EOF can precede the video end; the remaining interval is silence.
    if(pcm.size()<count && reader_.state()==QProcess::NotRunning && reader_.exitCode()==0)pcm.append(QByteArray(count-pcm.size(),'\0'));
    if(pcm.size()!=count)return;
    const bool ok=writeRecognitionWave(wav_,pcm);
    pending_=false;poll_.stop();if(ok)emit ready();else emit failed("인식용 음성 저장에 실패했습니다.");
}
}
