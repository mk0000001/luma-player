#include <QtTest>
#include <QTemporaryDir>
#include "media/playback.h"
#include <vlc/vlc.h>
using namespace luma;
class MediaTests : public QObject {
    Q_OBJECT
private slots:
    void menuCommandsMatchNativeAbi() {
        QCOMPARE(discActionCode(DiscAction::Activate),int(libvlc_navigate_activate));QCOMPARE(discActionCode(DiscAction::Up),int(libvlc_navigate_up));
        QCOMPARE(discActionCode(DiscAction::Down),int(libvlc_navigate_down));QCOMPARE(discActionCode(DiscAction::Left),int(libvlc_navigate_left));
        QCOMPARE(discActionCode(DiscAction::Right),int(libvlc_navigate_right));QCOMPARE(discActionCode(DiscAction::Popup),int(libvlc_navigate_popup));
    }
    void sourceClassification() {
        QVERIFY(isDiscSource("E:")); QVERIFY(isDiscSource("bluray:///E:/"));
        QVERIFY(!isDiscSource("C:/Videos/日本語 film.mkv")); QVERIFY(!isDiscSource("https://example.org/video.mp4"));
    }
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
        for(const auto kind:{PlaybackEngine::Kind::Disc,PlaybackEngine::Kind::File}){PlaybackEngine engine(kind,runtime);QVERIFY2(engine.available(),qPrintable(engine.lastError()));}
    }
};
QTEST_GUILESS_MAIN(MediaTests)
#include "media_tests.moc"
