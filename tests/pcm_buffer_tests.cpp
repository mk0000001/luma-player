#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "subtitles/pcm_buffer.h"
#include "subtitles/subtitle_job.h"
using namespace luma;
class PcmBufferTests:public QObject{
    Q_OBJECT
    void tools(QTemporaryDir &root){
        QDir(root.path()).mkpath("tools");
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        QVERIFY(QFile::copy(QCoreApplication::applicationDirPath()+"/subtitle_worker_fixture"+suffix,root.filePath("tools/ffmpeg"+suffix)));
    }
    QByteArray read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
private slots:
    void successfulShortAndSilentEof_data(){QTest::addColumn<QString>("source");QTest::newRow("partial")<<QString("short-pcm.wav");QTest::newRow("empty")<<QString("empty-pcm.wav");}
    void successfulShortAndSilentEof(){
        QFETCH(QString,source);QTemporaryDir root;tools(root);PcmBuffer buffer(root.path());QSignalSpy ready(&buffer,&PcmBuffer::ready),errors(&buffer,&PcmBuffer::failed);
        RecognitionRequest request;request.source=source;request.durationMs=5000;const auto wav=root.filePath("out.wav");buffer.prepare(request,wav);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,3000);QCOMPARE(errors.count(),0);const auto bytes=read(wav);QCOMPARE(bytes.size(),44+5*32000);QCOMPARE(bytes.right(32000),QByteArray(32000,'\0'));
    }
    void errorsNeverBecomeSilence(){
        QTemporaryDir root;tools(root);PcmBuffer buffer(root.path());QSignalSpy ready(&buffer,&PcmBuffer::ready),errors(&buffer,&PcmBuffer::failed);
        RecognitionRequest request;request.source="failed-pcm.wav";request.durationMs=5000;buffer.prepare(request,root.filePath("out.wav"));
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(),1,3000);QCOMPARE(ready.count(),0);
    }
    void sequentialRequestsReuseOneReadAndCancelDropsLateResults(){
        QTemporaryDir root;tools(root);const auto log=root.filePath("starts");qputenv("LUMA_FIXTURE_PCM_STARTS",QFile::encodeName(log));
        PcmBuffer buffer(root.path());QSignalSpy ready(&buffer,&PcmBuffer::ready);RecognitionRequest request;request.source="buffered-pcm.wav";request.durationMs=5000;
        buffer.prepare(request,root.filePath("first.wav"));QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,3000);
        request.startMs=7000;buffer.prepare(request,root.filePath("second.wav"));QTRY_COMPARE_WITH_TIMEOUT(ready.count(),2,3000);QCOMPARE(read(log).count('\n'),1);
        QCOMPARE(read(root.filePath("second.wav")).mid(44),QByteArray(5*32000,'p'));
        request.source="stall-pcm.wav";buffer.prepare(request,root.filePath("cancel.wav"));buffer.cancel();QTest::qWait(150);QCOMPARE(ready.count(),2);qunsetenv("LUMA_FIXTURE_PCM_STARTS");
    }
};
QTEST_GUILESS_MAIN(PcmBufferTests)
#include "pcm_buffer_tests.moc"
