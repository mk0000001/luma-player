#include <QtTest>
#include <QWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include "media/playback.h"
#include "core/disc.h"
#include "subtitles/subtitle_job.h"
using namespace luma;
namespace {
void report(const QString &suffix,const PlaybackState &s){
    const auto prefix=qEnvironmentVariable("LUMA_NATIVE_REPORT");if(prefix.isEmpty())return;
    QFile out(prefix+"-"+suffix+".json");if(out.open(QIODevice::WriteOnly))out.write(QJsonDocument(QJsonObject{{"durationMs",s.durationMs},{"positionMs",s.positionMs},{"title",s.currentTitle},{"chapter",s.currentChapter},{"chapters",s.chapters.size()},{"decodedVideo",s.decodedVideo},{"displayedPictures",s.displayedPictures},{"decodedAudio",s.decodedAudio},{"playedAudioBuffers",s.playedAudioBuffers},{"videoCodec",s.videoCodec},{"audioCodec",s.audioCodec},{"hardwareDecoder",s.hardwareDecoder}}).toJson());
}
}
class NativePlaybackTests:public QObject {
    Q_OBJECT
private slots:
    void physicalDiscMenusAndTransport(){
        const auto disc=qEnvironmentVariable("LUMA_NATIVE_DISC");if(disc.isEmpty())QSKIP("Physical disc test requires LUMA_NATIVE_DISC.");
        QWidget video;video.setWindowTitle("LumaPlayer · physical disc verification");video.resize(960,540);video.show();
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,qEnvironmentVariable("LUMA_TEST_RUNTIME"));QVERIFY2(engine.available(),qPrintable(engine.lastError()));QVERIFY(engine.open(disc,video.winId()));
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().titles.size()>=3,30000);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().currentTitle==0 && engine.state().playing && !engine.state().videoCodec.isEmpty(),45000);
        engine.navigate(discActionCode(DiscAction::Activate));
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().durationMs>3600000,20000);report("disc-started",engine.state());
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().decodedVideo>30 && engine.state().decodedAudio>30,15000);
        QVERIFY(engine.state().chapters.size()>1);report("disc-playing",engine.state());
        engine.pauseToggle();QTRY_VERIFY_WITH_TIMEOUT(!engine.state().playing,5000);const auto paused=engine.state().positionMs;QTest::qWait(700);QVERIFY(qAbs(engine.state().positionMs-paused)<300);
        engine.seek(60000);QTRY_VERIFY_WITH_TIMEOUT(qAbs(engine.state().positionMs-60000)<2500,10000);
        engine.pauseToggle();QTRY_VERIFY_WITH_TIMEOUT(engine.state().playing,5000);engine.setChapter(2);QTRY_COMPARE_WITH_TIMEOUT(engine.state().currentChapter,2,12000);report("disc-chapter",engine.state());
        engine.navigate(discActionCode(DiscAction::Popup));QTest::qWait(700);QVERIFY(!engine.state().failed);
        engine.setTitle(0);QTRY_COMPARE_WITH_TIMEOUT(engine.state().currentTitle,0,12000);report("disc-menu",engine.state());engine.stop();
    }
    void av1FileAndTransport(){
        const auto file=qEnvironmentVariable("LUMA_NATIVE_AV1");if(file.isEmpty())QSKIP("Native AV1 test requires LUMA_NATIVE_AV1.");
        QWidget video;video.setWindowTitle("LumaPlayer · AV1 verification");video.resize(960,540);video.show();PlaybackEngine engine(PlaybackEngine::Kind::File,qEnvironmentVariable("LUMA_TEST_RUNTIME"));
        QVERIFY2(engine.available(),qPrintable(engine.lastError()));QVERIFY(engine.open(file,video.winId()));QTRY_VERIFY_WITH_TIMEOUT(engine.state().positionMs>600 && !engine.state().videoCodec.isEmpty(),15000);
        QVERIFY(engine.state().videoCodec.contains("av1",Qt::CaseInsensitive) || engine.state().videoCodec.contains("dav1d",Qt::CaseInsensitive));report("av1",engine.state());
        engine.pauseToggle();QTRY_VERIFY_WITH_TIMEOUT(!engine.state().playing,3000);engine.seek(3000);QTRY_VERIFY_WITH_TIMEOUT(qAbs(engine.state().positionMs-3000)<500,5000);engine.pauseToggle();QTRY_VERIFY_WITH_TIMEOUT(engine.state().playing,3000);engine.stop();
    }
    void realLocalRecognition(){
        const auto speech=qEnvironmentVariable("LUMA_NATIVE_SPEECH");if(speech.isEmpty())QSKIP("Real inference requires LUMA_NATIVE_SPEECH and a model.");
        SubtitleJob job(qEnvironmentVariable("LUMA_TEST_RUNTIME"));QSignalSpy results(&job,&SubtitleJob::ready);QSignalSpy errors(&job,&SubtitleJob::failed);
        RecognitionRequest r;r.source=speech;r.language="en";r.durationMs=11000;job.start(r);job.cancel();const auto current=job.start(r);
        QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty() || !errors.isEmpty(),30000);QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors[0][0].toString()));QCOMPARE(results.size(),1);QCOMPARE(results[0][0].toULongLong(),current);
        const auto cues=qvariant_cast<QVector<SubtitleCue>>(results[0][1]);QVERIFY(!cues.isEmpty());QString words;for(const auto &cue:cues){words+=cue.text;QVERIFY(cue.startMs>=0);QVERIFY(cue.endMs<=11000);}QVERIFY(words.contains("country",Qt::CaseInsensitive));
    }
    void physicalDiscCaptionAcquisition(){
        const auto disc=qEnvironmentVariable("LUMA_NATIVE_DISC");if(disc.isEmpty())QSKIP("Physical caption test requires the TUKI disc.");
        RecognitionRequest r;r.source=disc;r.disc=true;r.playlist=matchDiscPlaylist(inspectDisc(disc),6294288);r.startMs=60000;r.durationMs=6000;r.language="ja";r.audioTrackId=4352;
        QVERIFY(!r.playlist.isEmpty());SubtitleJob job(qEnvironmentVariable("LUMA_TEST_RUNTIME"));QSignalSpy results(&job,&SubtitleJob::ready);QSignalSpy errors(&job,&SubtitleJob::failed);job.start(r);
        QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty() || !errors.isEmpty(),45000);QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors[0][0].toString()));QCOMPARE(results.size(),1);
        const auto cues=qvariant_cast<QVector<SubtitleCue>>(results[0][1]);for(const auto &cue:cues){QVERIFY(cue.startMs>=60000);QVERIFY(cue.endMs<=66000);}
        const auto prefix=qEnvironmentVariable("LUMA_NATIVE_REPORT");if(!prefix.isEmpty()){QFile out(prefix+"-disc-ai.json");if(out.open(QIODevice::WriteOnly))out.write(QJsonDocument(QJsonObject{{"playlist",r.playlist},{"startMs",60000},{"durationMs",6000},{"cueCount",cues.size()},{"completed",true}}).toJson());}
    }
};
QTEST_MAIN(NativePlaybackTests)
#include "native_playback_tests.moc"
