#include <QtTest>
#include <QTemporaryDir>
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
        RecognitionRequest r;r.source="E:/";r.disc=true;r.title=2;
        const auto c=extractionCommand("runtime",r,"audio.wav");
        QVERIFY(c.arguments.contains("bluray:///E:/#2"));QVERIFY(c.arguments.contains("--sout-std-dst=audio.wav"));
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
};
QTEST_GUILESS_MAIN(SubtitleJobTests)
#include "subtitle_job_tests.moc"
