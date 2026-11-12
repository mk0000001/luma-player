#include "main_window.h"
#include "performance_overlay.h"
#include "player_controls.h"
#include "luma_version.h"
#include "media/platform_features.h"
#include "subtitles/translation_service.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSysInfo>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace luma {
namespace {
constexpr qsizetype testerEventLimit=256;
constexpr qsizetype testerReportByteLimit=1024*1024;

QString knownValue(const QString &value,std::initializer_list<const char *> allowed){
    for(const auto *item:allowed)if(value==item)return value;
    return {};
}

QString deviceId(const QString &value){
    static const QRegularExpression pattern(
        R"(\A(?:(?:OpenVINO:)?(?:GPU|NPU)(?:\.[0-9]{1,3})?|CPU|(?:CUDA|Vulkan)[0-9]{1,3})\z)");
    return value.size()<=16 && pattern.match(value).hasMatch()?value:QString{};
}

QJsonObject requestedAi(const AiPreferences &preferences){
    return {{"mode",knownValue(preferences.mode,{"auto","cpu","gpu","npu"})},
            {"device",deviceId(preferences.gpuDevice)}};
}

QJsonObject translationBackendEvidence(const QString &description){
    if(description=="CPU / llama.cpp")return {{"backend","cpu"},{"accelerator","llama.cpp"},{"device","CPU"}};
    const auto fields=description.left(64).split(" / ");
    if(fields.size()<3)return {};
    const auto backend=knownValue(fields[0],{"GPU","NPU"}).toLower();
    if(backend.isEmpty())return {};
    if(fields[1]=="OpenVINO"){
        const auto device=deviceId(fields[2]);
        if(!device.startsWith(fields[0]))return {};
        return {{"backend",backend},{"accelerator","openvino"},{"device",device}};
    }
    const auto device=deviceId(fields[1]);
    const bool vulkan=device.startsWith("Vulkan"),cuda=device.startsWith("CUDA");
    if(backend!="gpu" || (!vulkan && !cuda))return {};
    return {{"backend","gpu"},{"accelerator",vulkan?"vulkan":"cuda"},{"device",device},{"gpuOffloadVerified",true}};
}

const char *asrFallbackReason(const QString &reason){
    if(reason=="시간 초과")return "timeout";
    if(reason=="처리 실패")return "process_failed";
    if(reason=="실행할 수 없음")return "launch_failed";
    if(reason=="가속 실행 확인 실패")return "acceleration_unconfirmed";
    if(reason=="자막 결과 없음")return "result_missing";
    if(reason=="모델 준비 시간 초과")return "model_start_timeout";
    return "other";
}

const char *asrFailureReason(const QString &message){
    if(message.startsWith("음성인식 모델을 찾을 수 없습니다."))return "model_missing";
    if(message.startsWith("사용 가능한 로컬 음성인식 장치를 찾지 못했습니다."))return "backends_exhausted";
    if(message.startsWith("음성 읽기 시간이 초과되었습니다."))return "audio_timeout";
    if(message.startsWith("AI 자막 처리 실패:"))return "audio_extraction_failed";
    if(message.startsWith("AI 도구를 실행할 수 없습니다:"))return "launch_failed";
    if(message.startsWith("이 구간의 음성을 읽을 수 없습니다."))return "audio_unavailable";
    return "other";
}

const char *translationFallbackReason(const QString &reason){
    if(reason=="모델 시작 시간이 초과되었습니다.")return "startup_timeout";
    if(reason=="현재 NPU 드라이버가 모델 컴파일 옵션을 지원하지 않습니다.")return "driver_option_unsupported";
    if(reason=="OpenVINO 모델 컴파일 또는 추론에 실패했습니다.")return "openvino_failed";
    if(reason=="네이티브 번역 실행기를 시작하지 못했습니다.")return "launch_failed";
    if(reason.startsWith("실행기가 종료되었습니다 (코드 "))return "process_exited";
    if(reason=="현재 NPU 실행기의 입력 한도로 문맥 번역을 처리할 수 없어 다음 장치를 시도합니다.")return "input_limit";
    if(reason=="로컬 번역 추론 요청이 실패했습니다.")return "inference_failed";
    if(reason=="실제 실행 장치를 확인하지 못했습니다.")return "acceleration_unconfirmed";
    return "other";
}

const char *translationFailureReason(const QString &message){
    if(message.startsWith("한국어 번역 모델이 없습니다."))return "model_missing";
    if(message=="로컬 번역 포트를 열 수 없습니다.")return "localhost_unavailable";
    if(message.startsWith("사용 가능한 로컬 한국어 번역 실행기가 없습니다."))return "backends_exhausted";
    return "other";
}

QString hardwareDecoderId(const QString &value){
    if(value=="CPU · Blu-ray 호환 디코딩" || value=="no" || value=="none")return "software";
    return knownValue(value.toLower(),{"auto","auto-safe","auto-copy","software","d3d11va","d3d11va-copy","dxva2",
        "dxva2-copy","vaapi","vaapi-copy","nvdec","nvdec-copy","vulkan","vulkan-copy","videotoolbox","videotoolbox-copy"});
}

QString videoCodecId(const QString &value){
    const auto codec=value.toLower();
    if(codec.startsWith("h.264") || codec=="avc1")return "h264";
    if(codec.startsWith("hevc") || codec=="h265" || codec=="hvc1")return "hevc";
    if(codec.startsWith("mpeg-2 video") || codec=="mpgv" || codec=="mp2v")return "mpeg2video";
    if(codec.startsWith("av1") || codec=="av01" || codec=="alliance for open media av1")return "av1";
    if(codec.startsWith("vp9") || codec=="vp09")return "vp9";
    return knownValue(codec,{"h264","libdav1d","vp8","mpeg1video","mpeg2video","mpeg4","vc1","prores","ffv1","theora"});
}

QString videoDecoderId(const QString &value){
    return knownValue(value.toLower(),{"h264","h264_mf","hevc","hevc_mf","vp8","vp9","vp9_mf","av1","av1_mf","libdav1d",
        "mpeg1video","mpeg2video","mpeg4","vc1","prores","ffv1","theora"});
}

QString audioCodecId(const QString &value){
    const auto codec=value.trimmed().toLower();
    if(codec=="a52")return "ac3";
    if(codec.startsWith("pcm_") || codec=="lpcm")return "pcm";
    return knownValue(codec,{"aac","ac3","eac3","truehd","mlp","dts","flac","mp3","mp2","opus","vorbis","alac","pcm"});
}

const char *playbackFailureReason(const QString &message,bool disc){
    if(message.startsWith("AACS 설정 파일을 찾거나 열 수 없습니다."))return "aacs_config_unavailable";
    if(message.startsWith("AACS 읽기 라이브러리를 불러오지 못했습니다."))return "aacs_runtime_unavailable";
    if(message.startsWith("BD+ 읽기 라이브러리를 불러오지 못했습니다."))return "bdplus_runtime_unavailable";
    if(message=="video output initialization failed")return "video_output_init_failed";
    if(message=="audio output initialization failed")return "audio_output_init_failed";
    if(message=="loading failed")return "loading_failed";
    if(message=="재생 엔진이 파일 열기 요청을 거부했습니다.")return "file_open_rejected";
    return disc?"libvlc":"libmpv";
}

QJsonObject installedWorkers(){
    const QDir runtime(QCoreApplication::applicationDirPath()+"/runtime");
#ifdef Q_OS_WIN
    const QString suffix=".exe";
#else
    const QString suffix;
#endif
    const auto exists=[&](const QString &file){return QFileInfo(runtime.filePath(file)).isFile();};
    QJsonObject result{{"asrNative",exists("ai/luma-whisper-worker"+suffix)},{"asrCli",exists("whisper/whisper-cli"+suffix)},
        {"translationVulkan",exists("translation/vulkan/llama-server"+suffix)},
        {"translationCuda",exists("translation/cuda/llama-server"+suffix)},
        {"translationOpenVino",exists("translation/openvino/llama-server"+suffix)}};
#ifdef Q_OS_WIN
    result.insert("asrVulkan",exists("ai/ggml-vulkan.dll"));
    result.insert("asrCuda",exists("ai/ggml-cuda.dll"));
    result.insert("asrOpenVino",exists("ai/openvino.dll"));
#endif
    return result;
}

QJsonObject lastAccelerationInstall(){
    const auto saved=QSettings().value("ai/accelerationLastInstall").toMap();
    const auto backend=knownValue(saved.value("backend").toString(),{"vulkan","cuda","openvino"});
    const auto result=knownValue(saved.value("result").toString(),{"verified","cancelled","failed"});
    bool codeValid=false;
    const auto code=saved.value("exitCode").toInt(&codeValid);
    if(backend.isEmpty() || result.isEmpty() || !codeValid)return {};
    return {{"backend",backend},{"result",result},{"exitCode",code}};
}

QByteArray serializeTesterReport(QJsonObject report){
    auto bytes=QJsonDocument(report).toJson();
    if(bytes.size()<=testerReportByteLimit)return bytes;
    bytes=QJsonDocument(report).toJson(QJsonDocument::Compact);
    if(bytes.size()<=testerReportByteLimit)return bytes;

    auto performance=report.value("performance").toObject();
    auto samples=performance.value("samples").toArray();
    const auto total=samples.size();
    while(!samples.isEmpty() && bytes.size()>testerReportByteLimit){
        const auto discard=std::min<qsizetype>(16,samples.size());
        for(qsizetype i=0;i<discard;++i)samples.removeFirst();
        performance.insert("samples",samples);
        performance.insert("omittedForSize",total-samples.size());
        report.insert("performance",performance);
        bytes=QJsonDocument(report).toJson(QJsonDocument::Compact);
    }
    return bytes;
}
}

void MainWindow::setDeveloperDiagnosticCollection(bool enabled){
    performance_->setDiagnosticCollection(enabled);
    if(discEngine_)discEngine_->setPerformanceMetricsEnabled(enabled);
    if(fileEngine_)fileEngine_->setPerformanceMetricsEnabled(enabled);
    if(!enabled){
        developerEvents_={};
        droppedDeveloperEvents_=0;
    }
}

void MainWindow::recordDeveloperEvent(const char *code,const char *reason,const QString &device){
    if(!developerApproved_)return;
    const auto elapsed=performance_->elapsedMs();
    if(elapsed<0)return;
    QJsonObject entry{{"elapsedMs",elapsed},{"code",code},{"reason",reason}};
    const auto id=deviceId(device);
    if(!id.isEmpty())entry.insert("device",id);
    if(developerEvents_.size()==testerEventLimit){
        developerEvents_.removeFirst();
        if(droppedDeveloperEvents_<std::numeric_limits<int>::max())++droppedDeveloperEvents_;
    }
    developerEvents_.append(entry);
}

void MainWindow::connectDeveloperDiagnostics(){
    connect(&job_,&SubtitleJob::progress,this,[this](const QString &message){
        if(!developerApproved_ || message.size()>128)return;
        static const QRegularExpression fallback(R"(\A(GPU|NPU|CPU) 사용 불가 \(([^)]+)\) · 다음 장치로 전환 중…\z)");
        const auto match=fallback.match(message);
        if(match.hasMatch())recordDeveloperEvent("asr_fallback",asrFallbackReason(match.captured(2)),match.captured(1));
    });
    connect(&job_,&SubtitleJob::failed,this,[this](const QString &message){
        recordDeveloperEvent("asr_failed",asrFailureReason(message));
    });
    connect(&job_,&SubtitleJob::backendSelected,this,[this,last=QString{}](const QString &backend) mutable {
        if(!developerApproved_){last.clear();return;}
        const auto kind=knownValue(backend,{"CPU","GPU","NPU"});
        if(kind.isEmpty())return;
        const auto device=deviceId(job_.selectedDevice());
        const auto identity=kind+'/'+device+'/'+job_.selectedAccelerator();
        if(identity==last)return;
        last=identity;
        recordDeveloperEvent("asr_backend_confirmed",kind=="GPU"?"gpu":kind=="NPU"?"npu":"cpu",device);
    });
    connect(translation_,&TranslationService::progress,this,[this](const QString &message){
        if(!developerApproved_ || message.size()>160)return;
        const QString fallback=" 번역 사용 불가: ",missing=" 번역 실행기가 없습니다.";
        const auto separator=message.indexOf(fallback);
        if(separator>=0){
            const auto device=deviceId(message.left(separator));
            if(!device.isEmpty())
                recordDeveloperEvent("translation_fallback",translationFallbackReason(message.mid(separator+fallback.size())),device);
        }else if(message.endsWith(missing)){
            const auto device=deviceId(message.chopped(missing.size()));
            if(!device.isEmpty())recordDeveloperEvent("translation_backend_missing","worker_missing",device);
        }
    });
    connect(translation_,&TranslationService::backendSelected,this,[this](const QString &backend){
        if(!developerApproved_)return;
        const auto evidence=translationBackendEvidence(backend);
        if(!evidence.isEmpty())recordDeveloperEvent("translation_backend_confirmed",
            evidence.value("gpuOffloadVerified").toBool()?"gpu_offload":"backend_verified",evidence.value("device").toString());
    });
    connect(translation_,&TranslationService::failed,this,[this](const QString &message){
        recordDeveloperEvent("translation_failed",translationFailureReason(message));
    });
    connect(translation_,&TranslationService::skippedCue,this,[this]{
        recordDeveloperEvent("translation_cue_skipped","request_skipped");
    });
}

void MainWindow::connectDeveloperPlaybackDiagnostics(PlaybackEngine *engine){
    const bool disc=engine==discEngine_.get();
    if(!engine->available())recordDeveloperEvent("video_runtime_unavailable",disc?"libvlc":"libmpv");
    connect(engine,&PlaybackEngine::videoOutputLost,this,[this](const QString &message){
        const auto *reason=message.endsWith("(DXGI_ERROR_DEVICE_REMOVED, 0x887a0005)")?"dxgi_device_removed":
                           message.endsWith("(DXGI_ERROR_DEVICE_RESET, 0x887a0007)")?"dxgi_device_reset":
                           message=="Blu-ray GPU 영상이 멈춰 호환 디코딩으로 복구합니다."?"decoder_stalled":"other";
        recordDeveloperEvent("video_output_lost",reason);
    });
    connect(engine,&PlaybackEngine::errorOccurred,this,[this,disc](const QString &message){
        recordDeveloperEvent("video_failed",playbackFailureReason(message,disc));
    });
    connect(engine,&PlaybackEngine::logMessage,this,[this,disc,opened=QString("Opening with ")+engine->name()](const QString &message){
        if(!developerApproved_)return;
        if(message==opened)
            recordDeveloperEvent("video_opened",disc?"libvlc":"libmpv");
        if(message.startsWith("재생 엔진이 거부한 설정 · "))
            recordDeveloperEvent("video_option_rejected","runtime_rejected_override");
    });
    connect(engine,&PlaybackEngine::updated,this,[this,engine,last=QString{},generation=quint64{}]() mutable {
        if(!developerApproved_ || engine!=active_){last.clear();return;}
        if(generation!=sourceGeneration_){generation=sourceGeneration_;last.clear();}
        const auto &state=engine->state();
        if(state.videoCodec.isEmpty())return;
        const auto decoder=hardwareDecoderId(state.hardwareDecoder);
        if(decoder.isEmpty() || decoder==last)return;
        last=decoder;
        recordDeveloperEvent("video_decoder_active",decoder.toLatin1().constData());
    });
}

QJsonObject MainWindow::testerDiagnosticReport() const{
    static const QRegularExpression kernelVersion(R"(\A[0-9]{1,6}(?:\.[0-9]{1,6}){0,3})");
    static const QRegularExpression driverVersion(R"(\A[0-9]{1,5}(?:\.[0-9]{1,5}){3}\z)");
    static const QRegularExpression engineVersion(R"(\A(?:mpv )?v?([0-9]{1,4}\.[0-9]{1,4}(?:\.[0-9]{1,4}){0,2})(?=[-+ ]|\z))");
    const QJsonObject os{{"kernel",knownValue(QSysInfo::kernelType(),{"winnt","linux","darwin","freebsd","openbsd","netbsd"})},
        {"kernelVersion",kernelVersion.match(QSysInfo::kernelVersion()).captured(0)},
        {"architecture",QSysInfo::buildCpuArchitecture()}};
    const auto preferences=videoOutputPreferences();
    const auto selectedAdapter=QSettings().value("video/adapter").toString();
    constexpr std::array hardware{"auto","software","d3d11va","dxva2","vaapi"};
    constexpr std::array codecs{"auto","h264","hevc","vp9","av1","libdav1d"};
    QJsonObject configured{{"hardware",hardware.at(static_cast<size_t>(preferences.hardware))},
        {"codec",codecs.at(static_cast<size_t>(preferences.codec))},
        {"adapterSelection",selectedAdapter.isEmpty()?"auto":"manual"}};
    QJsonArray adapters;
    for(const auto &value:videoAdapters(true)){
        const auto adapter=value.toObject();
        const int vendor=adapter.value("vendorId").toInt(-1),device=adapter.value("deviceId").toInt(-1);
        if(vendor<0 || vendor>0xffff || device<0 || device>0xffff)continue;
        const auto version=adapter.value("driverVersion").toString();
        QJsonObject entry{{"vendorId",vendor},{"deviceId",device}};
        if(driverVersion.match(version).hasMatch())entry.insert("driverVersion",version);
        const auto memory=adapter.value("dedicatedMemoryMiB").toDouble(-1);
        if(std::isfinite(memory) && memory>=0 && memory<=1024*1024)entry.insert("dedicatedMemoryMiB",memory);
        if(!selectedAdapter.isEmpty() && adapter.value("id").toString()==selectedAdapter)configured.insert("adapter",entry);
        if(adapters.size()<16)adapters.append(entry);
    }
    QJsonObject video{{"configured",configured},{"recoveries",videoRecoveryCount_},{"recoveryExhausted",videoRecoveryExhausted_},
        {"libmpvAvailable",fileEngine_ && fileEngine_->available()},{"libvlcAvailable",discEngine_ && discEngine_->available()}};
    if(active_){
        const auto &state=active_->state();
        const auto requested=hardwareDecoderId(state.videoDecoderRequest.section(" / ",0,0));
        const auto actualDecoder=videoDecoderId(state.videoDecoder);
        const auto current=state.videoCodec.isEmpty() && actualDecoder.isEmpty()?QString{}:hardwareDecoderId(state.hardwareDecoder);
        auto codec=videoCodecId(state.videoCodec);
        if(codec.isEmpty())
            codec=actualDecoder=="libdav1d"?QString("av1"):actualDecoder.endsWith("_mf")?actualDecoder.chopped(3):actualDecoder;
        QJsonObject playback{{"engine",active_==discEngine_.get()?"libvlc":"libmpv"},{"playing",state.playing},{"failed",state.failed},
            {"buffering",state.buffering || state.fileCacheHolding},
            {"sourceKind",active_==discEngine_.get()?"bluray":lanRemotePlayback_?"lan":"file"},
            {"timelineReady",state.timelineReady},
            {"requestedHardware",requested},{"requestedDecoder",videoDecoderId(state.videoDecoderRequest.section(" / ",1,1))},
            {"actualHardware",current},{"actualDecoder",actualDecoder},{"videoCodec",codec},
            {"droppedFrames",state.droppedVideoFrames},{"lostAudioBuffers",state.lostAudioBuffers},
            {"overrideRejected",!state.videoOverrideStatus.isEmpty()},{"forcedHdr",state.videoColorForced},
            {"audioBitstreamRequested",state.audioBitstreamRequested},{"audioBitstreamActive",state.audioBitstreamActive},
            {"audioSettingsWarning",!state.audioSettingsError.isEmpty()}};
        const auto runtime=engineVersion.match(state.runtimeVersion).captured(1);
        if(!runtime.isEmpty())playback.insert("engineVersion",runtime);
        if(std::isfinite(state.sourceFps) && state.sourceFps>0 && state.sourceFps<=1000)playback.insert("sourceFps",state.sourceFps);
        const auto range=knownValue(state.sourceVideoRange,{"sdr","pq","hlg","unknown"});
        if(!range.isEmpty())playback.insert("sourceVideoRange",range);
        const auto transfer=knownValue(state.transferFunction,{"pq","hlg","bt.1886","srgb","linear","gamma2.2","gamma2.4"});
        if(!transfer.isEmpty())playback.insert("inputTransfer",transfer);
        const auto outputTransfer=knownValue(state.outputTransferFunction,{"pq","hlg","bt.1886","srgb","linear","gamma2.2","gamma2.4"});
        if(!outputTransfer.isEmpty())playback.insert("outputTransfer",outputTransfer);
        const auto primaries=knownValue(state.colorPrimaries,{"bt.601-525","bt.601-625","bt.709","bt.2020","dci-p3","display-p3"});
        if(!primaries.isEmpty())playback.insert("inputPrimaries",primaries);
        const auto outputPrimaries=knownValue(state.outputPrimaries,{"bt.601-525","bt.601-625","bt.709","bt.2020","dci-p3","display-p3"});
        if(!outputPrimaries.isEmpty())playback.insert("outputPrimaries",outputPrimaries);
        if(state.dolbyVisionProfile && *state.dolbyVisionProfile>=0 && *state.dolbyVisionProfile<=255)
            playback.insert("dolbyVisionProfile",*state.dolbyVisionProfile);
        const auto audioCodec=audioCodecId(state.audioCodec);
        if(!audioCodec.isEmpty())playback.insert("audioCodec",audioCodec);
        if(state.audioSampleRate>0 && state.audioSampleRate<=1000000)playback.insert("audioSampleRateHz",state.audioSampleRate);
        if(state.audioChannels>0 && state.audioChannels<=64)playback.insert("audioChannels",state.audioChannels);
        const auto automatic=knownValue(requested,{"auto","auto-safe","auto-copy"});
        if(!requested.isEmpty() && !current.isEmpty() && automatic.isEmpty()){
            const auto family=[](const QString &decoder){return decoder.endsWith("-copy")?decoder.chopped(5):decoder;};
            playback.insert("decoderFallback",family(requested)!=family(current));
        }
        video.insert("playback",playback);
    }
    QJsonObject asr{{"requested",requestedAi(job_.preferences())},{"enabled",aiEnabled_->isChecked()},
        {"busy",job_.busy()},{"failed",aiFailed_},{"retryCount",aiRetryCount_}};
    const auto asrBackend=knownValue(job_.selectedBackend(),{"CPU","GPU","NPU"}).toLower();
    if(!asrBackend.isEmpty())asr.insert("lastConfirmed",QJsonObject{{"backend",asrBackend},
        {"accelerator",job_.selectedAccelerator()},{"device",deviceId(job_.selectedDevice())},
        {"alignment",knownValue(job_.selectedAlignment(),{"dtw","segment","token"})}});
    QJsonObject translation{{"requested",requestedAi(translation_->preferences())},{"enabled",translationEnabled_->isChecked()},
        {"busy",translation_->busy()},{"failed",translationFailed_}};
    const auto actualTranslation=translationBackendEvidence(translation_->selectedBackend());
    if(!actualTranslation.isEmpty())translation.insert("actual",actualTranslation);
    QJsonObject report{{"schemaVersion",2},{"appVersion",LUMA_VERSION},{"qtVersion",qVersion()},{"os",os},{"graphicsAdapters",adapters},
        {"installedWorkers",installedWorkers()},{"video",video},{"asr",asr},{"translation",translation},
        {"performance",performance_->diagnosticHistory()},
        {"events",developerEvents_},{"droppedEvents",droppedDeveloperEvents_}};
    const auto installer=lastAccelerationInstall();
    if(!installer.isEmpty())report.insert("lastAccelerationInstall",installer);
    return report;
}

PerformanceInput MainWindow::performanceInput() const {
    PerformanceInput input;
    input.sourceGeneration = sourceGeneration_;
    input.recoveries = videoRecoveryCount_;
    input.captionWaiting = bufferHolding_;
    input.recognitionBackend = job_.selectedBackend();
    input.recognitionRuntime = job_.selectedAccelerator();
    if (translation_) {
        input.translationBackend = translation_->selectedBackend();
        input.translationRuntime = translationBackendEvidence(input.translationBackend).value("accelerator").toString();
    }
    if (active_) {
        input.playback = active_->state();
        input.disc = active_ == discEngine_.get();
        if (liveCaptioningActive())
            input.captionAheadMs = coverage_.bufferedAhead(input.playback->positionMs, input.playback->durationMs);
    }
    return input;
}

QJsonObject MainWindow::developerSnapshot() const {
    const auto input = performanceInput();
    QJsonObject result{
        {"version", LUMA_VERSION}, {"performance", performance_->summary()},
        {"videoRecoveries", input.recoveries}, {"captionAheadMs", input.captionAheadMs},
        {"captionWaiting", input.captionWaiting}, {"recognitionBackend", input.recognitionBackend},
        {"translationBackend", input.translationBackend}, {"captureAllowed", captureApproved_}
    };
    if (const auto &state = input.playback) {
        result.insert("playback", QJsonObject{
            {"engine", input.disc ? "libVLC" : "libmpv"}, {"playing", state->playing},
            {"buffering", state->buffering || state->fileCacheHolding}, {"failed", state->failed},
            {"videoCodec", state->videoCodec}, {"audioCodec", state->audioCodec},
            {"decoder", state->hardwareDecoder}, {"videoDecoder", state->videoDecoder},
            {"dolbyVisionProfile", state->dolbyVisionProfile ? QJsonValue(*state->dolbyVisionProfile) : QJsonValue()},
            {"droppedFrames", state->droppedVideoFrames},
            {"lostAudioBuffers", state->lostAudioBuffers}, {"sourceFps", state->sourceFps},
            {"inputTransfer", state->transferFunction}, {"inputPrimaries", state->colorPrimaries},
            {"outputTransfer", state->outputTransferFunction}, {"outputPrimaries", state->outputPrimaries},
            {"requestedDecoder", state->videoDecoderRequest}, {"requestedTransfer", state->videoColorRequest},
            {"forcedHdr", state->videoColorForced}, {"audioFormat", state->audioFormat},
            {"audioLayout", state->audioLayout}, {"audioBitstreamRequested", state->audioBitstreamRequested},
            {"audioBitstreamActive", state->audioBitstreamActive}, {"audioVolumeExternal", state->audioVolumeExternal},
            {"requestedDolbyVision", state->dolbyVisionRequest}, {"overrideStatus", state->videoOverrideStatus}
        });
    }
    return result;
}

void MainWindow::buildDeveloperTools(QVBoxLayout *layout) {
    performance_ = new PerformanceOverlay(surface_, [this] { return performanceInput(); });
    auto *heading = new QLabel("성능 진단");
    heading->setObjectName("section");
    layout->addWidget(heading);

    performanceEnabled_ = new QCheckBox("성능 오버레이");
    performanceEnabled_->setObjectName("performanceEnabled");
    performanceEnabled_->setToolTip("Ctrl+Shift+D");
    layout->addWidget(performanceEnabled_);
    auto *options = new QFormLayout;
    options->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *view = new ChoiceBox;
    view->setObjectName("performanceView");
    view->addItem("텍스트 + 그래프", "both");
    view->addItem("텍스트", "text");
    view->addItem("그래프", "graph");
    options->addRow("표시", view);
    auto *corner = new ChoiceBox;
    corner->setObjectName("performanceCorner");
    corner->addItem("오른쪽 위", "right");
    corner->addItem("왼쪽 위", "left");
    options->addRow("위치", corner);
    layout->addLayout(options);

    auto *feedback = new QLabel;
    feedback->setObjectName("performanceFeedback");
    feedback->setWordWrap(true);
    feedback->setTextFormat(Qt::PlainText);
    const auto addButton = [layout](const QString &name, const char *id) {
        auto *button = new QPushButton(name);
        button->setObjectName(id);
        layout->addWidget(button);
        return button;
    };
    auto *copy = addButton("진단 요약 복사", "copyDiagnostics");
    auto *exportTester = addButton("테스트 진단 JSON 저장", "exportTesterDiagnostics");
    exportTester->setToolTip("최근 5분 성능과 GPU·드라이버, 디코더, AI 실행 상태를 JSON 파일로 저장합니다.");
    auto *exportCsv = addButton("성능 기록 CSV 저장", "exportPerformance");
    auto *clear = addButton("성능 기록 초기화", "clearPerformance");
    auto *clearLog = addButton("재생 로그 지우기", "clearDeveloperLog");
    layout->addWidget(feedback);

    QSettings settings;
    view->setCurrentIndex(std::max(0, view->findData(settings.value("developer/performanceView", "both"))));
    corner->setCurrentIndex(std::max(0, corner->findData(settings.value("developer/performanceCorner", "right"))));
    const auto applyOptions = [this, view, corner] {
        const auto style = view->currentData().toString();
        const auto position = corner->currentData().toString();
        QSettings preferences;
        preferences.setValue("developer/performanceView", style);
        preferences.setValue("developer/performanceCorner", position);
        performance_->setView(style == "text" ? PerformanceOverlay::View::Text :
                              style == "graph" ? PerformanceOverlay::View::Graph : PerformanceOverlay::View::Both);
        performance_->setLeftAligned(position == "left");
    };
    applyOptions();
    connect(view, &QComboBox::currentIndexChanged, this, applyOptions);
    connect(corner, &QComboBox::currentIndexChanged, this, applyOptions);
    connect(performanceEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        const bool allowed = developerApproved_ && enabled;
        QSettings().setValue("developer/performanceEnabled", allowed);
        performance_->setMonitoring(allowed);
    });
    setDeveloperDiagnosticCollection(developerApproved_);
    performanceEnabled_->setChecked(developerApproved_ && settings.value("developer/performanceEnabled", false).toBool());

    connect(copy, &QPushButton::clicked, this, [this, feedback] {
        if (!developerApproved_)
            return;
        QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(developerSnapshot()).toJson()));
        feedback->setText("진단 요약을 복사했습니다.");
    });
    connect(exportTester, &QPushButton::clicked, this, [this, feedback] {
        if(!developerApproved_)return;
        const auto path=QFileDialog::getSaveFileName(this,"테스트 진단 저장","LumaPlayer-diagnostics.json","JSON (*.json)");
        if(path.isEmpty() || !developerApproved_)return;
        const auto report=serializeTesterReport(testerDiagnosticReport());
        QSaveFile output(path);
        const bool saved=report.size()<=testerReportByteLimit && output.open(QIODevice::WriteOnly) &&
            output.write(report)==report.size() && output.commit();
        feedback->setText(saved?"테스트 진단을 저장했습니다.":"테스트 진단을 저장하지 못했습니다.");
    });
    connect(exportCsv, &QPushButton::clicked, this, [this, feedback] {
        if (!developerApproved_)
            return;
        if (!performance_->hasSamples()) {
            feedback->setText("성능 측정값을 수집하고 있습니다. 잠시 후 다시 저장하세요.");
            return;
        }
        const auto history = performance_->csv();
        const auto path = QFileDialog::getSaveFileName(this, "성능 기록 저장", "LumaPlayer-performance.csv", "CSV (*.csv)");
        if (path.isEmpty())
            return;
        QSaveFile output(path);
        const bool saved = output.open(QIODevice::WriteOnly) && output.write(history) == history.size() && output.commit();
        feedback->setText(saved ? "성능 기록을 저장했습니다." : "성능 기록을 저장하지 못했습니다.");
    });
    connect(clear, &QPushButton::clicked, this, [this, feedback] {
        if (developerApproved_) {
            performance_->clearHistory();
            feedback->setText("성능 기록을 초기화했습니다.");
        }
    });
    connect(clearLog, &QPushButton::clicked, this, [this] {
        if (developerApproved_) {
            log_->clear();
            developerEvents_={};
            droppedDeveloperEvents_=0;
        }
    });
    buildDeveloperVideoTools(layout);
}

void MainWindow::buildDeveloperVideoTools(QVBoxLayout *layout) {
    using Hardware=VideoOverrides::Hardware;
    using Codec=VideoOverrides::Codec;
    using Color=VideoOverrides::Color;
    using Dolby=VideoOverrides::DolbyVision;
    auto *heading=new QLabel("영상 디코딩과 색상");
    heading->setObjectName("section");
    layout->addWidget(heading);
    auto *form=new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addLayout(form);
    const auto choice=[form](const QString &label,const char *name){
        auto *box=new ChoiceBox;
        box->setObjectName(name);
        form->addRow(label,box);
        return box;
    };
    auto *hardware=choice("디코딩 장치","developerHardware");
    hardware->addItem("자동",int(Hardware::Auto));
    hardware->addItem("CPU 소프트웨어",int(Hardware::Software));
#ifdef Q_OS_WIN
    hardware->addItem("D3D11VA",int(Hardware::D3D11));
    hardware->addItem("DXVA2",int(Hardware::DXVA2));
#elif defined(Q_OS_LINUX)
    hardware->addItem("VAAPI",int(Hardware::VAAPI));
#endif
    auto *codec=choice("파일 디코더 우선순위","developerCodec");
    codec->addItem("자동",int(Codec::Auto));
    codec->addItem("H.264 · FFmpeg",int(Codec::H264));
    codec->addItem("HEVC · FFmpeg",int(Codec::HEVC));
    codec->addItem("VP9 · FFmpeg",int(Codec::VP9));
    codec->addItem("AV1 · FFmpeg",int(Codec::AV1));
    codec->addItem("AV1 · dav1d",int(Codec::Dav1d));
    auto *color=choice("파일 출력 형식","developerColor");
    color->addItem("자동",int(Color::Auto));
    color->addItem("SDR · BT.709",int(Color::SDR));
    color->addItem("HDR10 · PQ / BT.2020",int(Color::HDR10));
    color->addItem("HDR · HLG / BT.2020",int(Color::HLG));
    auto *dolby=choice("파일 Dolby Vision","developerDolby");
    dolby->addItem("자동",int(Dolby::Auto));
    dolby->addItem("메타데이터 적용",int(Dolby::Apply));
    dolby->addItem("메타데이터 무시",int(Dolby::Ignore));
    dolby->addItem("기본 레이어 + 메타데이터",int(Dolby::BaseLayer));
    dolby->setToolTip("소스의 Dolby Vision 메타데이터를 영상 변환에 사용합니다. HDR10 출력을 함께 요청할 수 있습니다.");
    auto *force=new QCheckBox("SDR 화면에서도 HDR 렌더링 강제");
    force->setObjectName("developerForceHdr");
    force->setToolTip("시험용 설정입니다. SDR 화면에서 색상과 밝기가 달라질 수 있습니다.");
    layout->addWidget(force);

    const std::array boxes{hardware,codec,color,dolby};
    QSettings settings;
    force->setChecked(settings.value("developer/forceHdr",false).toBool());
    for(auto *box:boxes)
        box->setCurrentIndex(std::max(0,box->findData(settings.value("developer/"+box->objectName(),0).toInt())));
    const auto read=[hardware,codec,color,dolby,force]{
        return VideoOverrides{Hardware(hardware->currentData().toInt()),Codec(codec->currentData().toInt()),
                              Color(color->currentData().toInt()),Dolby(dolby->currentData().toInt()),force->isChecked()};
    };
    developerVideoOptions_=read();
    auto *note=new QLabel("출력 형식과 HDR 강제는 현재 파일에 적용합니다. 디코더와 Dolby Vision 설정은 다음에 열 때 적용합니다.");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *reset=new QPushButton("영상 설정 자동으로 되돌리기");
    reset->setObjectName("resetDeveloperVideo");
    layout->addWidget(reset);
    developerVideoStatus_=new QLabel("미디어를 열면 디코더와 입·출력 색상을 표시합니다.");
    developerVideoStatus_->setObjectName("developerVideoStatus");
    developerVideoStatus_->setWordWrap(true);
    developerVideoStatus_->setTextFormat(Qt::PlainText);
    layout->addWidget(developerVideoStatus_);
    const auto save=[this,boxes,read,force]{
        if(!developerApproved_)return;
        developerVideoOptions_=read();
        QSettings preferences;
        for(auto *box:boxes)preferences.setValue("developer/"+box->objectName(),box->currentData());
        preferences.setValue("developer/forceHdr",force->isChecked());
        applyVideoOutputPreferences();
    };
    for(auto *box:boxes)connect(box,&QComboBox::currentIndexChanged,this,save);
    connect(force,&QCheckBox::toggled,this,save);
    connect(reset,&QPushButton::clicked,this,[boxes,save,force]{
        for(auto *box:boxes){
            const QSignalBlocker block(box);
            box->setCurrentIndex(0);
        }
        const QSignalBlocker block(force);force->setChecked(false);
        save();
    });
}

void MainWindow::updateDeveloperVideoStatus(const PlaybackState &state) {
    if(!developerApproved_)return;
    const auto show=[](const QString &value){return value.isEmpty()?QString("—"):value;};
    const auto decoder=state.videoDecoder.isEmpty()?state.videoCodec:state.videoDecoder;
    auto text=QString("디코더 요청 %1\n실제 %2 · %3\n입력 %4 / %5\n출력 요청 %6 · 렌더러 %7 / %8\nDolby Vision 요청 %9")
        .arg(show(state.videoDecoderRequest),show(decoder),show(state.hardwareDecoder),
             show(state.transferFunction),show(state.colorPrimaries),show(state.videoColorRequest),
             show(state.outputTransferFunction),show(state.outputPrimaries),show(state.dolbyVisionRequest));
    if(state.dolbyVisionProfile)text+=QString(" · 원본 프로필 %1").arg(*state.dolbyVisionProfile);
    if(state.videoColorForced)text+="\nSDR 화면 HDR 렌더링 강제";
    if(!state.videoOverrideStatus.isEmpty())text+="\n"+state.videoOverrideStatus;
    if(developerVideoStatus_->text()!=text)developerVideoStatus_->setText(text);
}

}
