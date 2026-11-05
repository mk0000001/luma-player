#include <QtTest>
#include <QTemporaryDir>
#include <QUrl>
#include <QDir>
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
