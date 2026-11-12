#include "playback.h"
#include <cmath>
#include "motion_options.h"
#include <QSettings>
#include <QSet>
#include <QHash>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QTimer>
#include <QThread>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QUrl>
#include <QRegularExpression>
#include <vlc/vlc.h>
#include <mpv/client.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <atomic>
#include <optional>
#include <iterator>
#include <limits>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace luma {
int discActionCode(DiscAction action){
    switch(action){case DiscAction::Activate:return libvlc_navigate_activate;case DiscAction::Up:return libvlc_navigate_up;case DiscAction::Down:return libvlc_navigate_down;case DiscAction::Left:return libvlc_navigate_left;case DiscAction::Right:return libvlc_navigate_right;case DiscAction::Popup:return libvlc_navigate_popup;}return -1;
}
namespace {
bool validSourceText(const QString &source) {
    return !source.trimmed().isEmpty() && source.size() <= 32768 &&
        std::none_of(source.begin(), source.end(), [](QChar c) { return c.category() == QChar::Other_Control; });
}
bool driveRoot(const QString &source) {
    static const QRegularExpression root("^[A-Za-z]:[/\\\\]?$");
    return root.match(source).hasMatch();
}

QString videoRangeFromTransfer(const QString &transfer) {
    if (transfer == "pq" || transfer == "hlg")
        return transfer;
    static const QSet<QString> sdrTransfers{"bt.1886", "srgb", "gamma1.8", "gamma2.0",
        "gamma2.2", "gamma2.4", "gamma2.6", "gamma2.8"};
    return sdrTransfers.contains(transfer) ? QStringLiteral("sdr") : QStringLiteral("unknown");
}

bool lanBridgeUrl(const QUrl &url) {
    const int port = url.port();
    return url.isValid() && url.scheme() == "http" && port > 0 && port <= 65535 &&
        url.authority(QUrl::FullyEncoded) == "127.0.0.1:" + QString::number(port) &&
        !url.hasQuery() && !url.hasFragment();
}

bool bypassProxyForLanBridge() {
    // libmpv's HTTP transport reads the proxy environment for media and subtitles.
    auto exclusions = qgetenv("no_proxy");
    if (exclusions.isEmpty())
        exclusions = qgetenv("NO_PROXY");
    if (exclusions == "*")
        return true;
    for (const auto &entry : exclusions.split(',')) {
        if (entry == "127.0.0.1")
            return true;
    }
    if (!exclusions.isEmpty())
        exclusions += ',';
    return qputenv("no_proxy", exclusions + "127.0.0.1");
}

QByteArray lanDemuxer(const QString &mimeType) {
    static const QHash<QString,QByteArray> formats{
        {"video/x-matroska", "demuxer=+mkv"},
        {"video/webm", "demuxer=+mkv"},
        {"video/mp4", "demuxer=+lavf,demuxer-lavf-format=mov"},
        {"video/quicktime", "demuxer=+lavf,demuxer-lavf-format=mov"},
        {"audio/mp4", "demuxer=+lavf,demuxer-lavf-format=mov"},
        {"video/mp2t", "demuxer=+lavf,demuxer-lavf-format=mpegts"},
        {"video/x-msvideo", "demuxer=+lavf,demuxer-lavf-format=avi"},
        {"audio/flac", "demuxer=+lavf,demuxer-lavf-format=flac"},
        {"audio/mpeg", "demuxer=+lavf,demuxer-lavf-format=mp3"},
        {"audio/wav", "demuxer=+lavf,demuxer-lavf-format=wav"}
    };
    return formats.value(mimeType);
}
}

bool validLanMediaSource(const LanMediaSource &source) {
    static const QRegularExpression mediaPath("\\A(/[A-Za-z0-9_-]{21}[AQgw])/media\\z");
    static const QRegularExpression subtitlePath("\\A(/[A-Za-z0-9_-]{21}[AQgw])/subtitle/[A-Za-z0-9_-]{1,64}\\.vtt\\z");
    const auto media = mediaPath.match(source.url.path(QUrl::FullyEncoded));
    if (!lanBridgeUrl(source.url) || !media.hasMatch() || lanDemuxer(source.mimeType).isEmpty() || source.subtitles.size() > 8)
        return false;
    for (const auto &[url, label] : source.subtitles) {
        const auto subtitle = subtitlePath.match(url.path(QUrl::FullyEncoded));
        if (!lanBridgeUrl(url) || url.port() != source.url.port() || !subtitle.hasMatch() ||
            subtitle.captured(1) != media.captured(1) || label.size() > 128)
            return false;
    }
    return true;
}

std::optional<QString> normalizedPlaybackSource(const QString &source, bool disc) {
    if (!validSourceText(source))
        return std::nullopt;

    const auto nativePath = QDir::fromNativeSeparators(source);
    const QUrl url(source, QUrl::StrictMode);
    const auto scheme = url.scheme();
    const bool absolute = QDir::isAbsolutePath(nativePath);
    QString path = nativePath;
    if (!absolute && scheme.isEmpty() && source.contains("://"))
        return std::nullopt;
    if (!absolute && !driveRoot(nativePath) && !scheme.isEmpty()) {
        if (scheme == "file" || (disc && scheme == "bluray")) {
            if (!url.isValid() || url.hasQuery() || url.hasFragment() || !url.userInfo().isEmpty() || url.port() >= 0)
                return std::nullopt;
            auto local = url;
            local.setScheme("file");
            path = local.toLocalFile();
            if (!QDir::isAbsolutePath(path) && !driveRoot(path))
                return std::nullopt;
        } else {
            static const QSet<QString> networkSchemes{"http", "https", "rtsp", "rtsps", "rtmp", "rtmps", "srt", "udp"};
            if (!disc && url.isValid() && networkSchemes.contains(scheme) && !url.host().isEmpty())
                return url.toString(QUrl::FullyEncoded);
#ifndef Q_OS_WIN
            if (!source.contains("://") && QFileInfo::exists(nativePath))
                return QFileInfo(nativePath).absoluteFilePath();
#endif
            return std::nullopt;
        }
    }
    path = QDir::fromNativeSeparators(path);
    if (!validSourceText(path))
        return std::nullopt;
#ifdef Q_OS_WIN
    if (path.startsWith("//./") || path.startsWith("//?/"))
        return std::nullopt;
    if (driveRoot(path) && path.size() == 2)
        path += '/';
#endif
    return QFileInfo(path).absoluteFilePath();
}

bool isDiscSource(const QString &source) {
    const auto path = normalizedPlaybackSource(source, true);
    return path && (QUrl(source).scheme() == "bluray" || driveRoot(source) || driveRoot(*path) ||
                    QFileInfo::exists(QDir(*path).filePath("BDMV/index.bdmv")));
}
bool discTimelineReady(qint64 liveDurationMs,qint64 titleDurationMs,bool navigationTitle){
    if(navigationTitle || liveDurationMs<=0 || titleDurationMs<=0)return false;
    const auto difference=liveDurationMs>titleDurationMs?liveDurationMs-titleDurationMs:titleDurationMs-liveDurationMs;
    return difference<=2000;
}
namespace {
QString discFailureDetail(const QString &message){
    const auto has=[&](const char *text){return message.contains(QString::fromLatin1(text),Qt::CaseInsensitive);};
    const auto retry=QStringLiteral(" 디스크를 다시 열어 주세요. 계속되면 개발자 모드의 진단을 확인하세요.");
    if(has("Missing AACS configuration file"))return QStringLiteral("AACS 설정 파일을 찾거나 열 수 없습니다. 현재 사용자 계정의 AACS 설정을 확인한 뒤 디스크를 다시 열어 주세요.");
    if(has("needs a library for AACS decoding"))return QStringLiteral("AACS 읽기 라이브러리를 불러오지 못했습니다. 설치 파일과 개발자 모드의 진단을 확인하세요.");
    if(has("No valid processing key found in AACS config file"))return QStringLiteral("이 디스크에 사용할 수 있는 AACS 처리 키를 확인하지 못했습니다. 개발자 모드의 진단을 확인하세요.");
    if(has("No valid host certificate found in AACS config file"))return QStringLiteral("유효한 AACS 호스트 인증서를 확인하지 못했습니다. 개발자 모드의 진단을 확인하세요.");
    if(has("AACS Host certificate revoked"))return QStringLiteral("AACS 호스트 인증서가 폐기되어 이번 재생을 시작하지 못했습니다. 개발자 모드의 진단을 확인하세요.");
    if(has("AACS MMC failed"))return QStringLiteral("AACS 처리 중 디스크 드라이브와 통신하지 못했습니다.")+retry;
    if(has("needs a library for BD+ decoding"))return QStringLiteral("BD+ 읽기 라이브러리를 불러오지 못했습니다. 설치 파일과 개발자 모드의 진단을 확인하세요.");
    if(has("Your system BD+ decoding library does not work"))return QStringLiteral("이 디스크의 BD+ 처리를 시작하지 못했습니다.")+retry;
    return {};
}
QString videoOutputLossReason(const QString &message){
    if(!message.contains("SwapChain Present failed",Qt::CaseInsensitive))return {};
    static const QRegularExpression hresult(QStringLiteral("\\bhr\\s*=\\s*(0x887a000[57])\\b"),QRegularExpression::CaseInsensitiveOption);
    const auto match=hresult.match(message);if(!match.hasMatch())return {};
    const auto code=match.captured(1).toLower();
    const auto name=code=="0x887a0005"?QStringLiteral("DXGI_ERROR_DEVICE_REMOVED"):QStringLiteral("DXGI_ERROR_DEVICE_RESET");
    return QStringLiteral("영상 출력 장치를 사용할 수 없어 영상 출력을 다시 준비해야 합니다. (%1, %2)").arg(name,code);
}

QString redactNetworkSources(QString message) {
    static const QRegularExpression urls(R"(\b(?:https?|rtsps?|rtmps?|srt|udp)://[^\s<>]+)",
                                         QRegularExpression::CaseInsensitiveOption);
    return message.replace(urls, QStringLiteral("[media URL]"));
}

const char *hardwareDecoder(VideoOverrides::Hardware hardware, bool copyBack) {
    switch (hardware) {
    case VideoOverrides::Hardware::Software: return "no";
#ifdef Q_OS_WIN
    case VideoOverrides::Hardware::D3D11: return copyBack ? "d3d11va-copy" : "d3d11va";
    case VideoOverrides::Hardware::DXVA2: return copyBack ? "dxva2-copy" : "dxva2";
    case VideoOverrides::Hardware::D3D11Copy: return "d3d11va-copy";
    case VideoOverrides::Hardware::VulkanCopy: return "vulkan-copy";
#elif defined(Q_OS_LINUX)
    case VideoOverrides::Hardware::VAAPI: return copyBack ? "vaapi-copy" : "vaapi";
#endif
    default: return copyBack ? "auto-copy" : "auto-safe";
    }
}

const char *fileHardwareDecoder(VideoOverrides::Hardware hardware, bool copyBack) {
    // gpu-next requires the copy-back variant for the legacy DXVA2 API.
    return hardwareDecoder(hardware,copyBack || hardware==VideoOverrides::Hardware::DXVA2);
}

const char *preferredDecoder(VideoOverrides::Codec codec) {
    switch (codec) {
    case VideoOverrides::Codec::H264: return "h264";
    case VideoOverrides::Codec::HEVC: return "hevc";
    case VideoOverrides::Codec::VP9: return "vp9";
    case VideoOverrides::Codec::AV1: return "av1";
    case VideoOverrides::Codec::Dav1d: return "libdav1d";
    default: return "";
    }
}

const char *dolbyFilter(VideoOverrides::DolbyVision mode) {
    switch (mode) {
    case VideoOverrides::DolbyVision::Apply: return "@luma_dolby:format=dolbyvision=yes";
    case VideoOverrides::DolbyVision::Ignore: return "@luma_dolby:format=dolbyvision=no";
    case VideoOverrides::DolbyVision::BaseLayer: return "@luma_dolby:format=dolbyvision=yes:enhancement-layer=no";
    default: return "";
    }
}
}
struct PlaybackEngine::Impl {
    PlaybackEngine *owner;
    Kind kind;
    QLibrary lib;
    QString error;
    QString discError;
    PlaybackState state;
    QTimer timer;
    bool ready=false, errorSent=false, performanceMetrics=false;
    bool videoOutputActive=false,videoOutputLossSent=false;
    bool managedSubtitle=false;
    MotionOptions motion;
    VideoOverrides nextVideo, activeVideo;
    AudioOutputOptions nextAudio;
    QStringList videoRejections;
    bool discBitstream=false, nightApplied=false;
    bool audioFallbackRetried=false;
    QElapsedTimer audioFallbackWait;
    QByteArray fileSource, fileOptions;
    QVector<QPair<QUrl,QString>> lanSubtitles;
    bool lanSource=false;
    QString motionMetadata;
    bool motionApplied=false;
    bool dolbyApplied=false;
    bool fileLoaded=false;
    bool seekReady=false;
    std::optional<qint64> deferredSeek;
    bool discSoftware=true;
    bool nextDiscSoftware=false,discAllocationFailed=false;
    QByteArray discHardwareOverride;
    QElapsedTimer discFrameProgress;
    int lastDiscFrame=-1,audioAtLastDiscFrame=0;
    int metadataTitle=-2,metadataAudio=-2;
    int chapterOffsetsTitle=-2,chapterOffsetsCount=-1;
    bool physicalBluRay=false;
    QSet<int> navigationTitles;
    QHash<int,qint64> titleDurations;
    bool titleMetadataReady=false;
    qint64 preparedDiscDuration=0;
    int preparedNativeTitle=-1;
    bool preparedPauseRequested=false;
    QElapsedTimer preparedTitleWait;
    int managedMpvSubtitle=-1;
    std::atomic<quint64> generation{0};
    std::atomic_bool stopping{false};
    bool destroying=false;
    libvlc_instance_t *vlc=nullptr;
    libvlc_media_player_t *player=nullptr;
    libvlc_media_t *media=nullptr;
    mpv_handle *mpv=nullptr;
#ifdef Q_OS_WIN
    DLL_DIRECTORY_COOKIE directoryCookie=nullptr;
#endif
#define VF(n) decltype(&::n) n=nullptr
    VF(libvlc_new); VF(libvlc_release); VF(libvlc_log_set); VF(libvlc_log_unset);
    VF(libvlc_media_new_location); VF(libvlc_media_add_option); VF(libvlc_media_release);
    VF(libvlc_media_player_new); VF(libvlc_media_player_release); VF(libvlc_media_player_set_media);
    VF(libvlc_media_player_play); VF(libvlc_media_player_stop); VF(libvlc_media_player_pause);
    VF(libvlc_media_player_set_pause);
    VF(libvlc_media_player_get_time); VF(libvlc_media_player_get_length); VF(libvlc_media_player_set_time);
    VF(libvlc_media_player_get_state); VF(libvlc_media_player_is_playing); VF(libvlc_media_player_set_rate);
    VF(libvlc_audio_set_volume); VF(libvlc_audio_get_track_description); VF(libvlc_audio_set_track);
    VF(libvlc_video_get_spu_description); VF(libvlc_video_set_spu); VF(libvlc_track_description_list_release);
    VF(libvlc_media_player_get_chapter_count); VF(libvlc_media_player_set_chapter);
    VF(libvlc_media_player_get_full_chapter_descriptions); VF(libvlc_chapter_descriptions_release);
    VF(libvlc_media_player_get_title); VF(libvlc_media_player_get_chapter);
    VF(libvlc_media_player_get_full_title_descriptions); VF(libvlc_title_descriptions_release);
    VF(libvlc_media_player_set_title); VF(libvlc_media_player_navigate);
    VF(libvlc_video_set_key_input); VF(libvlc_video_set_mouse_input); VF(libvlc_media_player_add_slave);
    VF(libvlc_media_tracks_get); VF(libvlc_media_tracks_release);
    VF(libvlc_media_get_stats); VF(libvlc_audio_get_track); VF(libvlc_video_get_spu);
#ifdef Q_OS_WIN
    VF(libvlc_media_player_set_hwnd);
#elif defined(Q_OS_LINUX)
    VF(libvlc_media_player_set_xwindow);
#endif
#undef VF
#define MF(n) decltype(&::n) n=nullptr
    MF(mpv_create); MF(mpv_initialize); MF(mpv_terminate_destroy); MF(mpv_set_option_string);
    MF(mpv_set_option); MF(mpv_command); MF(mpv_command_async); MF(mpv_get_property); MF(mpv_get_property_string);
    MF(mpv_set_property); MF(mpv_set_property_string); MF(mpv_wait_event); MF(mpv_free);
    MF(mpv_free_node_contents); MF(mpv_request_log_messages); MF(mpv_error_string);
#undef MF
    explicit Impl(PlaybackEngine *o,Kind k):owner(o),kind(k){}
    bool initDisc(bool bitstream) {
        QList<const char *> options{"--no-video-title-show","--no-osd","--no-snapshot-preview","--no-lua","--verbose=1"};
        options.append(bitstream ? "--spdif" : "--no-spdif");
#ifdef Q_OS_WIN
        options.append(bitstream ? "--mmdevice-passthrough=2" : "--mmdevice-passthrough=0");
        if(bitstream)options.append("--aout=mmdevice");
#endif
        auto *instance=libvlc_new(int(options.size()),options.data());
        if(!instance)return false;
        auto *replacement=libvlc_media_player_new(instance);
        if(!replacement){libvlc_release(instance);return false;}
        if(player)libvlc_media_player_release(player);
        if(media){libvlc_media_release(media);media=nullptr;}
        if(vlc){libvlc_log_unset(vlc);libvlc_release(vlc);}
        vlc=instance;
        player=replacement;
        discBitstream=bitstream;
        libvlc_log_set(vlc,&Impl::log,this);
        libvlc_video_set_key_input(player,0);
        libvlc_video_set_mouse_input(player,1);
        return true;
    }
    ~Impl(){
        lib.unload();
#ifdef Q_OS_WIN
        if(directoryCookie)RemoveDllDirectory(directoryCookie);
#endif
    }
    template<class T> bool resolve(T &f,const char *name) { f=reinterpret_cast<T>(lib.resolve(name)); if(!f) error=QString("Media runtime symbol missing: %1").arg(name); return f!=nullptr; }
    void fail(const QString &text) {
        const bool detailed=kind==Kind::Disc && !discError.isEmpty();
        const auto message=detailed?discError:(kind==Kind::Disc && errorSent?error:text);
        const bool changed=error!=message;
        error=message;state.failed=true;state.playing=false;state.timelineReady=false;
        // 첫 상세 원인은 세션 종료까지 유지하고, 늦게 도착하면 일반 오류를 승격한다.
        // Keep the first detailed reason for this session and upgrade a prior generic error.
        if(!errorSent || (detailed && changed)){errorSent=true;emit owner->errorOccurred(message);}
    }
    int command(const QList<QByteArray> &args, bool asynchronous=false) {
        if(!mpv) return MPV_ERROR_UNINITIALIZED;
        QVector<const char *> pointers; for(const auto &s:args) pointers.append(s.constData()); pointers.append(nullptr);
        const int code=asynchronous?mpv_command_async(mpv,0,pointers.data()):mpv_command(mpv,pointers.data());
        if(code<0) emit owner->logMessage(QString("mpv: %1").arg(QString::fromUtf8(mpv_error_string(code))));
        return code;
    }
    QString prop(const char *key) { char *s=mpv_get_property_string(mpv,key); if(!s)return {}; const auto result=QString::fromUtf8(s); mpv_free(s); return result; }
    void applyVideoColor() {
        using Color=VideoOverrides::Color;
        auto color=activeVideo.color;
        const bool forced=activeVideo.forceHdr;
        if(forced && (color==Color::Auto || color==Color::SDR))color=Color::HDR10;
        const char *transfer=color==Color::SDR?"gamma2.2":color==Color::HDR10?"pq":color==Color::HLG?"hlg":"auto";
        const char *primaries=color==Color::SDR?"bt.709":color==Color::Auto?"auto":"bt.2020";
        auto rejected=videoRejections;
        const auto set=[&](const char *name,const char *value,bool required=true){
            if(mpv_set_property_string(mpv,name,value)<0 && required)rejected.append(QString::fromLatin1(name));
        };
        set("options/target-trc",transfer);
        set("options/target-prim",primaries);
        set("options/target-colorspace-hint",color==Color::Auto?"auto":"yes",color!=Color::Auto);
        set("options/target-colorspace-hint-strict",forced?"no":"yes",forced);
        set("options/inverse-tone-mapping",forced?"yes":"no",forced);
        state.videoColorRequest=QString::fromLatin1(transfer);
        state.videoColorForced=forced;
        const auto error=rejected.isEmpty()?QString{}:QString("재생 엔진이 거부한 설정 · ")+rejected.join(", ");
        if(!error.isEmpty() && error!=state.videoOverrideStatus)emit owner->logMessage(error);
        state.videoOverrideStatus=error;
    }
    void applyFileAudio() {
        using Profile=AudioOutputOptions::Profile;
        QStringList rejected;
        const auto set=[&](const char *name,const char *value){
            if(mpv_set_property_string(mpv,name,value)<0)rejected.append(QString::fromLatin1(name));
        };
        if(nightApplied){command({"af","remove","@luma_night"});nightApplied=false;}
#ifdef Q_OS_WIN
        set("options/ao",nextAudio.bitstream?"wasapi":"");
#endif
        set("options/audio-spdif",nextAudio.bitstream?"ac3,eac3,dts,dts-hd,truehd":"");
        const auto profile=nextAudio.bitstream?Profile::Auto:nextAudio.profile;
        const char *channels=profile==Profile::Stereo?"stereo":profile==Profile::Surround51?"5.1,stereo":
                             profile==Profile::Surround71?"7.1,5.1,stereo":"auto-safe";
        set("options/audio-channels",channels);
        if(profile==Profile::Night){
            nightApplied=command({"af","add","@luma_night:lavfi=[acompressor=threshold=0.125:ratio=3:attack=20:release=250]"})>=0;
            if(!nightApplied)rejected.append("야간 모드");
        }
        if(!rejected.isEmpty())state.audioSettingsError="재생 엔진이 거부한 설정 · "+rejected.join(", ");
        mpv_request_log_messages(mpv,nextAudio.bitstream?"v":"warn");
    }
    void finishAudioFallback() {
        if(!audioFallbackWait.isValid())return;
        if(!state.audioFormat.isEmpty() && !state.audioOutput.isEmpty() && state.audioOutput!="null"){
            audioFallbackWait.invalidate();
            return;
        }
        if(audioFallbackWait.elapsed()<1000)return;
        audioFallbackWait.invalidate();
        audioFallbackRetried=true;
        if(mpv_set_property_string(mpv,"options/audio-spdif","")<0){
            state.audioSettingsError="PCM 오디오 전환을 시작할 수 없습니다.";
            return;
        }
        // Some WASAPI devices reject compressed audio and libmpv's PCM decoder
        // then waits for another packet. Reload once with PCM already selected.
        fileLoaded=false;
        seekReady=false;
        if(state.positionMs>0)deferredSeek=state.positionMs;
        if(loadFile()<0)
            state.audioSettingsError="PCM 오디오로 미디어를 다시 열 수 없습니다.";
    }
    int loadFile() {
        if (fileOptions.isEmpty())
            return command({"loadfile",fileSource,"replace"});
        return command({"loadfile",fileSource,"replace","-1",fileOptions});
    }
    double number(const char *key) { double value=0; mpv_get_property(mpv,key,MPV_FORMAT_DOUBLE,&value); return value; }
    std::optional<double> metric(const char *key) {
        double value=0;
        if(mpv_get_property(mpv,key,MPV_FORMAT_DOUBLE,&value)>=0 && std::isfinite(value))return value;
        return {};
    }
    QVector<MediaItem> descriptions(libvlc_track_description_t *list) {
        QVector<MediaItem> items;
        for(auto *item=list;item;item=item->p_next) items.append({item->i_id,QString::fromUtf8(item->psz_name?item->psz_name:"")});
        if(list) libvlc_track_description_list_release(list);
        return items;
    }
    static void log(void *opaque,int level,const libvlc_log_t *,const char *fmt,va_list args) {
        if(level<LIBVLC_NOTICE) return;
        char buffer[2048]; vsnprintf(buffer,sizeof(buffer),fmt,args);
        auto *self=static_cast<Impl *>(opaque);
        const QString msg=QString::fromUtf8(buffer);
        const auto generation=self->generation.load();
        QMetaObject::invokeMethod(self->owner,[o=self->owner,msg,generation]{
            if(generation!=o->d->generation.load() || o->d->stopping.load())return;emit o->logMessage(msg);
            if(msg.contains("Using D3D11VA"))o->d->state.hardwareDecoder="D3D11VA";
            else if(msg.contains("Using DXVA2"))o->d->state.hardwareDecoder="DXVA2";
            if(msg.contains("hardware acceleration picture allocation failed",Qt::CaseInsensitive))o->d->discAllocationFailed=true;
            const auto outputLoss=videoOutputLossReason(msg);
            if(!outputLoss.isEmpty() && o->d->videoOutputActive && !o->d->state.failed && !o->d->videoOutputLossSent){
                // 신호 수신자가 복구를 결정한다. 여기서는 전송 상태를 실패로 바꾸지 않는다.
                // The receiver owns recovery; device-loss notification is not a fatal transport error.
                o->d->videoOutputLossSent=true;emit o->videoOutputLost(outputLoss);
            }
            const auto detail=discFailureDetail(msg);
            if(!detail.isEmpty()){
                if(o->d->discError.isEmpty())o->d->discError=detail;
                o->d->fail(detail);
            }else if(msg.contains("Your input can't be opened") || msg.startsWith("VLC is unable to open"))
                o->d->fail("Blu-ray 재생을 시작할 수 없습니다. 디스크를 다시 열어 주세요. 계속되면 개발자 모드의 진단을 확인하세요.");
        },Qt::QueuedConnection);
    }
};
PlaybackEngine::PlaybackEngine(Kind kind,const QString &runtimeRoot,QObject *parent):QObject(parent),d(std::make_unique<Impl>(this,kind)) {
    const auto root=runtimeRoot.isEmpty()?QCoreApplication::applicationDirPath()+"/runtime":runtimeRoot;
#ifdef Q_OS_WIN
    const auto file=QDir(root).filePath(kind==Kind::Disc?"vlc/libvlc.dll":"mpv/libmpv-2.dll");
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    const auto libraryDirectory=QFileInfo(file).absolutePath();
    d->directoryCookie=AddDllDirectory(reinterpret_cast<LPCWSTR>(libraryDirectory.utf16()));
#else
    const auto file=runtimeRoot.isEmpty()?(kind==Kind::Disc?"libvlc.so.5":"libmpv.so.2"):QDir(root).filePath("missing-runtime.so");
#endif
    d->lib.setFileName(file);
    // Release each native instance normally, but keep runtime code mapped: its
    // plugins/global TLS may outlive an instance, including failed initialization.
    d->lib.setLoadHints(QLibrary::PreventUnloadHint);
    if(!d->lib.load()){d->error=QString("%1 runtime을 불러올 수 없습니다: %2").arg(name(),d->lib.errorString());return;}
    bool ok=true;
    if(kind==Kind::Disc){
#define LOAD(n) ok=d->resolve(d->n,#n)&&ok
        LOAD(libvlc_new); LOAD(libvlc_release); LOAD(libvlc_log_set); LOAD(libvlc_log_unset);
        LOAD(libvlc_media_new_location); LOAD(libvlc_media_add_option); LOAD(libvlc_media_release);
        LOAD(libvlc_media_player_new); LOAD(libvlc_media_player_release); LOAD(libvlc_media_player_set_media);
        LOAD(libvlc_media_player_play); LOAD(libvlc_media_player_stop); LOAD(libvlc_media_player_pause);
        LOAD(libvlc_media_player_set_pause);
        LOAD(libvlc_media_player_get_time); LOAD(libvlc_media_player_get_length); LOAD(libvlc_media_player_set_time);
        LOAD(libvlc_media_player_get_state); LOAD(libvlc_media_player_is_playing); LOAD(libvlc_media_player_set_rate);
        LOAD(libvlc_audio_set_volume); LOAD(libvlc_audio_get_track_description); LOAD(libvlc_audio_set_track);
        LOAD(libvlc_video_get_spu_description); LOAD(libvlc_video_set_spu); LOAD(libvlc_track_description_list_release);
        LOAD(libvlc_media_player_get_chapter_count); LOAD(libvlc_media_player_set_chapter);
        LOAD(libvlc_media_player_get_title); LOAD(libvlc_media_player_get_chapter);
        LOAD(libvlc_media_player_get_full_title_descriptions); LOAD(libvlc_title_descriptions_release);
        LOAD(libvlc_media_player_set_title); LOAD(libvlc_media_player_navigate);
        LOAD(libvlc_video_set_key_input); LOAD(libvlc_video_set_mouse_input); LOAD(libvlc_media_player_add_slave);
        LOAD(libvlc_media_tracks_get); LOAD(libvlc_media_tracks_release);
        LOAD(libvlc_media_get_stats); LOAD(libvlc_audio_get_track); LOAD(libvlc_video_get_spu);
#ifdef Q_OS_WIN
        LOAD(libvlc_media_player_set_hwnd);
#elif defined(Q_OS_LINUX)
        LOAD(libvlc_media_player_set_xwindow);
#endif
#undef LOAD
        if(!ok)return;
#define LOAD_OPTIONAL(n) d->n=reinterpret_cast<decltype(d->n)>(d->lib.resolve(#n))
        LOAD_OPTIONAL(libvlc_media_player_get_full_chapter_descriptions);
        LOAD_OPTIONAL(libvlc_chapter_descriptions_release);
#undef LOAD_OPTIONAL
        const auto decoder=qEnvironmentVariable("LUMA_DISC_DECODER");
        if(decoder=="software")d->discHardwareOverride=":avcodec-hw=none";
        else if(decoder=="hardware")d->discHardwareOverride=":avcodec-hw=any";
        else if(decoder=="dxva2")d->discHardwareOverride=":avcodec-hw=dxva2";
        if(!d->initDisc(false)){d->error="libVLC initialization failed";return;}
    }else{
#define LOAD(n) ok=d->resolve(d->n,#n)&&ok
        LOAD(mpv_create); LOAD(mpv_initialize); LOAD(mpv_terminate_destroy); LOAD(mpv_set_option_string); LOAD(mpv_set_option);
        LOAD(mpv_command); LOAD(mpv_command_async); LOAD(mpv_get_property); LOAD(mpv_get_property_string); LOAD(mpv_set_property); LOAD(mpv_set_property_string);
        LOAD(mpv_wait_event); LOAD(mpv_free); LOAD(mpv_free_node_contents); LOAD(mpv_request_log_messages); LOAD(mpv_error_string);
#undef LOAD
        if(!ok)return;
        d->mpv=d->mpv_create(); if(!d->mpv){d->error="libmpv allocation failed";return;}
        for(const auto &[key,value]:QList<QPair<QByteArray,QByteArray>>{{"config","no"},{"load-scripts","no"},{"load-unsafe-playlists","no"},{"terminal","no"},{"input-default-bindings","no"},{"input-builtin-drag-and-drop","no"},{"drag-and-drop","no"},{"input-vo-keyboard","no"},{"osc","no"},{"osd-level","0"},{"idle","yes"},{"keep-open","yes"},{"hwdec","auto-safe"},{"vo","gpu-next"}})
            d->mpv_set_option_string(d->mpv,key.constData(),value.constData());
#ifdef Q_OS_WIN
        d->mpv_set_option_string(d->mpv,"gpu-api","d3d11");
        const auto adapter=QSettings().value("video/adapter").toString().toUtf8();
        if(!adapter.isEmpty())d->mpv_set_option_string(d->mpv,"d3d11-adapter",adapter.constData());
#endif
        d->mpv_set_option_string(d->mpv,"target-colorspace-hint","auto");
        d->mpv_set_option_string(d->mpv,"hwdec-extra-frames","16");
        const auto motionMode=QSettings().value("motion/mode","off").toString();
        d->motion=normalizeMotionOptions({motionMode=="display"?MotionMode::DisplayBlend:motionMode=="optical"?MotionMode::OpticalFlow:MotionMode::Off,QSettings().value("motion/fps",120).toInt()});
        if(d->mpv_initialize(d->mpv)<0){d->error="libmpv initialization failed";return;}
        d->mpv_request_log_messages(d->mpv,"warn");
    }
    d->ready=true; d->timer.setInterval(200); connect(&d->timer,&QTimer::timeout,this,&PlaybackEngine::poll); d->timer.start();
}
PlaybackEngine::~PlaybackEngine(){
    d->destroying=true;d->timer.stop();stop();
    if(d->player)d->libvlc_media_player_release(d->player);
    if(d->media)d->libvlc_media_release(d->media);
    if(d->vlc){d->libvlc_log_unset(d->vlc);d->libvlc_release(d->vlc);}
    if(d->mpv)d->mpv_terminate_destroy(d->mpv);
}
bool PlaybackEngine::available()const{return d->ready;}
QString PlaybackEngine::lastError()const{return d->error;}
QString PlaybackEngine::name()const{return d->kind==Kind::Disc?"libVLC · Blu-ray":"libmpv · GPU Next";}
const PlaybackState &PlaybackEngine::state()const{return d->state;}
void PlaybackEngine::useSoftwareDiscDecoderOnNextOpen(){d->nextDiscSoftware=true;}
bool PlaybackEngine::openPreparedDisc(const QString &source,WId surface,qint64 durationMs){
    if(d->kind!=Kind::Disc || durationMs<=0 || durationMs>24LL*60*60*1000){d->fail("준비한 디스크 타이틀의 길이가 올바르지 않습니다.");return false;}
    return openInternal(source,surface,durationMs);
}
bool PlaybackEngine::open(const QString &source,WId surface){return openInternal(source,surface,0);}
bool PlaybackEngine::openLanMedia(const LanMediaSource &source,WId surface){
    if(d->kind!=Kind::File || !validLanMediaSource(source))return false;
    if(!bypassProxyForLanBridge()){d->error="로컬 재생 연결을 준비할 수 없습니다.";return false;}
    return openInternal(source.url.toString(QUrl::FullyEncoded),surface,0,&source);
}
void PlaybackEngine::setVideoOverrides(VideoOverrides options){d->nextVideo=options;}
void PlaybackEngine::setVideoColorOutput(VideoOverrides::Color color,bool forceHdr){
    if(!d->mpv || !available() || !d->videoOutputActive)return;
    if(d->activeVideo.color==color && d->activeVideo.forceHdr==forceHdr)return;
    d->activeVideo.color=color;
    d->activeVideo.forceHdr=forceHdr;
    d->applyVideoColor();
    emit updated();
}
void PlaybackEngine::setAudioOutputOptions(AudioOutputOptions options){d->nextAudio=options;}
bool PlaybackEngine::openInternal(const QString &source,WId surface,qint64 preparedDiscDurationMs,const LanMediaSource *lanSource){
    if(!available()){d->fail(d->error);return false;}
    const auto input=normalizedPlaybackSource(source,d->kind==Kind::Disc);
    if(!input){d->error="지원하지 않는 미디어 주소입니다.";return false;}
    stop();++d->generation;d->state={};d->errorSent=false;d->error.clear();d->discError.clear();d->managedSubtitle=false;d->managedMpvSubtitle=-1;
    d->lanSource=lanSource!=nullptr;
    d->metadataTitle=d->metadataAudio=d->chapterOffsetsTitle=-2;
    d->chapterOffsetsCount=-1;
    d->fileLoaded=false;d->seekReady=false;d->deferredSeek.reset();d->navigationTitles.clear();d->titleDurations.clear();d->titleMetadataReady=false;
    d->preparedDiscDuration=preparedDiscDurationMs;d->preparedNativeTitle=-1;d->preparedPauseRequested=false;d->state.discMenus=preparedDiscDurationMs==0;
    d->activeVideo=d->nextVideo;
    d->state.audioBitstreamRequested=d->nextAudio.bitstream;
    if(preparedDiscDurationMs>0)d->preparedTitleWait.start();else d->preparedTitleWait.invalidate();
    if(d->kind==Kind::Disc){
        if(d->discBitstream!=d->nextAudio.bitstream && !d->initDisc(d->nextAudio.bitstream)){
            d->fail("Blu-ray 오디오 출력을 준비할 수 없습니다. 오디오 출력 설정을 확인해 주세요.");
            return false;
        }
        d->state.audioVolumeExternal=d->discBitstream;
        // HDMV can reuse video output across First Play, menus and the feature.
        // Start menu sessions in compatibility mode so a failed GPU surface
        // allocation cannot force a reopen that loses the authored navigation state.
        QByteArray hardware=":avcodec-hw=none";
#ifdef Q_OS_WIN
        if(!d->state.discMenus)hardware=":avcodec-hw=any";
#endif
        if(!d->discHardwareOverride.isEmpty())hardware=d->discHardwareOverride;
        if(d->activeVideo.hardware==VideoOverrides::Hardware::Software)
            hardware=":avcodec-hw=none";
        else if(d->activeVideo.hardware!=VideoOverrides::Hardware::Auto &&
                d->activeVideo.hardware!=VideoOverrides::Hardware::VulkanCopy){
            const auto discHardware=d->activeVideo.hardware==VideoOverrides::Hardware::D3D11Copy
                ?VideoOverrides::Hardware::D3D11:d->activeVideo.hardware;
            const QByteArray decoder=hardwareDecoder(discHardware,false);
            if(decoder!="auto-safe")hardware=":avcodec-hw="+decoder;
        }
        d->discSoftware=std::exchange(d->nextDiscSoftware,false) || hardware.endsWith("=none");
        if(d->discSoftware)hardware=":avcodec-hw=none";
        d->state.videoDecoderRequest=QString::fromLatin1(hardware.mid(sizeof(":avcodec-hw=")-1));
        d->discAllocationFailed=false;
        d->discFrameProgress.invalidate();
        d->lastDiscFrame=-1;
        auto path=QDir::fromNativeSeparators(*input);
        if(!path.endsWith('/'))path+='/';
        auto url=QUrl::fromLocalFile(path);
        url.setScheme("bluray");
#ifdef Q_OS_WIN
        const auto nativeRoot = QDir::toNativeSeparators(path);
        d->physicalBluRay = driveRoot(path) && GetDriveTypeW(reinterpret_cast<LPCWSTR>(nativeRoot.utf16())) == DRIVE_CDROM;
        d->libvlc_media_player_set_hwnd(d->player,reinterpret_cast<void *>(surface));
#elif defined(Q_OS_LINUX)
        d->libvlc_media_player_set_xwindow(d->player,static_cast<uint32_t>(surface));
#endif
        if(d->media){d->libvlc_media_release(d->media);d->media=nullptr;}
        d->media=d->libvlc_media_new_location(d->vlc,url.toEncoded().constData());
        if(!d->media){d->fail("Blu-ray media creation failed");return false;}
        d->libvlc_media_add_option(d->media,d->state.discMenus?":bluray-menu":":no-bluray-menu");
        if(preparedDiscDurationMs>0)d->libvlc_media_add_option(d->media,":start-paused");
        d->libvlc_media_add_option(d->media,":bluray-region=A");
        d->libvlc_media_add_option(d->media,":disc-caching=10000");
        d->libvlc_media_add_option(d->media,hardware.constData());
        d->libvlc_media_player_set_media(d->player,d->media);
        if(d->libvlc_media_player_play(d->player)<0){d->fail("Blu-ray 재생을 시작할 수 없습니다. 진단 로그를 확인하세요.");return false;}
    }else{
        d->fileSource=input->toUtf8();
        if(lanSource){
            d->fileOptions=lanDemuxer(lanSource->mimeType)+
                ",access-references=no,sub-auto=no,audio-file-auto=no,ytdl=no"
                ",cache-on-disk=no,resume-playback=no,save-position-on-quit=no";
            d->lanSubtitles=lanSource->subtitles;
        }
        d->audioFallbackWait.invalidate();
        d->audioFallbackRetried=false;
        const int64_t wid=static_cast<int64_t>(surface); d->mpv_set_property(d->mpv,"wid",MPV_FORMAT_INT64,const_cast<int64_t *>(&wid));
        d->motionMetadata.clear();
        if(d->motionApplied){d->command({"vf","remove","@luma_motion"});d->motionApplied=false;}
        if(d->dolbyApplied){d->command({"vf","remove","@luma_dolby"});d->dolbyApplied=false;}
        d->mpv_set_property_string(d->mpv,"interpolation","no");
        const auto hardware=fileHardwareDecoder(d->activeVideo.hardware,false);
        const auto decoder=preferredDecoder(d->activeVideo.codec);
        d->videoRejections.clear();
        const auto set=[&](const char *name,const char *value){
            if(d->mpv_set_property_string(d->mpv,name,value)<0)d->videoRejections.append(QString::fromLatin1(name));
        };
        set("options/hwdec",hardware);
        set("options/vd",decoder);
        d->state.videoDecoderRequest=QString::fromLatin1(hardware);
        if(*decoder)d->state.videoDecoderRequest+=" / "+QString::fromLatin1(decoder);
        const auto filter=dolbyFilter(d->activeVideo.dolby);
        if(*filter){
            d->dolbyApplied=d->command({"vf","add",filter})>=0;
            if(!d->dolbyApplied)d->videoRejections.append("Dolby Vision");
        }
        d->state.dolbyVisionRequest=d->activeVideo.dolby==VideoOverrides::DolbyVision::Ignore?"ignore":
            d->activeVideo.dolby==VideoOverrides::DolbyVision::BaseLayer?"base-layer":
            d->activeVideo.dolby==VideoOverrides::DolbyVision::Apply?"apply":"auto";
        d->applyVideoColor();
        d->applyFileAudio();
        if(d->loadFile()<0){d->fail("재생 엔진이 파일 열기 요청을 거부했습니다.");return false;}
    }
    d->videoOutputActive=true;d->videoOutputLossSent=false;
    emit logMessage(QString("Opening with %1").arg(name()));return true;
}
void PlaybackEngine::stop(){
    if(!available() || d->stopping.exchange(true))return;
    d->videoOutputActive=false;d->preparedDiscDuration=0;d->preparedNativeTitle=-1;d->preparedTitleWait.invalidate();
    d->timer.stop();d->state.playing=false;d->state.durationMs=0;d->state.timelineReady=false;d->state.lanSubtitleCueActive=false;d->fileLoaded=false;
    d->state.sourceVideoRange="unknown";
    d->state.chapterOffsetsMs.clear();
    if(d->player){
        // Embedded Win32 video teardown can synchronously message the host HWND.
        // Keep its owner thread pumping while libVLC's blocking stop joins output threads.
        auto *worker=QThread::create([player=d->player,stop=d->libvlc_media_player_stop]{stop(player);});
        QEventLoop loop;connect(worker,&QThread::finished,&loop,&QEventLoop::quit);worker->start();
        loop.exec(QEventLoop::ExcludeUserInputEvents);worker->wait();delete worker;
    }
    if(d->mpv){
        d->audioFallbackWait.invalidate();
        d->command({"stop"});
        d->fileOptions.clear();d->lanSubtitles.clear();
        if(d->lanSource)d->fileSource.clear();
    }
    d->stopping.store(false);if(!d->destroying)d->timer.start();
}
void PlaybackEngine::pauseToggle(){if(!available())return;if(d->player){if(d->preparedDiscDuration>0)setPaused(!d->preparedPauseRequested);else d->libvlc_media_player_pause(d->player);}else d->command({"cycle","pause"});}
void PlaybackEngine::setPaused(bool paused){if(!available())return;if(d->player){d->preparedPauseRequested=paused;d->libvlc_media_player_set_pause(d->player,d->preparedDiscDuration>0 || paused?1:0);}else d->mpv_set_property_string(d->mpv,"pause",paused?"yes":"no");}
void PlaybackEngine::setFrameInterpolation(const QString &mode,int targetFps){
    d->motion=normalizeMotionOptions({mode=="display"?MotionMode::DisplayBlend:mode=="optical"?MotionMode::OpticalFlow:MotionMode::Off,targetFps});
    d->motionMetadata.clear();updateFrameInterpolation();emit updated();
}
void PlaybackEngine::updateFrameInterpolation(){
    if(!available() || !d->mpv){d->state.motionStatus=motionPlan(d->motion,d->kind==Kind::File).status;return;}
    const auto gamma=d->fileLoaded?d->prop("video-params/gamma"):QString{},matrix=d->fileLoaded?d->prop("video-params/colormatrix"):QString{};const auto peak=d->fileLoaded?d->number("video-params/sig-peak"):0;
    const auto metadata=gamma+'\n'+matrix+'\n'+QString::number(peak);
    if(metadata==d->motionMetadata)return;d->motionMetadata=metadata;
    const auto plan=motionPlanForSource(d->motion,true,gamma,matrix,peak);d->state.motionStatus=plan.status;
    if(d->motionApplied){d->command({"vf","remove","@luma_motion"});d->motionApplied=false;}
    const auto hardware=fileHardwareDecoder(d->activeVideo.hardware,plan.requiresCopyBack);
    if(d->mpv_set_property_string(d->mpv,"hwdec",hardware)>=0 && !d->state.videoDecoderRequest.isEmpty()){
        d->state.videoDecoderRequest=QString::fromLatin1(hardware);
        const auto decoder=preferredDecoder(d->activeVideo.codec);
        if(*decoder)d->state.videoDecoderRequest+=" / "+QString::fromLatin1(decoder);
    }
    d->mpv_set_property_string(d->mpv,"interpolation",plan.interpolation.constData());
    d->mpv_set_property_string(d->mpv,"video-sync",plan.videoSync.constData());
    if(!plan.videoFilter.isEmpty()){
        if(d->command({"vf","add",plan.videoFilter.toUtf8()})>=0)d->motionApplied=true;
        else d->state.motionStatus="이 실행기에서 광학 흐름 필터를 시작할 수 없습니다.";
    }
}
void PlaybackEngine::seek(qint64 ms){if(!available())return;ms=std::max<qint64>(0,ms);if(d->player){d->discFrameProgress.invalidate();d->libvlc_media_player_set_time(d->player,ms);}else if(!d->seekReady)d->deferredSeek=ms;else d->command({"seek",QByteArray::number(double(ms)/1000,'f',3),"absolute+exact"});}
void PlaybackEngine::setPerformanceMetricsEnabled(bool enabled){
    d->performanceMetrics=enabled;
    if(!enabled){
        d->state.cacheSeconds.reset();
        d->state.avSyncMs.reset();
        d->state.fileCacheHolding=false;
    }
}
void PlaybackEngine::setVolume(int v){if(!available())return;v=std::clamp(v,0,100);if(d->player)d->libvlc_audio_set_volume(d->player,v);else d->mpv_set_property_string(d->mpv,"volume",QByteArray::number(v).constData());}
void PlaybackEngine::setRate(double r){if(!available())return;r=std::clamp(r,0.25,4.0);if(d->player)d->libvlc_media_player_set_rate(d->player,float(r));else d->mpv_set_property_string(d->mpv,"speed",QByteArray::number(r).constData());}
void PlaybackEngine::setAudio(int id){if(!available())return;if(d->player)d->libvlc_audio_set_track(d->player,id);else d->mpv_set_property_string(d->mpv,"aid",QByteArray::number(id).constData());}
void PlaybackEngine::setSubtitle(int id){if(!available())return;d->managedSubtitle=false;if(d->player)d->libvlc_video_set_spu(d->player,id);else d->mpv_set_property_string(d->mpv,"sid",id<0?"no":QByteArray::number(id).constData());}
void PlaybackEngine::setChapter(int id){if(!available())return;if(d->player){d->discFrameProgress.invalidate();d->libvlc_media_player_set_chapter(d->player,id);}else d->mpv_set_property_string(d->mpv,"chapter",QByteArray::number(id).constData());}
void PlaybackEngine::setTitle(int id){if(d->player){d->discFrameProgress.invalidate();d->preparedDiscDuration=0;d->preparedNativeTitle=-1;d->preparedTitleWait.invalidate();d->libvlc_media_player_set_title(d->player,id);}}
void PlaybackEngine::navigate(int mode){if(d->player && mode>=libvlc_navigate_activate && mode<=libvlc_navigate_popup)d->libvlc_media_player_navigate(d->player,mode);}
void PlaybackEngine::clearManagedSubtitles(){
    if(!d->managedSubtitle)return;
    if(d->player)d->libvlc_video_set_spu(d->player,-1);
    if(d->mpv){d->mpv_set_property_string(d->mpv,"sid","no");if(d->managedMpvSubtitle>0)d->command({"sub-remove",QByteArray::number(d->managedMpvSubtitle)});d->managedMpvSubtitle=-1;}
    d->managedSubtitle=false;
}
bool PlaybackEngine::loadSubtitle(const QString &path,bool managed){
    if(!available() || !QFileInfo::exists(path))return false;
    if(d->player){const bool ok=d->libvlc_media_player_add_slave(d->player,libvlc_media_slave_type_subtitle,QUrl::fromLocalFile(path).toEncoded().constData(),true)==0;if(ok)d->managedSubtitle=managed;return ok;}
    if(managed && d->managedMpvSubtitle>0)d->command({"sub-remove",QByteArray::number(d->managedMpvSubtitle)});
    const bool ok=d->command({"sub-add",path.toUtf8(),"select"})>=0;if(ok){d->managedSubtitle=managed;if(managed)d->managedMpvSubtitle=int(d->number("sid"));}return ok;
}
void PlaybackEngine::poll(){
    if(d->stopping.load())return;
    if(d->player){
        d->state.positionMs=std::max<qint64>(0,d->libvlc_media_player_get_time(d->player));
        d->state.durationMs=std::max<qint64>(0,d->libvlc_media_player_get_length(d->player));
        d->state.playing=d->libvlc_media_player_is_playing(d->player)!=0;
        d->state.currentTitle=d->libvlc_media_player_get_title(d->player);d->state.currentChapter=d->libvlc_media_player_get_chapter(d->player);
        d->state.currentAudio=d->libvlc_audio_get_track(d->player);d->state.currentSubtitle=d->libvlc_video_get_spu(d->player);
        libvlc_media_stats_t stats{};
        d->state.discStatisticsAvailable=d->media && d->libvlc_media_get_stats(d->media,&stats);
        if(d->state.discStatisticsAvailable){
            d->state.decodedVideo=stats.i_decoded_video;
            d->state.decodedAudio=stats.i_decoded_audio;
            d->state.displayedPictures=stats.i_displayed_pictures;
            d->state.playedAudioBuffers=stats.i_played_abuffers;
            d->state.droppedVideoFrames=stats.i_lost_pictures;
            d->state.lostAudioBuffers=stats.i_lost_abuffers;
            d->state.discInputBytes=quint32(stats.i_read_bytes);
            d->state.discDemuxBytes=quint32(stats.i_demux_read_bytes);
        }
        const auto nativeState=d->libvlc_media_player_get_state(d->player);
        d->state.buffering=nativeState==libvlc_Buffering;
        if(nativeState==libvlc_Error)d->fail("Blu-ray 재생 중 오류가 발생했습니다. 디스크를 다시 열어 주세요. 계속되면 개발자 모드의 진단을 확인하세요.");
        d->state.audioTracks=d->descriptions(d->libvlc_audio_get_track_description(d->player));
        d->state.subtitleTracks=d->descriptions(d->libvlc_video_get_spu_description(d->player));
        d->state.chapters.clear(); const int chapters=d->libvlc_media_player_get_chapter_count(d->player);
        for(int i=0;i<chapters && i<10000;++i)d->state.chapters.append({i,QString("Chapter %1").arg(i+1)});
        libvlc_title_description_t **titles=nullptr;
        const int count=d->libvlc_media_player_get_full_title_descriptions(d->player,&titles);
        d->state.titles.clear();
        const bool menuTitles=count>1 && titles && titles[0] && (titles[0]->i_flags&libvlc_title_menu);
        if(count>0 && titles){
            d->titleMetadataReady=true;
            d->navigationTitles.clear();
            for(int i=0;i<count && i<10000;++i)if(titles[i]){
                if((titles[i]->i_flags&libvlc_title_menu) || (menuTitles && i==count-1))d->navigationTitles.insert(i);
                if(titles[i]->i_duration>0)d->titleDurations.insert(i,titles[i]->i_duration);
            }
        }
        // Keep known lengths/mapping if a transient metadata query fails. The
        // live length can still describe the previous title after its ID changes.
        d->state.navigationTitle=!d->titleMetadataReady || d->state.currentTitle<0 || (d->state.discMenus && d->state.currentTitle==0) || d->navigationTitles.contains(d->state.currentTitle);
        d->state.timelineReady=discTimelineReady(d->state.durationMs,d->titleDurations.value(d->state.currentTitle),d->state.navigationTitle);
        if(d->preparedDiscDuration>0 && !d->state.failed){
            d->libvlc_media_player_set_pause(d->player,1);d->state.playing=false;
            int matches=0,match=-1,known=0;
            for(int i=0;titles && i<count && i<10000;++i)if(titles[i] && !d->navigationTitles.contains(i)){
                const auto length=titles[i]->i_duration;
                if(length>0 && length<=24LL*60*60*1000){++known;if(std::abs(length-d->preparedDiscDuration)<=200){++matches;match=i;}}
            }
            const bool complete=count>0 && count<=10000 && known==count;
            if(count>10000 || (complete && matches!=1))d->fail(matches>1?"준비한 길이와 일치하는 디스크 타이틀이 여러 개입니다. 타이틀을 확인해 주세요.":"준비한 디스크 타이틀을 직접 재생 목록에서 찾을 수 없습니다. 디스크 메뉴에서 타이틀을 확인해 주세요.");
            else if(complete && d->preparedNativeTitle>=0 && match!=d->preparedNativeTitle)d->fail("확인 중 디스크 타이틀 정보가 바뀌었습니다. 디스크를 다시 열어 주세요.");
            else if(complete && d->preparedNativeTitle<0){d->preparedNativeTitle=match;if(d->state.currentTitle!=match)d->libvlc_media_player_set_title(d->player,match);}
            if(!d->state.failed && complete && matches==1 && match==d->preparedNativeTitle && d->preparedNativeTitle>=0 && d->state.currentTitle==d->preparedNativeTitle && !d->state.navigationTitle && d->state.timelineReady && std::abs(d->state.durationMs-d->preparedDiscDuration)<=200){
                d->preparedDiscDuration=0;d->preparedTitleWait.invalidate();d->libvlc_media_player_set_pause(d->player,d->preparedPauseRequested?1:0);
            }else d->state.timelineReady=false;
            if(!d->state.failed && d->preparedTitleWait.isValid() && d->preparedTitleWait.elapsed()>=15000)d->fail("준비한 디스크 타이틀을 확인하는 시간이 초과됐습니다. 디스크를 다시 열어 주세요.");
        }
        for(int i=0;titles && i<count && i<10000;++i)if(titles[i])d->state.titles.append({i,QString::fromUtf8(titles[i]->psz_name?titles[i]->psz_name:"")+QString(" · %1 min").arg(titles[i]->i_duration/60000)});
        if(count>0 && titles)d->libvlc_title_descriptions_release(titles,unsigned(count));
        if (!d->state.timelineReady || chapters <= 0) {
            d->state.chapterOffsetsMs.clear();
            d->chapterOffsetsCount = -1;
        } else if (d->libvlc_media_player_get_full_chapter_descriptions && d->libvlc_chapter_descriptions_release &&
                   (d->chapterOffsetsTitle != d->state.currentTitle || d->chapterOffsetsCount != chapters)) {
            d->state.chapterOffsetsMs.clear();
            libvlc_chapter_description_t **descriptions = nullptr;
            const int descriptionCount = d->libvlc_media_player_get_full_chapter_descriptions(d->player, d->state.currentTitle, &descriptions);
            if (descriptionCount > 0 && descriptions) {
                for (int i = 0; i < std::min(descriptionCount, 10000); ++i) {
                    if (descriptions[i] && descriptions[i]->i_time_offset >= 0)
                        d->state.chapterOffsetsMs.append(descriptions[i]->i_time_offset);
                }
                d->libvlc_chapter_descriptions_release(descriptions, unsigned(descriptionCount));
                d->chapterOffsetsTitle = d->state.currentTitle;
                d->chapterOffsetsCount = chapters;
            }
        }
        if (d->metadataTitle != d->state.currentTitle)
            d->state.sourceVideoRange = "unknown";
        if(d->media && d->state.playing && (d->state.videoCodec.isEmpty() || d->state.audioCodec.isEmpty() || d->metadataTitle!=d->state.currentTitle || d->metadataAudio!=d->state.currentAudio)){
            libvlc_media_track_t **tracks=nullptr;const auto n=d->libvlc_media_tracks_get(d->media,&tracks);
            d->state.sourceFps=0;
            bool sawVideoTrack = false;
            bool standardHdAvc = d->physicalBluRay;
            for(unsigned i=0;tracks && i<n;++i){
                const auto *track=tracks[i];
                if (!track)
                    continue;
                const auto code=track->i_codec;
                const char fourcc[5]={char(code&255),char((code>>8)&255),char((code>>16)&255),char((code>>24)&255),0};
                if(track->i_type==libvlc_track_video){
                    d->state.videoCodec=QString::fromLatin1(fourcc);
                    sawVideoTrack = true;
                    // Conventional HD AVC Blu-ray uses SDR. LibVLC 3 provides no transfer field.
                    const bool avc = d->state.videoCodec == "h264" || d->state.videoCodec == "avc1";
                    const int profile = track->i_profile;
                    standardHdAvc &= avc && track->video && track->video->i_width == 1920 &&
                        track->video->i_height == 1080 && (profile <= 0 || profile == 66 || profile == 77 || profile == 100);
                    if(track->video && track->video->i_frame_rate_den)
                        d->state.sourceFps=double(track->video->i_frame_rate_num)/track->video->i_frame_rate_den;
                }
                if(track->i_type==libvlc_track_audio && track->i_id==d->state.currentAudio){
                    d->state.audioCodec=QString::fromLatin1(fourcc);
                    if(track->audio){d->state.audioSampleRate=int(track->audio->i_rate);d->state.audioChannels=int(track->audio->i_channels);}
                }
            }
            if (sawVideoTrack)
                d->state.sourceVideoRange = standardHdAvc ? "sdr" : "unknown";
            if(n && tracks)d->libvlc_media_tracks_release(tracks,n);
            d->metadataTitle=d->state.currentTitle;d->metadataAudio=d->state.currentAudio;
        }
        if(d->discSoftware)d->state.hardwareDecoder="CPU · Blu-ray 호환 디코딩";
        const bool monitorGpu=!d->discSoftware && d->videoOutputActive && !d->state.failed &&
            nativeState==libvlc_Playing && d->state.playing && d->state.timelineReady && !d->state.videoCodec.isEmpty();
        if(!monitorGpu)d->discFrameProgress.invalidate();
        else if(d->lastDiscFrame!=d->state.displayedPictures || !d->discFrameProgress.isValid()){
            if(d->lastDiscFrame!=d->state.displayedPictures)d->discAllocationFailed=false;
            d->lastDiscFrame=d->state.displayedPictures;
            d->audioAtLastDiscFrame=d->state.playedAudioBuffers;
            d->discFrameProgress.start();
        }else if(!d->videoOutputLossSent && d->discFrameProgress.elapsed()>=8000 &&
                 (d->discAllocationFailed || d->state.playedAudioBuffers>d->audioAtLastDiscFrame)){
            d->videoOutputLossSent=true;
            emit videoOutputLost(QStringLiteral("Blu-ray GPU 영상이 멈춰 호환 디코딩으로 복구합니다."));
        }
    }else if(d->mpv){
        for(int i=0;i<100;++i){auto *event=d->mpv_wait_event(d->mpv,0);if(!event || event->event_id==MPV_EVENT_NONE)break;
            if(event->event_id==MPV_EVENT_FILE_LOADED){
                d->fileLoaded=true;updateFrameInterpolation();
                if(!d->lanSubtitles.isEmpty()){
                    // The primary demuxer is open. Let the WebVTT track select its own format.
                    if(d->mpv_set_property_string(d->mpv,"file-local-options/demuxer-lavf-format","")>=0){
                        bool first=true;
                        for(const auto &[url,label]:d->lanSubtitles){
                            d->command({"sub-add",url.toEncoded(),first?"select":"auto",label.toUtf8()},true);
                            first=false;
                        }
                    }
                }
            }
            if(event->event_id==MPV_EVENT_PLAYBACK_RESTART){d->seekReady=true;if(d->deferredSeek){const auto target=*d->deferredSeek;d->deferredSeek.reset();seek(target);}}
            if(event->event_id==MPV_EVENT_LOG_MESSAGE){
                auto *m=static_cast<mpv_event_log_message *>(event->data);
                const auto message=QString::fromUtf8(m->text).trimmed();
                if(d->videoOutputActive && d->state.audioBitstreamRequested && !d->audioFallbackRetried &&
                   QByteArrayView(m->prefix)=="cplayer" && message=="Falling back to PCM output."){
                    d->audioFallbackWait.start();
                    d->mpv_request_log_messages(d->mpv,"warn");
                }
                if(m->log_level<=MPV_LOG_LEVEL_WARN && !d->lanSource)emit logMessage(redactNetworkSources(message));
            }
            if(event->event_id==MPV_EVENT_END_FILE){
                auto *end=static_cast<mpv_event_end_file *>(event->data);
                if(end->reason!=MPV_END_FILE_REASON_ERROR)continue;
                if(end->error==MPV_ERROR_AO_INIT_FAILED && d->videoOutputActive && d->state.audioBitstreamRequested && !d->audioFallbackRetried){
                    if(!d->audioFallbackWait.isValid())d->audioFallbackWait.start();
                }else d->fail(QString::fromUtf8(d->mpv_error_string(end->error)));
            }
        }
        d->state.positionMs=qint64(d->number("time-pos")*1000);d->state.durationMs=qint64(d->number("duration")*1000);
        d->state.timelineReady=d->fileLoaded && d->state.durationMs>0;
        int paused=1,idle=1;d->mpv_get_property(d->mpv,"pause",MPV_FORMAT_FLAG,&paused);d->mpv_get_property(d->mpv,"idle-active",MPV_FORMAT_FLAG,&idle);d->state.playing=!paused && !idle;
        d->state.videoCodec=d->prop("video-codec");d->state.audioCodec=d->prop("audio-codec-name");d->state.hardwareDecoder=d->prop("hwdec-current");
        d->state.videoDecoder=d->prop("current-tracks/video/decoder");
        int64_t dvProfile=0;
        if(d->mpv_get_property(d->mpv,"current-tracks/video/dolby-vision-profile",MPV_FORMAT_INT64,&dvProfile)>=0 && dvProfile>0 && dvProfile<=255)
            d->state.dolbyVisionProfile=int(dvProfile);
        else d->state.dolbyVisionProfile.reset();
        d->state.runtimeVersion=d->prop("mpv-version");d->state.colorPrimaries=d->prop("video-params/primaries");d->state.transferFunction=d->prop("video-params/gamma");
        d->state.sourceFps=d->number("container-fps");d->state.filteredFps=d->number("estimated-vf-fps");d->state.displayFps=d->number("display-fps");
        if(d->performanceMetrics){
            d->state.cacheSeconds=d->metric("demuxer-cache-duration");
            d->state.avSyncMs=d->metric("avsync");
            if(d->state.avSyncMs)*d->state.avSyncMs*=1000;
            int cacheHolding=0;
            d->mpv_get_property(d->mpv,"paused-for-cache",MPV_FORMAT_FLAG,&cacheHolding);
            d->state.fileCacheHolding=cacheHolding!=0;
        }
        updateFrameInterpolation();
        d->state.outputPrimaries=d->prop("video-target-params/primaries");d->state.outputTransferFunction=d->prop("video-target-params/gamma");
        d->state.audioSampleRate=int(d->number("audio-params/samplerate"));d->state.audioChannels=int(d->number("audio-params/channel-count"));d->state.audioOutput=d->prop("current-ao");d->state.droppedVideoFrames=int(d->number("frame-drop-count"));
        d->state.audioFormat=d->prop("audio-out-params/format");
        d->state.audioLayout=d->prop("audio-out-params/hr-channels");
        d->state.audioBitstreamActive=d->state.audioFormat.startsWith("spdif-") && !d->state.audioOutput.isEmpty() && d->state.audioOutput!="null";
        d->state.audioVolumeExternal=d->state.audioBitstreamActive;
        d->finishAudioFallback();
        d->state.currentChapter=int(d->number("chapter"));
        d->state.currentAudio=int(d->number("aid"));const auto sid=d->prop("sid");d->state.currentSubtitle=sid=="no"?-1:sid.toInt();
        d->state.lanSubtitleCueActive=d->lanSource && d->state.currentSubtitle>=0 && !d->prop("sub-text").isEmpty();
        const int trackCount=std::clamp(int(d->number("track-list/count")),0,1000);d->state.audioTracks.clear();d->state.subtitleTracks={{-1,"자막 끄기"}};
        int sourceVideoTracks = 0;
        bool sourceVideoSelected = false;
        for (int i = 0; i < trackCount; ++i) {
            const auto prefix = QByteArray("track-list/") + QByteArray::number(i) + '/';
            const auto type = d->prop((prefix + "type").constData());
            if (type == "video" && d->prop((prefix + "albumart").constData()) != "yes" &&
                d->prop((prefix + "external").constData()) != "yes") {
                ++sourceVideoTracks;
                sourceVideoSelected |= d->prop((prefix + "selected").constData()) == "yes";
            }
            if (type != "audio" && type != "sub")
                continue;
            const int id = int(d->number((prefix + "id").constData()));
            const auto title = d->prop((prefix + "title").constData());
            const auto language = d->prop((prefix + "lang").constData());
            auto &tracks = type == "audio" ? d->state.audioTracks : d->state.subtitleTracks;
            tracks.append({id, QString("%1 %2 %3").arg(id).arg(title, language)});
        }
        // The encoder maps the first ordinary video stream. The selected decoder's
        // transfer is safe to send only when it corresponds to the sole source video.
        d->state.sourceVideoRange = "unknown";
        if (d->fileLoaded && sourceVideoTracks == 1 && sourceVideoSelected) {
            d->state.sourceVideoRange = videoRangeFromTransfer(d->prop("video-dec-params/gamma"));
            // The decoder often reports auto for SDR. Accept its resolved SDR
            // transfer, but never infer a source HDR format from display settings.
            if (d->state.sourceVideoRange == "unknown" &&
                videoRangeFromTransfer(d->prop("video-params/gamma")) == "sdr")
                d->state.sourceVideoRange = "sdr";
        }
        const int chapters=std::clamp(int(d->number("chapter-list/count")),0,10000);d->state.chapters.clear();
        const bool readOffsets = d->state.timelineReady && d->chapterOffsetsCount != chapters;
        if (readOffsets) {
            d->state.chapterOffsetsMs.clear();
            d->chapterOffsetsCount = chapters;
        }
        for (int i = 0; i < chapters; ++i) {
            const auto prefix = QByteArray("chapter-list/") + QByteArray::number(i) + '/';
            d->state.chapters.append({i, d->prop((prefix + "title").constData())});
            if (readOffsets) {
                const auto seconds = d->metric((prefix + "time").constData());
                if (seconds && *seconds >= 0 && *seconds < double(std::numeric_limits<qint64>::max()) / 1000)
                    d->state.chapterOffsetsMs.append(std::llround(*seconds * 1000));
            }
        }
    }
    if(d->state.failed){d->state.playing=false;d->state.timelineReady=false;}
    const auto codec=d->state.audioCodec.toLower();d->state.losslessAudio=codec=="flac" || codec=="alac" || codec=="truehd" || codec.startsWith("pcm") || codec=="bpcm" || codec=="mlp" || codec=="s16l" || codec=="s24l";
    emit updated();
}
}
