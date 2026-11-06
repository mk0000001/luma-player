#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QScopeGuard>
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
    void nativeTokenTimesReachThePlayer_data(){QTest::addColumn<QString>("mode");QTest::newRow("valid")<<QString("valid");QTest::newRow("invalid-falls-back")<<QString("invalid");}
    void nativeTokenTimesReachThePlayer(){
        QFETCH(QString,mode);QTemporaryDir dir;QVERIFY(QDir(dir.path()).mkpath("tools"));QVERIFY(QDir(dir.path()).mkpath("whisper"));
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        const auto fixture=QCoreApplication::applicationDirPath()+"/subtitle_worker_fixture"+suffix;
        for(const auto &tool:{"tools/ffmpeg","whisper/whisper-cli"})QVERIFY(QFile::copy(fixture,dir.filePath(QString(tool)+suffix)));
        QFile model(dir.filePath("model.bin"));QVERIFY(model.open(QIODevice::WriteOnly));model.write("fixture");model.close();
        qputenv("LUMA_FIXTURE_TOKEN_TIMINGS",mode.toUtf8());const auto cleanup=qScopeGuard([]{qunsetenv("LUMA_FIXTURE_TOKEN_TIMINGS");});
        SubtitleJob job(dir.path());job.setPreferences({"cpu",{}});QSignalSpy ready(&job,&SubtitleJob::ready),errors(&job,&SubtitleJob::failed);
        RecognitionRequest request;request.source="speech.wav";request.startMs=60000;request.durationMs=3000;job.start(request,model.fileName());
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,3000);QCOMPARE(errors.count(),0);const auto cues=qvariant_cast<QVector<SubtitleCue>>(ready[0][1]);QCOMPARE(cues.size(),1);
        if(mode=="valid"){
            QCOMPARE(cues[0].text,QString("ABCDEFGH"));QCOMPARE(cues[0].startMs,qint64(60200));QCOMPARE(cues[0].endMs,qint64(62000));QCOMPARE(cues[0].tokens.size(),2);
            QCOMPARE(cues[0].tokens[1],(SubtitleTokenTiming{4,4,61800,62000}));
        }else{QVERIFY(cues[0].text.startsWith("Fixture transcription"));QCOMPARE(cues[0].startMs,qint64(60000));QVERIFY(cues[0].tokens.isEmpty());}
    }
    void missingModelAndCancellation() {
        QTemporaryDir dir;SubtitleJob job(dir.path());QSignalSpy errors(&job,&SubtitleJob::failed);
        RecognitionRequest r;r.source="missing.mkv";job.start(r,"missing-model.bin");QCOMPARE(errors.count(),1);QVERIFY(!job.busy());
        const auto old=job.generation();job.cancel();QVERIFY(job.generation()>old);QVERIFY(!job.busy());
    }
    void nativeCpuPreservesTimedOutputWithoutLegacyCli(){
        QTemporaryDir dir;QVERIFY(QDir(dir.path()).mkpath("tools"));QVERIFY(QDir(dir.path()).mkpath("ai"));
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        const auto fixture=QCoreApplication::applicationDirPath()+"/subtitle_worker_fixture"+suffix;
        for(const auto &tool:{"tools/ffmpeg","ai/luma-whisper-worker"})QVERIFY(QFile::copy(fixture,dir.filePath(QString(tool)+suffix)));
        QFile model(dir.filePath("model.bin"));QVERIFY(model.open(QIODevice::WriteOnly));model.write("fixture");model.close();
        qputenv("LUMA_FIXTURE_TOKEN_TIMINGS","valid");const auto cleanup=qScopeGuard([]{qunsetenv("LUMA_FIXTURE_TOKEN_TIMINGS");});
        SubtitleJob job(dir.path());job.setPreferences({"cpu",{}});QSignalSpy ready(&job,&SubtitleJob::ready),errors(&job,&SubtitleJob::failed);
        RecognitionRequest request;request.source="speech.wav";request.durationMs=3000;job.start(request,model.fileName());
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,3000);QCOMPARE(errors.count(),0);QCOMPARE(job.selectedBackend(),QString("CPU"));
        const auto cues=qvariant_cast<QVector<SubtitleCue>>(ready[0][1]);QCOMPARE(cues[0].text,QString("ABCDEFGH"));QCOMPARE(cues[0].tokens.size(),2);
    }
    void nativeCpuFailureUsesCleanLegacyOutput(){
        QTemporaryDir dir;for(const auto &folder:{"tools","whisper","ai"})QVERIFY(QDir(dir.path()).mkpath(folder));
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        const auto fixture=QCoreApplication::applicationDirPath()+"/subtitle_worker_fixture"+suffix;
        for(const auto &tool:{"tools/ffmpeg","whisper/whisper-cli","ai/luma-whisper-worker"})QVERIFY(QFile::copy(fixture,dir.filePath(QString(tool)+suffix)));
        QFile model(dir.filePath("model.bin"));QVERIFY(model.open(QIODevice::WriteOnly));model.write("fixture");model.close();
        qputenv("LUMA_FIXTURE_TOKEN_TIMINGS","native-only");const auto cleanup=qScopeGuard([]{qunsetenv("LUMA_FIXTURE_TOKEN_TIMINGS");});
        SubtitleJob job(dir.path());job.setPreferences({"cpu",{}});QSignalSpy ready(&job,&SubtitleJob::ready),errors(&job,&SubtitleJob::failed);
        RecognitionRequest request;request.source="silent-cpu.wav";request.durationMs=3000;job.start(request,model.fileName());
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !errors.isEmpty(),3000);QCOMPARE(errors.size(),0);QCOMPARE(ready.size(),1);
        const auto cues=qvariant_cast<QVector<SubtitleCue>>(ready[0][1]);QCOMPARE(cues.size(),1);
        QVERIFY(cues[0].text.startsWith("Fixture transcription"));QVERIFY(cues[0].tokens.isEmpty());QCOMPARE(job.selectedAlignment(),QString("segment"));
        QCOMPARE(job.selectedBackend(),QString("CPU"));
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
    void acceleratorFallback_data(){
        QTest::addColumn<QString>("source");QTest::addColumn<QString>("backend");
        QTest::newRow("NPU-success")<<QString("accelerated.wav")<<QString("NPU");
        QTest::newRow("NPU-unavailable")<<QString("npu-fails.wav")<<QString("GPU");
        QTest::newRow("silent-CPU-fallback")<<QString("silent-cpu.wav")<<QString("CPU");
        QTest::newRow("NPU-deadline")<<QString("stall-npu.wav")<<QString("GPU");
    }
    void acceleratorFallback(){
        QFETCH(QString,source);QFETCH(QString,backend);QTemporaryDir dir;
        for(const auto &folder:{"tools","whisper","ai","models"})QVERIFY(QDir(dir.path()).mkpath(folder));
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        const auto fixture=QCoreApplication::applicationDirPath()+"/subtitle_worker_fixture"+suffix;
        for(const auto &tool:{"tools/ffmpeg","whisper/whisper-cli","ai/luma-whisper-worker"})QVERIFY(QFile::copy(fixture,dir.filePath(QString(tool)+suffix)));
        for(const auto &name:{"ggml-base.bin","ggml-base-encoder-openvino.xml","ggml-base-encoder-openvino.bin"}){QFile file(dir.filePath("models/"+QString(name)));QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");}
        // Persistent workers inherit their environment once, before their first launch.
        const auto working=dir.filePath("cancel-working");qputenv("LUMA_FIXTURE_ASR_WORKING",QFile::encodeName(working));
        const auto cleanup=qScopeGuard([]{qunsetenv("LUMA_FIXTURE_ASR_WORKING");});
        SubtitleJob job(dir.path());job.setStageTimeouts(1500,1500);job.setAcceleratorTimeout(1200);
        QSignalSpy errors(&job,&SubtitleJob::failed),results(&job,&SubtitleJob::ready),selection(&job,&SubtitleJob::backendSelected);
        RecognitionRequest request;request.source=source;const auto generation=job.start(request);
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,4000);QCOMPARE(errors.count(),0);QCOMPARE(selection.count(),1);QCOMPARE(selection[0][0].toString(),backend);QCOMPARE(job.selectedBackend(),backend);QCOMPARE(results[0][0].toULongLong(),generation);QVERIFY(!job.busy());
        // Cancel after the fixture acknowledges in-flight inference on every backend.
        request.source="cancel-running.wav";job.start(request);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(working).size()>0,3000);QVERIFY(job.busy());QCOMPARE(results.count(),1);job.cancel();
        request.source="accelerated.wav";const auto current=job.start(request);QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,4000);QCOMPARE(results[1][0].toULongLong(),current);
    }
    void persistentWorkerReusesModelBetweenChunks(){
        QTemporaryDir dir;for(const auto &folder:{"tools","whisper","ai","models"})QVERIFY(QDir(dir.path()).mkpath(folder));
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        const auto fixture=QCoreApplication::applicationDirPath()+"/subtitle_worker_fixture"+suffix;
        for(const auto &tool:{"tools/ffmpeg","whisper/whisper-cli","ai/luma-whisper-worker"})QVERIFY(QFile::copy(fixture,dir.filePath(QString(tool)+suffix)));
        for(const auto &name:{"ggml-base.bin","ggml-base-encoder-openvino.xml","ggml-base-encoder-openvino.bin"}){QFile file(dir.filePath("models/"+QString(name)));QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");}
        SubtitleJob job(dir.path());QSignalSpy results(&job,&SubtitleJob::ready),errors(&job,&SubtitleJob::failed);RecognitionRequest request;request.source="speech.wav";
        job.start(request);QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);
        request.startMs=1000;job.start(request);QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,3000);
        QCOMPARE(errors.count(),0);QVERIFY(qvariant_cast<QVector<SubtitleCue>>(results[1][1])[0].text.contains("request 2"));
        QSignalSpy started(&job,&SubtitleJob::recognitionStarted);request.source="cancel-running.wav";job.start(request);QTRY_COMPARE_WITH_TIMEOUT(started.count(),1,3000);job.cancel();
        request.source="speech.wav";job.start(request);QTRY_COMPARE_WITH_TIMEOUT(results.count(),3,3000);
        QVERIFY2(qvariant_cast<QVector<SubtitleCue>>(results[2][1])[0].text.contains("request 4"),"Cancel must discard only the old result while keeping the initialized model");
    }
    void prewarmSurvivesSeekAndOnlyLatestRequestRuns(){
        QTemporaryDir dir;for(const auto &folder:{"tools","whisper","ai","models"})QVERIFY(QDir(dir.path()).mkpath(folder));
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        const auto fixture=QCoreApplication::applicationDirPath()+"/subtitle_worker_fixture"+suffix;
        for(const auto &tool:{"tools/ffmpeg","whisper/whisper-cli","ai/luma-whisper-worker"})QVERIFY(QFile::copy(fixture,dir.filePath(QString(tool)+suffix)));
        for(const auto &name:{"ggml-base.bin","ggml-base-encoder-openvino.xml","ggml-base-encoder-openvino.bin"}){QFile file(dir.filePath("models/"+QString(name)));QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");}
        const auto starts=dir.filePath("starts"),release=dir.filePath("warmup-release");qputenv("LUMA_FIXTURE_ASR_STARTS",QFile::encodeName(starts));qputenv("LUMA_FIXTURE_WARMUP_GATE",QFile::encodeName(release));
        const auto cleanup=qScopeGuard([]{qunsetenv("LUMA_FIXTURE_ASR_STARTS");qunsetenv("LUMA_FIXTURE_WARMUP_GATE");});
        SubtitleJob job(dir.path());QSignalSpy warm(&job,&SubtitleJob::warmedUp),ready(&job,&SubtitleJob::ready),backend(&job,&SubtitleJob::backendSelected),errors(&job,&SubtitleJob::failed);
        job.warmUp();QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(starts).size()>0,3000);QTest::qWait(30);QVERIFY(!job.busy());QCOMPARE(warm.count(),0);QCOMPARE(backend.count(),0);
        RecognitionRequest request;request.source="speech.wav";job.start(request);QTest::qWait(80);job.cancel();QCOMPARE(warm.count(),0);QCOMPARE(ready.count(),0);
        request.startMs=1000;job.start(request);QTest::qWait(80);job.cancel();request.startMs=2000;const auto generation=job.start(request);QCOMPARE(warm.count(),0);QCOMPARE(ready.count(),0);
        QFile gate(release);QVERIFY(gate.open(QIODevice::WriteOnly));QCOMPARE(gate.write("release\n"),8);gate.close();
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,4000);QCOMPARE(errors.count(),0);QCOMPARE(warm.count(),1);QCOMPARE(backend.count(),1);QCOMPARE(ready[0][0].toULongLong(),generation);
        const auto cues=qvariant_cast<QVector<SubtitleCue>>(ready[0][1]);QCOMPARE(cues[0].startMs,2000);QVERIFY(cues[0].text.contains("request 1"));
        QFile log(starts);QVERIFY(log.open(QIODevice::ReadOnly));QCOMPARE(log.readAll().count('\n'),1);job.shutdown();
    }
    void shutdownInvalidatesDeferredWarmupFallback(){
        QTemporaryDir dir;for(const auto &folder:{"ai","models"})QVERIFY(QDir(dir.path()).mkpath(folder));
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        QVERIFY(QFile::copy(QCoreApplication::applicationDirPath()+"/subtitle_worker_fixture"+suffix,dir.filePath("ai/luma-whisper-worker"+suffix)));
        for(const auto &name:{"ggml-base.bin","ggml-base-encoder-openvino.xml","ggml-base-encoder-openvino.bin"}){QFile file(dir.filePath("models/"+QString(name)));QVERIFY(file.open(QIODevice::WriteOnly));file.write("fixture");}
        const auto starts=dir.filePath("starts");qputenv("LUMA_FIXTURE_ASR_STARTS",QFile::encodeName(starts));qputenv("LUMA_FIXTURE_WARMUP_FAIL","1");
        const auto cleanup=qScopeGuard([]{qunsetenv("LUMA_FIXTURE_ASR_STARTS");qunsetenv("LUMA_FIXTURE_WARMUP_FAIL");});
        SubtitleJob job(dir.path());bool stopped=false;connect(&job,&SubtitleJob::progress,this,[&](const QString &message){if(message.contains("모델 준비 실패")){job.shutdown();stopped=true;}});
        job.warmUp();QTRY_VERIFY_WITH_TIMEOUT(stopped,3000);QTest::qWait(250);QFile log(starts);QVERIFY(log.open(QIODevice::ReadOnly));QCOMPARE(log.readAll().count('\n'),1);
    }
};
QTEST_GUILESS_MAIN(SubtitleJobTests)
#include "subtitle_job_tests.moc"
