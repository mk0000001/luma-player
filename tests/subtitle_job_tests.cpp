#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "subtitles/subtitle_job.h"
using namespace luma;
class SubtitleJobTests : public QObject {
    Q_OBJECT
private slots:
    void literalUnicodeArguments() {
        RecognitionRequest r; r.source=QString::fromUtf8("C:/영화/日本語 & $film.mkv");r.startMs=1250;r.durationMs=5000;
        const auto c=extractionCommand("runtime",r,"out file.wav");
        const int input=c.arguments.indexOf("-i");QVERIFY(input>=0);QCOMPARE(c.arguments[input+1],r.source);
        QCOMPARE(c.arguments[c.arguments.indexOf("-ss")+1],QString("1.250"));QCOMPARE(c.arguments.last(),QString("out file.wav"));
        const auto w=recognitionCommand("runtime","model with spaces.bin","ja","wave.wav","out");
        QCOMPARE(w.arguments[w.arguments.indexOf("-m")+1],QString("model with spaces.bin"));QVERIFY(!w.arguments.contains("--translate"));
    }
    void discUsesNavigationTitle() {
        RecognitionRequest r;r.source="E:/";r.disc=true;r.title=2;r.playlist="00003";
        const auto c=extractionCommand("runtime",r,"audio.wav");
        QVERIFY(c.arguments.contains("bluray:E:/"));QCOMPARE(c.arguments[c.arguments.indexOf("-playlist")+1],QString("3"));QCOMPARE(c.arguments.last(),QString("audio.wav"));
    }
    void captionsFollowSelectedAudio() {
        RecognitionRequest r;r.source="movie.mkv";r.audioIndex=1;r.audioTrackId=4353;
        auto c=extractionCommand("runtime",r,"out.wav");QCOMPARE(c.arguments[c.arguments.indexOf("-map")+1],QString("0:a:1"));
        r.disc=true;r.source="E:/";r.playlist="00003";c=extractionCommand("runtime",r,"out.wav");QCOMPARE(c.arguments[c.arguments.indexOf("-map")+1],QString("0:i:4353"));
    }
    void shiftedCueSerialization() {
        const auto text=writeSrt({{61200,63400,QString::fromUtf8("こんにちは\n안녕하세요")}});
        QVERIFY(text.contains("00:01:01,200 --> 00:01:03,400"));const auto parsed=parseSrt(text);QCOMPARE(parsed.size(),1);QCOMPARE(parsed[0].startMs,61200);
    }
    void missingModelAndCancellation() {
        QTemporaryDir dir;SubtitleJob job(dir.path());QSignalSpy errors(&job,&SubtitleJob::failed);
        RecognitionRequest r;r.source="missing.mkv";job.start(r,"missing-model.bin");QCOMPARE(errors.count(),1);QVERIFY(!job.busy());
        const auto old=job.generation();job.cancel();QVERIFY(job.generation()>old);QVERIFY(!job.busy());
    }
    void runningCancellationAndDeadline() {
        QTemporaryDir dir;QDir(dir.path()).mkpath("tools");QDir(dir.path()).mkpath("whisper");
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        const auto fixture=QCoreApplication::applicationDirPath()+"/subtitle_worker_fixture"+suffix;
        QVERIFY(QFile::copy(fixture,dir.filePath("tools/ffmpeg"+suffix)));QVERIFY(QFile::copy(fixture,dir.filePath("whisper/whisper-cli"+suffix)));
        QFile model(dir.filePath("model.bin"));QVERIFY(model.open(QIODevice::WriteOnly));model.write("fixture model");model.close();
        SubtitleJob job(dir.path());job.setStageTimeouts(120,1000);QSignalSpy errors(&job,&SubtitleJob::failed);QSignalSpy results(&job,&SubtitleJob::ready);
        RecognitionRequest r;r.source="stall.wav";job.start(r,model.fileName());QTRY_COMPARE_WITH_TIMEOUT(errors.count(),1,1500);QVERIFY(!job.busy());
        job.setStageTimeouts(1000,1000);job.start(r,model.fileName());job.cancel();r.source="speech.wav";r.startMs=1200;const auto generation=job.start(r,model.fileName());
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);QCOMPARE(results[0][0].toULongLong(),generation);const auto cues=qvariant_cast<QVector<SubtitleCue>>(results[0][1]);QCOMPARE(cues.size(),1);QCOMPARE(cues[0].startMs,1200);QCOMPARE(errors.count(),1);
    }
};
QTEST_GUILESS_MAIN(SubtitleJobTests)
#include "subtitle_job_tests.moc"
