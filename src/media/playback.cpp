#include "playback.h"
#include "motion_options.h"
#include <QSettings>
#include <QSet>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QTimer>
#include <QThread>
#include <QEventLoop>
#include <QUrl>
#include <QRegularExpression>
#include <vlc/vlc.h>
#include <mpv/client.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <atomic>
#include <optional>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace luma {
int discActionCode(DiscAction action){
    switch(action){case DiscAction::Activate:return libvlc_navigate_activate;case DiscAction::Up:return libvlc_navigate_up;case DiscAction::Down:return libvlc_navigate_down;case DiscAction::Left:return libvlc_navigate_left;case DiscAction::Right:return libvlc_navigate_right;case DiscAction::Popup:return libvlc_navigate_popup;}return -1;
}
bool isDiscSource(const QString &source) {
    return source.startsWith("bluray://",Qt::CaseInsensitive) ||
        QRegularExpression("^[A-Za-z]:[/\\\\]?$").match(source).hasMatch() ||
        QFileInfo::exists(QDir(source).filePath("BDMV/index.bdmv"));
}
struct PlaybackEngine::Impl {
    PlaybackEngine *owner;
    Kind kind;
    QLibrary lib;
    QString error;
    PlaybackState state;
    QTimer timer;
    bool ready=false, errorSent=false;
    bool managedSubtitle=false;
    MotionOptions motion;
    QString motionMetadata;
    bool motionApplied=false;
    bool fileLoaded=false;
    bool seekReady=false;
    std::optional<qint64> deferredSeek;
    bool discSoftware=true;
    int metadataTitle=-2,metadataAudio=-2;
    QSet<int> navigationTitles;
    bool titleMetadataReady=false;
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
    MF(mpv_set_option); MF(mpv_command); MF(mpv_get_property); MF(mpv_get_property_string);
    MF(mpv_set_property); MF(mpv_set_property_string); MF(mpv_wait_event); MF(mpv_free);
    MF(mpv_free_node_contents); MF(mpv_request_log_messages); MF(mpv_error_string);
#undef MF
    explicit Impl(PlaybackEngine *o,Kind k):owner(o),kind(k){}
    ~Impl(){
        lib.unload();
#ifdef Q_OS_WIN
        if(directoryCookie)RemoveDllDirectory(directoryCookie);
#endif
    }
    template<class T> bool resolve(T &f,const char *name) { f=reinterpret_cast<T>(lib.resolve(name)); if(!f) error=QString("Media runtime symbol missing: %1").arg(name); return f!=nullptr; }
    void fail(const QString &text) { error=text; state.failed=true; if(!errorSent){errorSent=true; emit owner->errorOccurred(text);} }
    int command(const QList<QByteArray> &args) {
        if(!mpv) return MPV_ERROR_UNINITIALIZED;
        QVector<const char *> pointers; for(const auto &s:args) pointers.append(s.constData()); pointers.append(nullptr);
        const int code=mpv_command(mpv,pointers.data());
        if(code<0) emit owner->logMessage(QString("mpv: %1").arg(QString::fromUtf8(mpv_error_string(code))));
        return code;
    }
    QString prop(const char *key) { char *s=mpv_get_property_string(mpv,key); if(!s)return {}; const auto result=QString::fromUtf8(s); mpv_free(s); return result; }
    double number(const char *key) { double value=0; mpv_get_property(mpv,key,MPV_FORMAT_DOUBLE,&value); return value; }
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
            if(msg.contains("Your input can't be opened") || msg.startsWith("VLC is unable to open"))o->d->fail(msg);
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
        d->discSoftware=qEnvironmentVariable("LUMA_DISC_DECODER")!="hardware";
        const char *options[]={"--no-video-title-show","--no-osd","--no-snapshot-preview","--no-lua","--verbose=1","--avcodec-hw=none"};
        d->vlc=d->libvlc_new(d->discSoftware?6:5,options);
        if(!d->vlc){d->error="libVLC initialization failed";return;}
        d->libvlc_log_set(d->vlc,&Impl::log,d.get());
        d->player=d->libvlc_media_player_new(d->vlc);
        if(!d->player){d->error="libVLC player allocation failed";return;}
        d->libvlc_video_set_key_input(d->player,0); d->libvlc_video_set_mouse_input(d->player,1);
    }else{
#define LOAD(n) ok=d->resolve(d->n,#n)&&ok
        LOAD(mpv_create); LOAD(mpv_initialize); LOAD(mpv_terminate_destroy); LOAD(mpv_set_option_string); LOAD(mpv_set_option);
        LOAD(mpv_command); LOAD(mpv_get_property); LOAD(mpv_get_property_string); LOAD(mpv_set_property); LOAD(mpv_set_property_string);
        LOAD(mpv_wait_event); LOAD(mpv_free); LOAD(mpv_free_node_contents); LOAD(mpv_request_log_messages); LOAD(mpv_error_string);
#undef LOAD
        if(!ok)return;
        d->mpv=d->mpv_create(); if(!d->mpv){d->error="libmpv allocation failed";return;}
        for(const auto &[key,value]:QList<QPair<QByteArray,QByteArray>>{{"config","no"},{"terminal","no"},{"input-default-bindings","no"},{"input-vo-keyboard","no"},{"osc","no"},{"osd-level","0"},{"idle","yes"},{"keep-open","yes"},{"hwdec","auto-safe"},{"vo","gpu-next"}})
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
bool PlaybackEngine::open(const QString &source,WId surface){
    if(!available()){d->fail(d->error);return false;}
    stop();++d->generation;d->state={};d->errorSent=false;d->managedSubtitle=false;d->managedMpvSubtitle=-1;
    d->fileLoaded=false;d->seekReady=false;d->deferredSeek.reset();d->navigationTitles.clear();d->titleMetadataReady=false;
    if(d->kind==Kind::Disc){
        QString url=source;
        if(!url.startsWith("bluray://")) { auto path=QDir::fromNativeSeparators(QFileInfo(source).absoluteFilePath()); if(!path.endsWith('/'))path+='/'; url="bluray://"+(path.startsWith('/')?QString{}:QString("/"))+path; }
#ifdef Q_OS_WIN
        d->libvlc_media_player_set_hwnd(d->player,reinterpret_cast<void *>(surface));
#elif defined(Q_OS_LINUX)
        d->libvlc_media_player_set_xwindow(d->player,static_cast<uint32_t>(surface));
#endif
        if(d->media){d->libvlc_media_release(d->media);d->media=nullptr;}
        d->media=d->libvlc_media_new_location(d->vlc,url.toUtf8().constData());
        if(!d->media){d->fail("Blu-ray media creation failed");return false;}
        d->libvlc_media_add_option(d->media,":bluray-menu=1");
        d->libvlc_media_add_option(d->media,":bluray-region=A");
        d->libvlc_media_add_option(d->media,":disc-caching=10000");
        if(d->discSoftware)d->libvlc_media_add_option(d->media,":avcodec-hw=none");
        d->libvlc_media_player_set_media(d->player,d->media);
        if(d->libvlc_media_player_play(d->player)<0){d->fail("Blu-ray 재생을 시작할 수 없습니다. 진단 로그를 확인하세요.");return false;}
    }else{
        const int64_t wid=static_cast<int64_t>(surface); d->mpv_set_property(d->mpv,"wid",MPV_FORMAT_INT64,const_cast<int64_t *>(&wid));
        d->motionMetadata.clear();
        if(d->motionApplied){d->command({"vf","remove","@luma_motion"});d->motionApplied=false;}
        d->mpv_set_property_string(d->mpv,"interpolation","no");
        d->command({"loadfile",source.toUtf8(),"replace"});
    }
    emit logMessage(QString("Opening with %1").arg(name()));return true;
}
void PlaybackEngine::stop(){
    if(!available() || d->stopping.exchange(true))return;
    d->timer.stop();d->state.playing=false;d->state.durationMs=0;
    if(d->player){
        // Embedded Win32 video teardown can synchronously message the host HWND.
        // Keep its owner thread pumping while libVLC's blocking stop joins output threads.
        auto *worker=QThread::create([player=d->player,stop=d->libvlc_media_player_stop]{stop(player);});
        QEventLoop loop;connect(worker,&QThread::finished,&loop,&QEventLoop::quit);worker->start();
        loop.exec(QEventLoop::ExcludeUserInputEvents);worker->wait();delete worker;
    }
    if(d->mpv)d->command({"stop"});d->stopping.store(false);if(!d->destroying)d->timer.start();
}
void PlaybackEngine::pauseToggle(){if(!available())return;if(d->player)d->libvlc_media_player_pause(d->player);else d->command({"cycle","pause"});}
void PlaybackEngine::setPaused(bool paused){if(!available())return;if(d->player)d->libvlc_media_player_set_pause(d->player,paused?1:0);else d->mpv_set_property_string(d->mpv,"pause",paused?"yes":"no");}
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
    d->mpv_set_property_string(d->mpv,"hwdec",plan.requiresCopyBack?"auto-copy":"auto-safe");
    d->mpv_set_property_string(d->mpv,"interpolation",plan.interpolation.constData());
    d->mpv_set_property_string(d->mpv,"video-sync",plan.videoSync.constData());
    if(!plan.videoFilter.isEmpty()){
        if(d->command({"vf","add",plan.videoFilter.toUtf8()})>=0)d->motionApplied=true;
        else d->state.motionStatus="이 실행기에서 광학 흐름 필터를 시작할 수 없습니다.";
    }
}
void PlaybackEngine::seek(qint64 ms){if(!available())return;ms=std::max<qint64>(0,ms);if(d->player)d->libvlc_media_player_set_time(d->player,ms);else if(!d->seekReady)d->deferredSeek=ms;else d->command({"seek",QByteArray::number(double(ms)/1000,'f',3),"absolute+exact"});}
void PlaybackEngine::setVolume(int v){if(!available())return;v=std::clamp(v,0,100);if(d->player)d->libvlc_audio_set_volume(d->player,v);else d->mpv_set_property_string(d->mpv,"volume",QByteArray::number(v).constData());}
void PlaybackEngine::setRate(double r){if(!available())return;r=std::clamp(r,0.25,4.0);if(d->player)d->libvlc_media_player_set_rate(d->player,float(r));else d->mpv_set_property_string(d->mpv,"speed",QByteArray::number(r).constData());}
void PlaybackEngine::setAudio(int id){if(!available())return;if(d->player)d->libvlc_audio_set_track(d->player,id);else d->mpv_set_property_string(d->mpv,"aid",QByteArray::number(id).constData());}
void PlaybackEngine::setSubtitle(int id){if(!available())return;d->managedSubtitle=false;if(d->player)d->libvlc_video_set_spu(d->player,id);else d->mpv_set_property_string(d->mpv,"sid",id<0?"no":QByteArray::number(id).constData());}
void PlaybackEngine::setChapter(int id){if(!available())return;if(d->player)d->libvlc_media_player_set_chapter(d->player,id);else d->mpv_set_property_string(d->mpv,"chapter",QByteArray::number(id).constData());}
void PlaybackEngine::setTitle(int id){if(d->player)d->libvlc_media_player_set_title(d->player,id);}
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
        libvlc_media_stats_t stats{};if(d->media && d->libvlc_media_get_stats(d->media,&stats)){d->state.decodedVideo=stats.i_decoded_video;d->state.decodedAudio=stats.i_decoded_audio;d->state.displayedPictures=stats.i_displayed_pictures;d->state.playedAudioBuffers=stats.i_played_abuffers;d->state.droppedVideoFrames=stats.i_lost_pictures;d->state.lostAudioBuffers=stats.i_lost_abuffers;}
        if(d->libvlc_media_player_get_state(d->player)==libvlc_Error)d->fail("Blu-ray 재생 오류: AACS/BD+ 설정과 진단 로그를 확인하세요.");
        d->state.audioTracks=d->descriptions(d->libvlc_audio_get_track_description(d->player));
        d->state.subtitleTracks=d->descriptions(d->libvlc_video_get_spu_description(d->player));
        d->state.chapters.clear(); const int chapters=d->libvlc_media_player_get_chapter_count(d->player);
        for(int i=0;i<chapters && i<10000;++i)d->state.chapters.append({i,QString("Chapter %1").arg(i+1)});
        libvlc_title_description_t **titles=nullptr;
        const int count=d->libvlc_media_player_get_full_title_descriptions(d->player,&titles);
        d->state.titles.clear();
        const bool menuTitles=count>1 && titles[0] && (titles[0]->i_flags&libvlc_title_menu);
        if(count>0){
            d->titleMetadataReady=true;
            d->navigationTitles.clear();
            for(int i=0;i<count;++i)if(titles[i] && ((titles[i]->i_flags&libvlc_title_menu) || (menuTitles && i==count-1)))d->navigationTitles.insert(i);
        }
        // Keep the mapping if a transient metadata query fails. First Play
        // follows the content titles in libVLC's Blu-ray menu demuxer.
        d->state.navigationTitle=!d->titleMetadataReady || d->state.currentTitle<=0 || d->navigationTitles.contains(d->state.currentTitle);
        for(int i=0;i<count && i<10000;++i)d->state.titles.append({i,QString::fromUtf8(titles[i]->psz_name?titles[i]->psz_name:"")+QString(" · %1 min").arg(titles[i]->i_duration/60000)});
        if(count>0)d->libvlc_title_descriptions_release(titles,unsigned(count));
        if(d->media && d->state.playing && (d->state.videoCodec.isEmpty() || d->state.audioCodec.isEmpty() || d->metadataTitle!=d->state.currentTitle || d->metadataAudio!=d->state.currentAudio)){
            libvlc_media_track_t **tracks=nullptr;const auto n=d->libvlc_media_tracks_get(d->media,&tracks);
            for(unsigned i=0;i<n;++i){const auto code=tracks[i]->i_codec;char fourcc[5]={char(code&255),char((code>>8)&255),char((code>>16)&255),char((code>>24)&255),0};if(tracks[i]->i_type==libvlc_track_video)d->state.videoCodec=QString::fromLatin1(fourcc);if(tracks[i]->i_type==libvlc_track_audio && tracks[i]->i_id==d->state.currentAudio){d->state.audioCodec=QString::fromLatin1(fourcc);if(tracks[i]->audio){d->state.audioSampleRate=int(tracks[i]->audio->i_rate);d->state.audioChannels=int(tracks[i]->audio->i_channels);}}}
            if(n)d->libvlc_media_tracks_release(tracks,n);
            d->metadataTitle=d->state.currentTitle;d->metadataAudio=d->state.currentAudio;
        }
        if(d->discSoftware)d->state.hardwareDecoder="CPU · Blu-ray 호환 디코딩";
    }else if(d->mpv){
        for(int i=0;i<100;++i){auto *event=d->mpv_wait_event(d->mpv,0);if(!event || event->event_id==MPV_EVENT_NONE)break;
            if(event->event_id==MPV_EVENT_FILE_LOADED){d->fileLoaded=true;updateFrameInterpolation();}
            if(event->event_id==MPV_EVENT_PLAYBACK_RESTART){d->seekReady=true;if(d->deferredSeek){const auto target=*d->deferredSeek;d->deferredSeek.reset();seek(target);}}
            if(event->event_id==MPV_EVENT_LOG_MESSAGE){auto *m=static_cast<mpv_event_log_message *>(event->data);emit logMessage(QString::fromUtf8(m->text).trimmed());}
            if(event->event_id==MPV_EVENT_END_FILE){auto *end=static_cast<mpv_event_end_file *>(event->data);if(end->reason==MPV_END_FILE_REASON_ERROR)d->fail(QString::fromUtf8(d->mpv_error_string(end->error)));}
        }
        d->state.positionMs=qint64(d->number("time-pos")*1000);d->state.durationMs=qint64(d->number("duration")*1000);
        int paused=1,idle=1;d->mpv_get_property(d->mpv,"pause",MPV_FORMAT_FLAG,&paused);d->mpv_get_property(d->mpv,"idle-active",MPV_FORMAT_FLAG,&idle);d->state.playing=!paused && !idle;
        d->state.videoCodec=d->prop("video-codec");d->state.audioCodec=d->prop("audio-codec-name");d->state.hardwareDecoder=d->prop("hwdec-current");
        d->state.runtimeVersion=d->prop("mpv-version");d->state.colorPrimaries=d->prop("video-params/primaries");d->state.transferFunction=d->prop("video-params/gamma");
        d->state.sourceFps=d->number("container-fps");d->state.filteredFps=d->number("estimated-vf-fps");d->state.displayFps=d->number("display-fps");
        updateFrameInterpolation();
        d->state.outputPrimaries=d->prop("video-target-params/primaries");d->state.outputTransferFunction=d->prop("video-target-params/gamma");
        d->state.audioSampleRate=int(d->number("audio-params/samplerate"));d->state.audioChannels=int(d->number("audio-params/channel-count"));d->state.audioOutput=d->prop("current-ao");d->state.droppedVideoFrames=int(d->number("frame-drop-count"));
        d->state.currentChapter=int(d->number("chapter"));
        d->state.currentAudio=int(d->number("aid"));const auto sid=d->prop("sid");d->state.currentSubtitle=sid=="no"?-1:sid.toInt();
        const int trackCount=std::clamp(int(d->number("track-list/count")),0,1000);d->state.audioTracks.clear();d->state.subtitleTracks={{-1,"자막 끄기"}};
        for(int i=0;i<trackCount;++i){const auto prefix=QByteArray("track-list/")+QByteArray::number(i)+"/";const auto type=d->prop((prefix+"type").constData());const int id=int(d->number((prefix+"id").constData()));const auto label=d->prop((prefix+"title").constData())+" "+d->prop((prefix+"lang").constData());if(type=="audio")d->state.audioTracks.append({id,QString("%1 %2").arg(id).arg(label)});if(type=="sub")d->state.subtitleTracks.append({id,QString("%1 %2").arg(id).arg(label)});}
        const int chapters=std::clamp(int(d->number("chapter-list/count")),0,10000);d->state.chapters.clear();
        for(int i=0;i<chapters;++i)d->state.chapters.append({i,d->prop((QByteArray("chapter-list/")+QByteArray::number(i)+"/title").constData())});
    }
    const auto codec=d->state.audioCodec.toLower();d->state.losslessAudio=codec=="flac" || codec=="alac" || codec=="truehd" || codec.startsWith("pcm") || codec=="bpcm" || codec=="mlp" || codec=="s16l" || codec=="s24l";
    emit updated();
}
}
