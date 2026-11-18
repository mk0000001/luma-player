#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include "subtitles/preparation_media_probe.h"
using namespace luma;
namespace {
QString metadata(const QString &duration="00:00:01.25"){
    return "Input #0, matroska,webm, from 'owned.mkv':\n  Duration: "+duration+", start: 0.000000, bitrate: 128 kb/s\n"
        "  Stream #0:0: Video: h264, yuv420p, 320x180\n"
        "  Stream #0:1(jpn): Audio: aac (LC), 48000 Hz, stereo (default)\n"
        "  Stream #0:4[0x1200](eng): Audio: ac3, 48000 Hz, 5.1\n"
        "At least one output file must be specified\n";
}
bool prepareRuntime(const QTemporaryDir &directory){
    if(!QDir(directory.path()).mkpath("tools"))return false;
#ifdef Q_OS_WIN
    const auto destination=directory.filePath("tools/ffmpeg.exe");
#else
    const auto destination=directory.filePath("tools/ffmpeg");
#endif
    return QFile::copy(QCoreApplication::applicationFilePath(),destination);
}
QString input(const QTemporaryDir &directory,const QString &name,const QJsonObject &settings){
    const auto path=directory.filePath(name);QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(settings).toJson(QJsonDocument::Compact))<0)return {};
    return path;
}
QJsonArray readArguments(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(file.readAll()).array();}
int fixture(const QStringList &arguments){
    const auto index=arguments.indexOf("-i");if(index<0 || index+1>=arguments.size())return 7;
    QFile file(arguments[index+1]);if(!file.open(QIODevice::ReadOnly))return 8;
    const auto settings=QJsonDocument::fromJson(file.readAll()).object();
    if(const auto record=settings.value("argumentsFile").toString();!record.isEmpty()){
        QFile output(record);if(!output.open(QIODevice::WriteOnly))return 9;
        output.write(QJsonDocument(QJsonArray::fromStringList(arguments)).toJson(QJsonDocument::Compact));
    }
    if(const auto bytes=settings.value("stdoutBytes").toInt();bytes>0){const QByteArray extra(bytes,'x');std::fwrite(extra.constData(),1,extra.size(),stdout);std::fflush(stdout);}
    const auto output=settings.value("output").toString().toUtf8();std::fwrite(output.constData(),1,output.size(),stderr);std::fflush(stderr);
    if(const auto delay=settings.value("delayMs").toInt();delay>0)QThread::msleep(delay);
    return settings.value("exitCode").toInt(1);
}
}
class PreparationMediaProbeTests:public QObject {
    Q_OBJECT
private slots:
    void init(){QTest::failOnWarning(QRegularExpression("QProcess: Destroyed while process.*"));}
    void durationFractionsAndAudioOrdinals_data(){
        QTest::addColumn<QString>("duration");QTest::addColumn<qint64>("milliseconds");
        QTest::newRow("whole-second")<<QString("01:02:03")<<qint64(3723000);
        QTest::newRow("tenths")<<QString("00:00:01.2")<<qint64(1200);
        QTest::newRow("hundredths")<<QString("00:00:01.25")<<qint64(1250);
        QTest::newRow("sub-millisecond-truncated")<<QString("00:00:01.234567")<<qint64(1234);
        QTest::newRow("maximum-one-day")<<QString("24:00:00.000")<<qint64(86400000);
    }
    void durationFractionsAndAudioOrdinals(){
        QFETCH(QString,duration);QFETCH(qint64,milliseconds);QTemporaryDir directory;QVERIFY(prepareRuntime(directory));
        const auto path=input(directory,"owned.mkv",{{"output",metadata(duration)}});QVERIFY(!path.isEmpty());
        PreparationMediaProbe probe(directory.path());QSignalSpy ready(&probe,&PreparationMediaProbe::ready),failed(&probe,&PreparationMediaProbe::failed);
        const auto generation=probe.inspect(path);QVERIFY(probe.busy());QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,2000);QCOMPARE(failed.size(),0);
        QCOMPARE(ready[0][0].toULongLong(),generation);const auto info=qvariant_cast<PreparationMediaInfo>(ready[0][1]);QCOMPARE(info.durationMs,milliseconds);
        QCOMPARE(info.audioTracks.size(),2);QVERIFY(info.audioTracks[0].contains("jpn"));QVERIFY(info.audioTracks[0].contains("aac"));QVERIFY(info.audioTracks[1].contains("eng"));QVERIFY(info.audioTracks[1].contains("ac3"));QVERIFY(!probe.busy());
    }
    void invalidOrFailedMetadata_data(){
        QTest::addColumn<QString>("output");QTest::addColumn<int>("exitCode");
        QTest::newRow("no-duration")<<QString("Stream #0:0: Audio: pcm_s16le, 16000 Hz\nAt least one output file must be specified\n")<<1;
        QTest::newRow("no-audio")<<QString("Duration: 00:01:00.00, start: 0.0, bitrate: 10 kb/s\nStream #0:0: Video: h264\nAt least one output file must be specified\n")<<1;
        QTest::newRow("unknown-duration")<<metadata("N/A")<<1;
        QTest::newRow("zero-duration")<<metadata("00:00:00.000")<<1;
        QTest::newRow("bad-minute")<<metadata("00:60:01.000")<<1;
        QTest::newRow("too-long")<<metadata("24:00:00.001")<<1;
        QTest::newRow("fatal-exit")<<metadata()<<2;
        QTest::newRow("unexpected-success-exit")<<metadata()<<0;
        QTest::newRow("failed-input-exit-one")<<QString("Duration: 00:00:01.25, start: 0.0\nStream #0:1(jpn): Audio: aac\nInvalid data found when processing input\n")<<1;
    }
    void invalidOrFailedMetadata(){
        QFETCH(QString,output);QFETCH(int,exitCode);QTemporaryDir directory;QVERIFY(prepareRuntime(directory));
        const auto path=input(directory,"owned.mkv",{{"output",output},{"exitCode",exitCode}});QVERIFY(!path.isEmpty());
        PreparationMediaProbe probe(directory.path());QSignalSpy ready(&probe,&PreparationMediaProbe::ready),failed(&probe,&PreparationMediaProbe::failed);
        const auto generation=probe.inspect(path);QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,2000);QCOMPARE(ready.size(),0);QVERIFY(!probe.busy());
        QCOMPARE(failed[0][0].toULongLong(),generation);QVERIFY(!failed[0][1].toString().isEmpty());QVERIFY(failed[0][1].toString().size()<=600);
    }
    void literalUnicodePathHasOnlyHeaderInspectionArguments(){
        QTemporaryDir directory;QVERIFY(prepareRuntime(directory));const auto record=directory.filePath("arguments.json");
        const auto path=input(directory,QString::fromUtf8("소유 日本語 & literal $(probe) %.mkv"),{{"output",metadata()},{"argumentsFile",record}});QVERIFY(!path.isEmpty());
        PreparationMediaProbe probe(directory.path());QSignalSpy ready(&probe,&PreparationMediaProbe::ready);probe.inspect(path);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,2000);
        const auto args=readArguments(record);QCOMPARE(args.size(),6);
        QCOMPARE(args[0].toString(),QString("-hide_banner"));QCOMPARE(args[1].toString(),QString("-nostdin"));QCOMPARE(args[2].toString(),QString("-protocol_whitelist"));QCOMPARE(args[3].toString(),QString("file"));QCOMPARE(args[4].toString(),QString("-i"));QCOMPARE(args[5].toString(),QFileInfo(path).canonicalFilePath());
    }
    void rejectsMissingFilesDirectoriesAndUrls(){
        QTemporaryDir directory;QVERIFY(prepareRuntime(directory));PreparationMediaProbe probe(directory.path());
        QSignalSpy ready(&probe,&PreparationMediaProbe::ready),failed(&probe,&PreparationMediaProbe::failed);
        const QStringList paths{directory.filePath("missing.mkv"),directory.path(),"https://example.invalid/movie.mkv","file:///movie.mkv"};
        for(const auto &path:paths){const auto generation=probe.inspect(path);const auto previous=failed.size();QTRY_COMPARE_WITH_TIMEOUT(failed.size(),previous+1,1000);QCOMPARE(failed.last()[0].toULongLong(),generation);QVERIFY(!probe.busy());}QCOMPARE(ready.size(),0);
    }
    void absentBundledToolFails(){
        QTemporaryDir directory;const auto path=input(directory,"owned.mkv",{{"output",metadata()}});PreparationMediaProbe probe(directory.path());
        QSignalSpy ready(&probe,&PreparationMediaProbe::ready),failed(&probe,&PreparationMediaProbe::failed);probe.inspect(path);QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,2000);QCOMPARE(ready.size(),0);QVERIFY(!probe.busy());
    }
    void cancelAndReplacementSuppressLateCallbacks(){
        QTemporaryDir directory;QVERIFY(prepareRuntime(directory));const auto started=directory.filePath("started.json");
        const auto old=input(directory,"old.mkv",{{"output",metadata("00:00:08.00")},{"argumentsFile",started},{"delayMs",3000}});
        const auto current=input(directory,"current.mkv",{{"output",metadata("00:00:02.50")}});
        PreparationMediaProbe probe(directory.path());QSignalSpy ready(&probe,&PreparationMediaProbe::ready),failed(&probe,&PreparationMediaProbe::failed);
        const auto oldGeneration=probe.inspect(old);QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(started),2000);probe.cancel();QVERIFY(!probe.busy());QVERIFY(probe.generation()>oldGeneration);
        QTest::qWait(100);QCOMPARE(ready.size(),0);QCOMPARE(failed.size(),0);
        QVERIFY(QFile::remove(started));const auto replaced=probe.inspect(old);QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(started),2000);
        const auto currentGeneration=probe.inspect(current);QVERIFY(currentGeneration>replaced);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,2000);QCOMPARE(ready[0][0].toULongLong(),currentGeneration);QCOMPARE(qvariant_cast<PreparationMediaInfo>(ready[0][1]).durationMs,qint64(2500));QTest::qWait(200);QCOMPARE(ready.size(),1);QCOMPARE(failed.size(),0);
    }
    void queuedValidationFailureCannotEscapeIntoReplacement(){
        QTemporaryDir directory;QVERIFY(prepareRuntime(directory));const auto current=input(directory,"current.mkv",{{"output",metadata()}});
        PreparationMediaProbe probe(directory.path());QSignalSpy ready(&probe,&PreparationMediaProbe::ready),failed(&probe,&PreparationMediaProbe::failed);
        probe.inspect(directory.filePath("missing.mkv"));const auto generation=probe.inspect(current);QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,2000);QCOMPARE(ready[0][0].toULongLong(),generation);QCOMPARE(failed.size(),0);
    }
    void excessiveOutputStopsPromptlyWithBoundedDiagnostic(){
        QTemporaryDir directory;QVERIFY(prepareRuntime(directory));const auto path=input(directory,"flood.mkv",{{"output",metadata()},{"stdoutBytes",600000},{"delayMs",60000}});
        PreparationMediaProbe probe(directory.path());QSignalSpy ready(&probe,&PreparationMediaProbe::ready),failed(&probe,&PreparationMediaProbe::failed);
        probe.inspect(path);QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,2000);QCOMPARE(ready.size(),0);QVERIFY(!probe.busy());QVERIFY(failed[0][1].toString().size()<=600);
    }
    void cancelledProbeDestructionStopsTheProcess(){
        QTemporaryDir directory;QVERIFY(prepareRuntime(directory));const auto started=directory.filePath("started.json");
        const auto path=input(directory,"slow.mkv",{{"output",metadata()},{"argumentsFile",started},{"delayMs",60000}});
        QElapsedTimer teardown;
        {
            PreparationMediaProbe probe(directory.path());probe.inspect(path);QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(started),2000);
            probe.cancel();teardown.start();
        }
        QVERIFY(teardown.elapsed()<1500);
    }
    void unresponsiveProbeTimesOutWithoutAResult(){
        QTemporaryDir directory;QVERIFY(prepareRuntime(directory));const auto path=input(directory,"slow.mkv",{{"output",metadata()},{"delayMs",60000}});
        PreparationMediaProbe probe(directory.path());QSignalSpy ready(&probe,&PreparationMediaProbe::ready),failed(&probe,&PreparationMediaProbe::failed);QElapsedTimer elapsed;elapsed.start();
        probe.inspect(path);QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,35000);QVERIFY2(elapsed.elapsed()>=29000,"The real process deadline must govern timeout");QCOMPARE(ready.size(),0);QVERIFY(!probe.busy());QVERIFY(failed[0][1].toString().size()<=600);
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(QFileInfo(QCoreApplication::applicationFilePath()).completeBaseName().compare("ffmpeg",Qt::CaseInsensitive)==0)return fixture(app.arguments().mid(1));
    PreparationMediaProbeTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "preparation_media_probe_tests.moc"
