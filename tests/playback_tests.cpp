#ifdef LUMA_VLC_ERROR_FIXTURE
#include <vlc/vlc.h>
#include <cstdarg>
#include <string>

// 네이티브 경계만 대체하고 실제 엔진의 로그 큐·세션·poll·오류 신호를 검증한다.
// Only the native boundary is replaced. The tests below run the real engine,
// queued log delivery, generation checks, timer polling and error signals.
struct libvlc_instance_t { libvlc_log_cb callback=nullptr;void *opaque=nullptr; };
struct libvlc_media_t { std::string location;bool menus=true,startPaused=false; };
struct libvlc_media_player_t {
    libvlc_instance_t *instance;
    libvlc_media_t *media=nullptr;
    libvlc_state_t state=libvlc_Stopped;
    bool playing=false;
    int title=-1,pendingTitle=-1,ackReads=0,metadataReads=0;bool premature=false;
};
static libvlc_media_player_t *currentPlayer=nullptr;
static bool preparedFixture(const libvlc_media_player_t *p){return p->media && p->media->location.find("/prepared/")!=std::string::npos;}
static libvlc_time_t titleDuration(libvlc_media_player_t *p,int title){
    if(p->media->menus)return title==3?20020:title==0?750750:title==1?6294288:6296832;
    if(p->media->location.find("ambiguous")!=std::string::npos)return 6296832;
    if(p->media->location.find("missing")!=std::string::npos)return 6290000+title*30000;
    return title==0?6296832:6294288;
}
static void nativeLog(libvlc_instance_t *instance,const char *format,...){
    if(!instance->callback)return;
    va_list args;va_start(args,format);
    instance->callback(instance->opaque,LIBVLC_ERROR,nullptr,format,args);
    va_end(args);
}
#define FIXTURE_API extern "C"
FIXTURE_API int luma_fixture_disc_menus(){return currentPlayer && currentPlayer->media && currentPlayer->media->menus;}
FIXTURE_API int luma_fixture_premature_playback(){return currentPlayer && currentPlayer->premature;}
FIXTURE_API void luma_fixture_failure(const char *detail,int genericFirst){
    if(!currentPlayer)return;
    currentPlayer->state=libvlc_Error;
    // 오류 콜백과 이전 is_playing 조회 결과가 엇갈리는 비동기 순서를 재현한다.
    // Deliberately leave the previous is_playing snapshot true. Native getters
    // and asynchronous error callbacks do not form an atomic snapshot.
    if(genericFirst)nativeLog(currentPlayer->instance,"Your input can't be opened");
    if(detail && *detail)nativeLog(currentPlayer->instance,"%s",detail);
    if(!genericFirst)nativeLog(currentPlayer->instance,"Your input can't be opened");
}
FIXTURE_API void luma_fixture_video_log(const char *message){
    if(currentPlayer)nativeLog(currentPlayer->instance,"%s",message);
}
FIXTURE_API libvlc_instance_t *libvlc_new(int,const char *const *){return new libvlc_instance_t;}
FIXTURE_API void libvlc_release(libvlc_instance_t *p){delete p;}
FIXTURE_API void libvlc_log_set(libvlc_instance_t *p,libvlc_log_cb callback,void *opaque){p->callback=callback;p->opaque=opaque;}
FIXTURE_API void libvlc_log_unset(libvlc_instance_t *p){p->callback=nullptr;}
FIXTURE_API libvlc_media_t *libvlc_media_new_location(libvlc_instance_t *,const char *location){return new libvlc_media_t{location};}
FIXTURE_API void libvlc_media_add_option(libvlc_media_t *p,const char *option){const std::string value=option;if(value==":no-bluray-menu")p->menus=false;if(value==":start-paused")p->startPaused=true;}
FIXTURE_API void libvlc_media_release(libvlc_media_t *p){delete p;}
FIXTURE_API libvlc_media_player_t *libvlc_media_player_new(libvlc_instance_t *p){currentPlayer=new libvlc_media_player_t{p};return currentPlayer;}
FIXTURE_API void libvlc_media_player_release(libvlc_media_player_t *p){if(currentPlayer==p)currentPlayer=nullptr;delete p;}
FIXTURE_API void libvlc_media_player_set_media(libvlc_media_player_t *p,libvlc_media_t *media){p->media=media;p->title=preparedFixture(p)?(media->menus?3:1):-1;p->pendingTitle=-1;p->metadataReads=0;p->premature=false;}
FIXTURE_API int libvlc_media_player_play(libvlc_media_player_t *p){p->playing=!p->media->startPaused;p->state=p->media->location.find("poll-only")!=std::string::npos?libvlc_Error:p->playing?libvlc_Playing:libvlc_Paused;if(preparedFixture(p) && !p->media->menus && p->playing && p->title!=0)p->premature=true;return 0;}
FIXTURE_API void libvlc_media_player_stop(libvlc_media_player_t *p){p->playing=false;p->state=libvlc_Stopped;}
FIXTURE_API void libvlc_media_player_pause(libvlc_media_player_t *p){p->playing=!p->playing;}
FIXTURE_API void libvlc_media_player_set_pause(libvlc_media_player_t *p,int paused){p->playing=!paused;if(preparedFixture(p) && !p->media->menus && !paused && p->title!=0)p->premature=true;}
FIXTURE_API libvlc_time_t libvlc_media_player_get_time(libvlc_media_player_t *){return 1000;}
FIXTURE_API libvlc_time_t libvlc_media_player_get_length(libvlc_media_player_t *p){return preparedFixture(p)?titleDuration(p,p->title):0;}
FIXTURE_API void libvlc_media_player_set_time(libvlc_media_player_t *,libvlc_time_t){}
FIXTURE_API libvlc_state_t libvlc_media_player_get_state(libvlc_media_player_t *p){return p->state;}
FIXTURE_API int libvlc_media_player_is_playing(libvlc_media_player_t *p){return p->playing?1:0;}
FIXTURE_API int libvlc_media_player_set_rate(libvlc_media_player_t *,float){return 0;}
FIXTURE_API int libvlc_audio_set_volume(libvlc_media_player_t *,int){return 0;}
FIXTURE_API libvlc_track_description_t *libvlc_audio_get_track_description(libvlc_media_player_t *){return nullptr;}
FIXTURE_API int libvlc_audio_set_track(libvlc_media_player_t *,int){return 0;}
FIXTURE_API libvlc_track_description_t *libvlc_video_get_spu_description(libvlc_media_player_t *){return nullptr;}
FIXTURE_API int libvlc_video_set_spu(libvlc_media_player_t *,int){return 0;}
FIXTURE_API void libvlc_track_description_list_release(libvlc_track_description_t *){}
FIXTURE_API int libvlc_media_player_get_chapter_count(libvlc_media_player_t *p){return preparedFixture(p) && !p->media->menus && p->title>=0?3:0;}
FIXTURE_API void libvlc_media_player_set_chapter(libvlc_media_player_t *,int){}
FIXTURE_API int libvlc_media_player_get_full_chapter_descriptions(libvlc_media_player_t *p,int title,libvlc_chapter_description_t ***chapters){
    *chapters=nullptr;
    if(title!=p->title || libvlc_media_player_get_chapter_count(p)==0)return 0;
    *chapters=new libvlc_chapter_description_t*[3];
    (*chapters)[0]=new libvlc_chapter_description_t{0,0,nullptr};
    (*chapters)[1]=new libvlc_chapter_description_t{1100+500*title,0,nullptr};
    (*chapters)[2]=new libvlc_chapter_description_t{2750+500*title,0,nullptr};
    return 3;
}
FIXTURE_API void libvlc_chapter_descriptions_release(libvlc_chapter_description_t **chapters,unsigned count){for(unsigned i=0;i<count;++i)delete chapters[i];delete[] chapters;}
FIXTURE_API int libvlc_media_player_get_title(libvlc_media_player_t *p){if(p->pendingTitle>=0 && --p->ackReads<=0){p->title=p->pendingTitle;p->pendingTitle=-1;}return p->title;}
FIXTURE_API int libvlc_media_player_get_chapter(libvlc_media_player_t *){return -1;}
FIXTURE_API int libvlc_media_player_get_full_title_descriptions(libvlc_media_player_t *p,libvlc_title_description_t ***titles){
    *titles=nullptr;if(!preparedFixture(p))return 0;
    ++p->metadataReads;if(p->media->location.find("delayed")!=std::string::npos && p->metadataReads<3)return 0;
    const int count=p->media->menus?4:2;*titles=new libvlc_title_description_t*[count];
    for(int i=0;i<count;++i){auto *title=new libvlc_title_description_t{};title->i_duration=p->media->location.find("changing")!=std::string::npos && p->metadataReads>=2?(i==0?6297332:6296832):titleDuration(p,i);title->i_flags=p->media->menus && i==0?libvlc_title_menu:0;(*titles)[i]=title;}return count;
}
FIXTURE_API void libvlc_title_descriptions_release(libvlc_title_description_t **titles,unsigned count){for(unsigned i=0;i<count;++i)delete titles[i];delete[] titles;}
FIXTURE_API void libvlc_media_player_set_title(libvlc_media_player_t *p,int title){p->pendingTitle=title;p->ackReads=2;}
FIXTURE_API void libvlc_media_player_navigate(libvlc_media_player_t *,unsigned){}
FIXTURE_API void libvlc_video_set_key_input(libvlc_media_player_t *,unsigned){}
FIXTURE_API void libvlc_video_set_mouse_input(libvlc_media_player_t *,unsigned){}
FIXTURE_API int libvlc_media_player_add_slave(libvlc_media_player_t *,libvlc_media_slave_type_t,const char *,bool){return 0;}
FIXTURE_API unsigned libvlc_media_tracks_get(libvlc_media_t *,libvlc_media_track_t ***tracks){*tracks=nullptr;return 0;}
FIXTURE_API void libvlc_media_tracks_release(libvlc_media_track_t **,unsigned){}
FIXTURE_API int libvlc_media_get_stats(libvlc_media_t *,libvlc_media_stats_t *stats){*stats={};return 1;}
FIXTURE_API int libvlc_audio_get_track(libvlc_media_player_t *){return -1;}
FIXTURE_API int libvlc_video_get_spu(libvlc_media_player_t *){return -1;}
FIXTURE_API void libvlc_media_player_set_hwnd(libvlc_media_player_t *,void *){}
#else
#include <QtTest>
#include <QDir>
#include <QLibrary>
#include "media/playback.h"
using namespace luma;
class PlaybackTests:public QObject {
    Q_OBJECT
    using Failure=void (*)(const char *,int);
    using VideoLog=void (*)(const char *);
    QLibrary fixture_;
    Failure failure_=nullptr;
    VideoLog videoLog_=nullptr;
    using Query=int (*)();Query discMenus_=nullptr,premature_=nullptr;
    QString runtime()const{return QString::fromUtf8(LUMA_PLAYBACK_FIXTURE_RUNTIME);}
private slots:
    void initTestCase(){
        fixture_.setFileName(QDir(runtime()).filePath("vlc/libvlc.dll"));
        QVERIFY2(fixture_.load(),qPrintable(fixture_.errorString()));
        failure_=reinterpret_cast<Failure>(fixture_.resolve("luma_fixture_failure"));
        QVERIFY(failure_);
        videoLog_=reinterpret_cast<VideoLog>(fixture_.resolve("luma_fixture_video_log"));
        QVERIFY(videoLog_);
        discMenus_=reinterpret_cast<Query>(fixture_.resolve("luma_fixture_disc_menus"));premature_=reinterpret_cast<Query>(fixture_.resolve("luma_fixture_premature_playback"));QVERIFY(discMenus_);QVERIFY(premature_);
    }
    void preparedDiscSelectsExactNativeZero(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.openPreparedDisc("bluray:///prepared/target-zero",0,6296832));
        QTRY_COMPARE_WITH_TIMEOUT(engine.state().durationMs,6296832,3000);QCOMPARE(engine.state().currentTitle,0);QVERIFY(engine.state().timelineReady);QVERIFY(!engine.state().navigationTitle);QVERIFY(!engine.state().discMenus);QCOMPARE(discMenus_(),0);QCOMPARE(premature_(),0);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().playing,3000);
    }
    void preparedDiscDelayedSelectionKeepsManualPause(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.openPreparedDisc("bluray:///prepared/delayed",0,6296832));engine.setPaused(true);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().timelineReady,4000);QCOMPARE(engine.state().currentTitle,0);QVERIFY(!engine.state().playing);QCOMPARE(premature_(),0);engine.setPaused(false);QTRY_VERIFY(engine.state().playing);
    }
    void chapterOffsetsFollowSelectedNativeTitle(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());
        QVERIFY(engine.openPreparedDisc("bluray:///prepared/chapters",0,6296832));
        const QVector<qint64> firstTitle{0,1100,2750};
        QTRY_COMPARE_WITH_TIMEOUT(engine.state().chapterOffsetsMs,firstTitle,3000);
        QCOMPARE(engine.state().sourceVideoRange,QString("unknown"));
        engine.setTitle(1);
        const QVector<qint64> secondTitle{0,1600,3250};
        QTRY_COMPARE_WITH_TIMEOUT(engine.state().chapterOffsetsMs,secondTitle,3000);
        QCOMPARE(engine.state().currentTitle,1);
        engine.stop();
        QVERIFY(engine.state().chapterOffsetsMs.isEmpty());
    }
    void preparedDiscRejectsUnknownOrAmbiguousTitles_data(){QTest::addColumn<QString>("source");QTest::newRow("missing")<<QString("missing");QTest::newRow("ambiguous")<<QString("ambiguous");}
    void preparedDiscRejectsUnknownOrAmbiguousTitles(){
        QFETCH(QString,source);PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QSignalSpy errors(&engine,&PlaybackEngine::errorOccurred);QVERIFY(engine.openPreparedDisc("bluray:///prepared/"+source,0,6296832));QTRY_VERIFY_WITH_TIMEOUT(engine.state().failed,3000);QVERIFY(!engine.state().playing);QCOMPARE(errors.count(),1);QCOMPARE(premature_(),0);
    }
    void preparedDiscMetadataChangeCannotUnpauseTheCachedIndex(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QSignalSpy errors(&engine,&PlaybackEngine::errorOccurred);QVERIFY(engine.openPreparedDisc("bluray:///prepared/changing",0,6296832));QTRY_VERIFY_WITH_TIMEOUT(engine.state().failed,3000);QVERIFY(!engine.state().playing);QCOMPARE(errors.count(),1);
    }
    void ordinaryDiscOpenKeepsAuthoredMenuMode(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.open("bluray:///prepared/ordinary",0));QTRY_VERIFY(engine.state().navigationTitle);QVERIFY(engine.state().discMenus);QCOMPARE(discMenus_(),1);
    }
    void videoDeviceLossIsNonfatalAndDebounced_data(){
        QTest::addColumn<QByteArray>("message");QTest::addColumn<QString>("code");
        QTest::newRow("removed")<<QByteArray("SwapChain Present failed. (hr=0x887A0005)")<<QString("0x887a0005");
        QTest::newRow("reset")<<QByteArray("SwapChain Present failed. (hr=0x887A0007)")<<QString("0x887a0007");
        QTest::newRow("case-and-spacing")<<QByteArray("swapchain present failed. (hr = 0X887a0007)")<<QString("0x887a0007");
    }
    void videoDeviceLossIsNonfatalAndDebounced(){
        QFETCH(QByteArray,message);QFETCH(QString,code);
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QSignalSpy lost(&engine,SIGNAL(videoOutputLost(QString))),errors(&engine,&PlaybackEngine::errorOccurred),updates(&engine,&PlaybackEngine::updated);
        QVERIFY2(lost.isValid(),"PlaybackEngine must expose the nonfatal video-output-loss signal.");
        QVERIFY(engine.open("bluray:///fixture/healthy",0));QTRY_VERIFY(engine.state().playing);
        for(int i=0;i<20;++i)videoLog_(message.constData());
        QTRY_COMPARE(lost.count(),1);
        const auto reason=lost.first().first().toString();QVERIFY(reason.contains("영상 출력"));QVERIFY(reason.contains(code,Qt::CaseInsensitive));
        videoLog_("SwapChain Present failed. (hr=0x887A0005)");videoLog_("SwapChain Present failed. (hr=0x887A0007)");
        QTRY_VERIFY(updates.count()>=2);QCOMPARE(lost.count(),1);
        QVERIFY(engine.state().playing);QVERIFY(!engine.state().failed);QVERIFY(engine.lastError().isEmpty());QVERIFY(errors.isEmpty());
    }
    void unrelatedVideoErrorsDoNotRequestDeviceRecovery_data(){
        QTest::addColumn<QByteArray>("message");
        QTest::newRow("device-hung")<<QByteArray("SwapChain Present failed. (hr=0x887A0006)");
        QTest::newRow("invalid-call")<<QByteArray("SwapChain Present failed. (hr=0x887A0001)");
        QTest::newRow("desktop-duplication-loss")<<QByteArray("SwapChain Present failed. (hr=0x887A0026)");
        QTest::newRow("unrelated-module")<<QByteArray("decoder warning: hr=0x887A0005");
        QTest::newRow("not-a-failed-present")<<QByteArray("SwapChain Present succeeded. (hr=0x887A0005)");
        QTest::newRow("longer-hex-number")<<QByteArray("SwapChain Present failed. (hr=0x887A00050)");
    }
    void unrelatedVideoErrorsDoNotRequestDeviceRecovery(){
        QFETCH(QByteArray,message);
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QSignalSpy lost(&engine,SIGNAL(videoOutputLost(QString))),updates(&engine,&PlaybackEngine::updated);
        QVERIFY(lost.isValid());QVERIFY(engine.open("bluray:///fixture/healthy",0));videoLog_(message.constData());
        QTRY_VERIFY(updates.count()>=2);QVERIFY(lost.isEmpty());QVERIFY(!engine.state().failed);
    }
    void deviceLossKeepsPausedTransportIntent(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QSignalSpy lost(&engine,SIGNAL(videoOutputLost(QString)));QVERIFY(lost.isValid());
        QVERIFY(engine.open("bluray:///fixture/healthy",0));QTRY_VERIFY(engine.state().playing);
        engine.setPaused(true);QTRY_VERIFY(!engine.state().playing);
        videoLog_("SwapChain Present failed. (hr=0x887A0005)");QTRY_COMPARE(lost.count(),1);
        QVERIFY(!engine.state().playing);QVERIFY(!engine.state().failed);
    }
    void nextSuccessfulOpenAllowsOneNewDeviceLossNotification(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QSignalSpy lost(&engine,SIGNAL(videoOutputLost(QString)));QVERIFY(lost.isValid());
        QVERIFY(engine.open("bluray:///fixture/healthy",0));videoLog_("SwapChain Present failed. (hr=0x887A0005)");QTRY_COMPARE(lost.count(),1);
        QVERIFY(engine.open("bluray:///fixture/healthy",0));videoLog_("SwapChain Present failed. (hr=0x887A0007)");QTRY_COMPARE(lost.count(),2);
        QVERIFY(!engine.state().failed);
    }
    void queuedDeviceLossFromPreviousOpenIsIgnored(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QSignalSpy lost(&engine,SIGNAL(videoOutputLost(QString))),updates(&engine,&PlaybackEngine::updated);QVERIFY(lost.isValid());
        QVERIFY(engine.open("bluray:///fixture/healthy",0));videoLog_("SwapChain Present failed. (hr=0x887A0005)");
        QVERIFY(engine.open("bluray:///fixture/healthy",0));QTRY_VERIFY(updates.count()>=2);QVERIFY(lost.isEmpty());
        videoLog_("SwapChain Present failed. (hr=0x887A0005)");QTRY_COMPARE(lost.count(),1);
    }
    void queuedDeviceLossAfterStopIsIgnored(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QSignalSpy lost(&engine,SIGNAL(videoOutputLost(QString))),updates(&engine,&PlaybackEngine::updated);QVERIFY(lost.isValid());
        QVERIFY(engine.open("bluray:///fixture/healthy",0));videoLog_("SwapChain Present failed. (hr=0x887A0005)");engine.stop();
        videoLog_("SwapChain Present failed. (hr=0x887A0005)");
        QTRY_VERIFY(updates.count()>=2);QVERIFY(lost.isEmpty());
    }
    void configurationDetailSurvivesGenericLogAndPoll(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY2(engine.available(),qPrintable(engine.lastError()));
        QSignalSpy errors(&engine,&PlaybackEngine::errorOccurred),updates(&engine,&PlaybackEngine::updated);
        QVERIFY(engine.open("bluray:///fixture/healthy",0));
        failure_("Missing AACS configuration file!",0);
        QTRY_COMPARE(errors.count(),1);
        const auto detail=errors.first().first().toString();
        QVERIFY2(detail.startsWith("AACS 설정 파일을 찾거나 열 수 없습니다."),qPrintable(detail));
        QVERIFY(detail.contains("현재 사용자 계정의 AACS 설정을 확인한 뒤"));
        QCOMPARE(engine.lastError(),detail);
        QTRY_VERIFY(updates.count()>=2);
        QCOMPARE(engine.lastError(),detail);QCOMPARE(errors.count(),1);
    }
    void nativeDiscReasonsRemainSpecific_data(){
        QTest::addColumn<QByteArray>("native");QTest::addColumn<QString>("reason");
        QTest::newRow("aacs-library")<<QByteArray("This Blu-ray Disc needs a library for AACS decoding, and your system does not have it.")<<QString("AACS 읽기 라이브러리");
        QTest::newRow("processing-key")<<QByteArray("No valid processing key found in AACS config file.")<<QString("처리 키");
        QTest::newRow("host-certificate")<<QByteArray("No valid host certificate found in AACS config file.")<<QString("호스트 인증서");
        QTest::newRow("revoked-certificate")<<QByteArray("AACS Host certificate revoked.")<<QString("인증서가 폐기");
        QTest::newRow("mmc")<<QByteArray("AACS MMC failed.")<<QString("드라이브와 통신");
        QTest::newRow("bdplus-library")<<QByteArray("This Blu-ray Disc needs a library for BD+ decoding, and your system does not have it.")<<QString("BD+ 읽기 라이브러리");
        QTest::newRow("bdplus-decoding")<<QByteArray("Your system BD+ decoding library does not work. Missing configuration?")<<QString("BD+ 처리를 시작");
    }
    void nativeDiscReasonsRemainSpecific(){
        QFETCH(QByteArray,native);QFETCH(QString,reason);
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QSignalSpy errors(&engine,&PlaybackEngine::errorOccurred),updates(&engine,&PlaybackEngine::updated);
        QVERIFY(engine.open("bluray:///fixture/healthy",0));failure_(native.constData(),0);
        QTRY_COMPARE(errors.count(),1);
        const auto detail=errors.first().first().toString();QVERIFY2(detail.contains(reason),qPrintable(detail));
        QTRY_VERIFY(updates.count()>=2);QCOMPARE(engine.lastError(),detail);QCOMPARE(errors.count(),1);
    }
    void lateSpecificReasonReplacesAlreadyReportedGenericError(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QSignalSpy errors(&engine,&PlaybackEngine::errorOccurred),updates(&engine,&PlaybackEngine::updated);
        QVERIFY(engine.open("bluray:///fixture/healthy",0));failure_("Missing AACS configuration file!",1);
        QTRY_VERIFY(engine.lastError().contains("AACS 설정"));
        QCOMPARE(errors.last().first().toString(),engine.lastError());
        QTRY_VERIFY(updates.count()>=2);QCOMPARE(errors.count(),2);
    }
    void asynchronousFailureClearsPlayingImmediatelyAndAfterPoll(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QVERIFY(engine.open("bluray:///fixture/healthy",0));QTRY_VERIFY(engine.state().playing);
        QSignalSpy updates(&engine,&PlaybackEngine::updated);
        failure_("Missing AACS configuration file!",0);QTRY_VERIFY(engine.state().failed);
        QVERIFY(!engine.state().playing);QVERIFY(!engine.state().timelineReady);
        QTRY_VERIFY(updates.count()>=2);QVERIFY(!engine.state().playing);QVERIFY(!engine.state().timelineReady);
    }
    void pollFailureCannotAdvertisePlaying(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QVERIFY(engine.open("bluray:///fixture/poll-only",0));QTRY_VERIFY(engine.state().failed);
        QVERIFY(!engine.state().playing);QVERIFY(!engine.state().timelineReady);
    }
    void nextSuccessfulOpenClearsFailureAndLastError(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QVERIFY(engine.open("bluray:///fixture/healthy",0));failure_("Missing AACS configuration file!",0);QTRY_VERIFY(engine.state().failed);
        QVERIFY(engine.open("bluray:///fixture/healthy",0));
        QVERIFY(!engine.state().failed);QVERIFY2(engine.lastError().isEmpty(),qPrintable(engine.lastError()));
        QTRY_VERIFY(engine.state().playing);
    }
    void nextFailedOpenDoesNotInheritPreviousDiscReason(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QSignalSpy errors(&engine,&PlaybackEngine::errorOccurred);
        QVERIFY(engine.open("bluray:///fixture/healthy",0));failure_("Missing AACS configuration file!",0);QTRY_COMPARE(errors.count(),1);
        QVERIFY(engine.open("bluray:///fixture/healthy",0));failure_(nullptr,0);QTRY_COMPARE(errors.count(),2);
        QVERIFY(!engine.lastError().contains("AACS 설정"));
        QCOMPARE(errors.last().first().toString(),engine.lastError());
    }
    void queuedPreviousSessionDetailCannotPoisonNextOpen(){
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,runtime());QVERIFY(engine.available());
        QSignalSpy errors(&engine,&PlaybackEngine::errorOccurred),updates(&engine,&PlaybackEngine::updated);
        QVERIFY(engine.open("bluray:///fixture/healthy",0));failure_("Missing AACS configuration file!",0);
        QVERIFY(engine.open("bluray:///fixture/healthy",0));QTRY_VERIFY(updates.count()>=2);
        QVERIFY(!engine.state().failed);QVERIFY(engine.lastError().isEmpty());QVERIFY(errors.isEmpty());
    }
};
QTEST_GUILESS_MAIN(PlaybackTests)
#include "playback_tests.moc"
#endif
