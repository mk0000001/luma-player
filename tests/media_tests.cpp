#include <QtTest>
#include <QTemporaryDir>
#include <QUrl>
#include <QDir>
#include "media/playback.h"
#include "media/system_metrics_p.h"
#include <vlc/vlc.h>
using namespace luma;
class MediaTests : public QObject {
    Q_OBJECT
private slots:
    void networkRatesUseMatchedByteDeltasAndElapsedTime() {
        NetworkRateSampler sampler;
        const auto first = sampler.sample({{10, {10000, 5000}}, {20, {90000, 1000}}}, 0);
        QVERIFY(!first.receivedBytesPerSecond);
        QVERIFY(!first.sentBytesPerSecond);
        const auto next = sampler.sample({{10, {14000, 8000}}, {20, {92000, 2000}}}, 2000000000);
        QCOMPARE(next.receivedBytesPerSecond.value_or(-1), 3000.0);
        QCOMPARE(next.sentBytesPerSecond.value_or(-1), 2000.0);
        const auto idle = sampler.sample({{10, {14000, 8000}}, {20, {92000, 2000}}}, 3000000000);
        QCOMPARE(idle.receivedBytesPerSecond.value_or(-1), 0.0);
        QCOMPARE(idle.sentBytesPerSecond.value_or(-1), 0.0);
    }
    void networkRatesRecoverAfterNewInterfacesAndCounterResets() {
        NetworkRateSampler sampler;
        sampler.sample({{10, {10000, 10000}}}, 0);
        const auto added = sampler.sample({{10, {11000, 11000}}, {20, {90000, 90000}}}, 1000000000);
        QVERIFY(!added.receivedBytesPerSecond);
        QVERIFY(!added.sentBytesPerSecond);
        const auto reset = sampler.sample({{10, {12000, 12000}}, {20, {1000, 91000}}}, 2000000000);
        QVERIFY(!reset.receivedBytesPerSecond);
        QCOMPARE(reset.sentBytesPerSecond.value_or(-1), 2000.0);
        const auto recovered = sampler.sample({{10, {13000, 13000}}, {20, {2000, 92000}}}, 3000000000);
        QCOMPARE(recovered.receivedBytesPerSecond.value_or(-1), 2000.0);
        QCOMPARE(recovered.sentBytesPerSecond.value_or(-1), 2000.0);
        const auto disconnected = sampler.sample({}, 4000000000);
        QVERIFY(!disconnected.receivedBytesPerSecond);
        QVERIFY(!disconnected.sentBytesPerSecond);
        const auto reconnected = sampler.sample({{10, {70000, 70000}}}, 5000000000);
        QVERIFY(!reconnected.receivedBytesPerSecond);
        QVERIFY(!reconnected.sentBytesPerSecond);
        const auto resumed = sampler.sample({{10, {80000, 80000}}}, 11000000000);
        QVERIFY(!resumed.receivedBytesPerSecond);
        QVERIFY(!resumed.sentBytesPerSecond);
    }
    void timelineStartsUnavailable(){
        const PlaybackState state;QVERIFY(!state.timelineReady);
    }
    void discTimelineRejectsStaleLengthAfterTitleChanges_data(){
        QTest::addColumn<qint64>("liveMs");QTest::addColumn<qint64>("titleMs");QTest::addColumn<bool>("navigation");QTest::addColumn<bool>("ready");
        constexpr qint64 mainTitle=6294288;
        QTest::newRow("new-main-id-with-old-menu-length")<<qint64(20000)<<mainTitle<<false<<false;
        QTest::newRow("main-live-length-now-matches")<<mainTitle<<mainTitle<<false<<true;
        QTest::newRow("unknown-current-title-metadata")<<mainTitle<<qint64(0)<<false<<false;
        QTest::newRow("no-live-length-yet")<<qint64(0)<<mainTitle<<false<<false;
        QTest::newRow("negative-live-length")<<qint64(-1)<<mainTitle<<false<<false;
        QTest::newRow("negative-metadata-length")<<mainTitle<<qint64(-1)<<false<<false;
        QTest::newRow("menu-is-never-main-timeline")<<qint64(20000)<<qint64(20000)<<true<<false;
        QTest::newRow("first-play-is-never-main-timeline")<<mainTitle<<mainTitle<<true<<false;
        QTest::newRow("native-rounding-below-inclusive")<<qint64(mainTitle-2000)<<mainTitle<<false<<true;
        QTest::newRow("native-rounding-above-inclusive")<<qint64(mainTitle+2000)<<mainTitle<<false<<true;
        QTest::newRow("stale-below-tolerance")<<qint64(mainTitle-2001)<<mainTitle<<false<<false;
        QTest::newRow("stale-above-tolerance")<<qint64(mainTitle+2001)<<mainTitle<<false<<false;
    }
    void discTimelineRejectsStaleLengthAfterTitleChanges(){
        QFETCH(qint64,liveMs);QFETCH(qint64,titleMs);QFETCH(bool,navigation);QFETCH(bool,ready);
        QCOMPARE(discTimelineReady(liveMs,titleMs,navigation),ready);
    }
    void menuCommandsMatchNativeAbi() {
        QCOMPARE(discActionCode(DiscAction::Activate),int(libvlc_navigate_activate));QCOMPARE(discActionCode(DiscAction::Up),int(libvlc_navigate_up));
        QCOMPARE(discActionCode(DiscAction::Down),int(libvlc_navigate_down));QCOMPARE(discActionCode(DiscAction::Left),int(libvlc_navigate_left));
        QCOMPARE(discActionCode(DiscAction::Right),int(libvlc_navigate_right));QCOMPARE(discActionCode(DiscAction::Popup),int(libvlc_navigate_popup));
    }
    void sourceClassification() {
        QVERIFY(isDiscSource("E:")); QVERIFY(isDiscSource("bluray:///E:/"));
        QVERIFY(!isDiscSource("C:/Videos/日本語 film.mkv")); QVERIFY(!isDiscSource("https://example.org/video.mp4"));
    }
    void mediaSourceValidation(){
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto local=dir.filePath("sample #1.mkv");
        const auto encoded=QUrl::fromLocalFile(local).toString(QUrl::FullyEncoded);
        QCOMPARE(normalizedPlaybackSource(encoded,false).value_or(QString{}),local);
        const QString stream="https://example.invalid/media.mp4";
        QCOMPARE(normalizedPlaybackSource(stream,false).value_or(QString{}),stream);
        QVERIFY(!normalizedPlaybackSource(stream,true));
        QVERIFY(!normalizedPlaybackSource("custom://example.invalid/media.mp4",false));
        QVERIFY(!normalizedPlaybackSource("https://",false));
        QVERIFY(!normalizedPlaybackSource("",false));
    }
    void discUrisUseTheirSchemeRatherThanSlashCount() {
        QVERIFY(isDiscSource("bluray:/tmp/owned-disc"));
        QVERIFY(isDiscSource("BLURAY:/tmp/owned-disc"));
        auto uri=QUrl::fromLocalFile(QDir::tempPath()+QString::fromUtf8("/日本語 owned disc"));uri.setScheme("bluray");
        QVERIFY(isDiscSource(uri.toString(QUrl::FullyEncoded)));
        QVERIFY(!isDiscSource("file:/tmp/ordinary-video.mkv"));
    }
#ifdef Q_OS_LINUX
    void systemRuntimesSurviveRepeatedConstruction() {
        // CI installs real system libraries/plugins. A missing/broken runtime is
        // a failure here, not an opt-in skip or a substituted fake implementation.
        for(int iteration=0;iteration<12;++iteration){
            PlaybackEngine disc(PlaybackEngine::Kind::Disc),file(PlaybackEngine::Kind::File);
            QVERIFY2(disc.available(),qPrintable(QString("libVLC iteration %1: %2").arg(iteration).arg(disc.lastError())));
            QVERIFY2(file.available(),qPrintable(QString("libmpv iteration %1: %2").arg(iteration).arg(file.lastError())));
            QSignalSpy updates(&disc,&PlaybackEngine::updated);
            QTRY_VERIFY_WITH_TIMEOUT(!updates.isEmpty(),3000);
            QVERIFY(!disc.state().failed);QVERIFY(!file.state().failed);
        }
        QCoreApplication::processEvents();
    }
#endif
    void missingRuntimeIsRecoverable() {
        QTemporaryDir dir;
        for (const auto kind : {PlaybackEngine::Kind::Disc, PlaybackEngine::Kind::File}) {
            PlaybackEngine engine(kind,dir.path()); QVERIFY(!engine.available()); QVERIFY(!engine.lastError().isEmpty());
            QSignalSpy errors(&engine,&PlaybackEngine::errorOccurred);
            QVERIFY(!engine.open("missing.mkv",0)); QCOMPARE(errors.count(),1);
            engine.stop(); engine.pauseToggle(); engine.seek(-1); engine.setVolume(100); engine.navigate(0);
        }
    }
    void bundledRuntimesLoad() {
        const auto runtime=qEnvironmentVariable("LUMA_TEST_RUNTIME");if(runtime.isEmpty())QSKIP("Set LUMA_TEST_RUNTIME for the native runtime integration check.");
        for(int iteration=0;iteration<12;++iteration)
            for(const auto kind:{PlaybackEngine::Kind::Disc,PlaybackEngine::Kind::File}){PlaybackEngine engine(kind,runtime);QVERIFY2(engine.available(),qPrintable(engine.lastError()));}
    }
};
QTEST_GUILESS_MAIN(MediaTests)
#include "media_tests.moc"
