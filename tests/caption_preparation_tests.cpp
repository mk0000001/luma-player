#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QtEndian>
#ifdef LUMA_CAPTION_PREPARATION_FIXTURE
// CPU-only wire/file fixture. It never decodes media or loads model weights.
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    const auto option=[&](const QString &key,const QString &fallback=QString()){const auto i=args.indexOf(key);return i>=0&&i+1<args.size()?args[i+1]:fallback;};
    const auto log=[&](const QJsonObject &record){QFile f(qEnvironmentVariable("LUMA_PREPARATION_FIXTURE_LOG"));if(f.open(QIODevice::Append))f.write(QJsonDocument(record).toJson(QJsonDocument::Compact)+'\n');};
    if(args.contains("-i")){
        const auto source=option("-i");const auto start=qRound64(option("-ss","0").toDouble()*1000),duration=qRound64(option("-t","30").toDouble()*1000);
        log({{"event","extract"},{"start_ms",start},{"duration_ms",duration},{"throttled",args.contains("-readrate")}});
        if(start>=30000 && QFileInfo::exists(source+".fail-once")){QFile::remove(source+".fail-once");return 9;}
        QFile media(source);media.open(QIODevice::ReadOnly);const auto mode=QString::fromUtf8(media.read(128)).trimmed();QFile output(args.last());if(!output.open(QIODevice::WriteOnly))return 1;
        if(args.last().endsWith(".pcm")){
            if(source.contains("silent-disc")){output.write(QByteArray(90000*32,'\0'));return 0;}
            const auto metadata=QJsonDocument(QJsonObject{{"start",start},{"duration",duration},{"mode","speech"}}).toJson(QJsonDocument::Compact)+'\n';output.write(metadata);output.write(QByteArray(duration*32-metadata.size(),'p'));return 0;
        }
        if(mode=="silent"){
            QByteArray wave("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x80\x3e\0\0\0\x7d\0\0\x02\0\x10\0data\0\0\0\0",44);
            qToLittleEndian<quint32>(quint32(duration*32+36),wave.data()+4);qToLittleEndian<quint32>(quint32(duration*32),wave.data()+40);output.write(wave);output.write(QByteArray(duration*32,'\0'));
        }else{
            auto data=QJsonDocument(QJsonObject{{"start",start},{"duration",duration},{"mode",mode}}).toJson(QJsonDocument::Compact);data.append(QByteArray(128,' '));output.write(data);
        }
        return 0;
    }
    QFile audio(option("-f"));if(!audio.open(QIODevice::ReadOnly))return 2;auto audioBytes=audio.readAll();QJsonObject data;
    if(audioBytes.startsWith("RIFF")){data=QJsonDocument::fromJson(audioBytes.mid(44).split('\n').first()).object();data["duration"]=(audioBytes.size()-44)/32;}else data=QJsonDocument::fromJson(audioBytes).object();
    const auto start=data.value("start").toInteger(),duration=data.value("duration").toInteger();const auto mode=data.value("mode").toString();
    log({{"event","recognize"},{"start_ms",start}});QFile srt(option("-of")+".srt");if(!srt.open(QIODevice::WriteOnly))return 3;
    const auto stamp=[](qint64 ms){return QString("00:00:%1,%2").arg(ms/1000,2,10,QChar('0')).arg(ms%1000,3,10,QChar('0'));};
    if(mode=="dense"){
        for(int i=0;i<80;++i){const auto text=i==0?QString("bad"):QString::fromUtf8("原文%1").arg(i);srt.write((QString::number(i+1)+'\n'+stamp(i*100)+" --> "+stamp(i*100+80)+'\n'+text+"\n\n").toUtf8());}return 0;
    }
    const auto end=std::min<qint64>(1000,duration);const auto text=QString::fromUtf8("原文")+QString::number(start);
    srt.write(("1\n"+stamp(0)+" --> "+stamp(end)+'\n'+text+"\n\n").toUtf8());srt.close();
    const QJsonArray tokens{QJsonObject{{"byteStart",0},{"byteEnd",6},{"startMs",0},{"endMs",end/2}},QJsonObject{{"byteStart",6},{"byteEnd",text.toUtf8().size()},{"startMs",end/2},{"endMs",end}}};
    QFile timing(option("-of")+".timings.json");if(!timing.open(QIODevice::WriteOnly))return 4;
    timing.write(QJsonDocument(QJsonObject{{"schema",1},{"format","luma-asr-timing-v1"},{"alignment","token"},{"audioMs",duration},{"segments",QJsonArray{QJsonObject{{"startMs",0},{"endMs",end},{"text",text},{"tokens",tokens}}}}}).toJson(QJsonDocument::Compact));return 0;
}
#else
#include <QtTest>
#include <QTemporaryDir>
#include <QScopeGuard>
#include "subtitles/caption_preparation.h"
#include "subtitles/translation_service.h"
#include "subtitles/translation_support.h"
using namespace luma;
class CaptionPreparationTests:public QObject {
    Q_OBJECT
    static void put(const QString &path,const QByteArray &bytes){QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(bytes),bytes.size());}
    static QByteArray read(const QString &path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
    static void discFiles(const QString &root,qint64 duration){
        for(const auto &folder:{"BDMV/PLAYLIST","BDMV/STREAM","BDMV/CLIPINF"})QDir().mkpath(QDir(root).filePath(folder));
        QByteArray index(128,'\0');index.replace(0,8,"INDX0200");qToBigEndian<quint32>(64,reinterpret_cast<uchar*>(index.data()+8));qToBigEndian<quint32>(60,reinterpret_cast<uchar*>(index.data()+64));put(QDir(root).filePath("BDMV/index.bdmv"),index);
        QByteArray playlist(74,'\0');playlist.replace(0,8,"MPLS0200");qToBigEndian<quint32>(40,reinterpret_cast<uchar*>(playlist.data()+8));qToBigEndian<quint32>(30,reinterpret_cast<uchar*>(playlist.data()+40));qToBigEndian<quint16>(1,reinterpret_cast<uchar*>(playlist.data()+46));qToBigEndian<quint16>(20,reinterpret_cast<uchar*>(playlist.data()+50));playlist.replace(52,5,"00002");playlist.replace(57,4,"M2TS");qToBigEndian<quint32>(45000,reinterpret_cast<uchar*>(playlist.data()+64));qToBigEndian<quint32>(quint32(45000+duration*45),reinterpret_cast<uchar*>(playlist.data()+68));
        put(QDir(root).filePath("BDMV/PLAYLIST/00001.mpls"),playlist);put(QDir(root).filePath("BDMV/STREAM/00002.m2ts"),"synthetic clip metadata");put(QDir(root).filePath("BDMV/CLIPINF/00002.clpi"),"synthetic CLPI metadata");
    }
    CaptionPreparationRequest prepare(QTemporaryDir &dir,const QByteArray &mode="speech",bool translate=false){
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        for(const auto &folder:{"tools","whisper","translation/vulkan","translation/models"})QDir(dir.path()).mkpath(folder);
        const auto bin=QCoreApplication::applicationDirPath();
        for(const auto &tool:{"tools/ffmpeg","whisper/whisper-cli"})QFile::copy(bin+"/caption_preparation_fixture"+suffix,dir.filePath(QString(tool)+suffix));
        QFile::copy(bin+"/translation_worker_fixture"+suffix,dir.filePath("translation/vulkan/llama-server"+suffix));
        CaptionPreparationRequest request;request.source.source=dir.filePath("owned-media.mkv");put(request.source.source,mode);request.durationMs=65000;
        request.model=dir.filePath("model.bin");put(request.model,"synthetic model identity, no weights");
        put(dir.filePath("whisper/ggml.dll"),"synthetic DLL metadata, never loaded");
        request.translationModel=dir.filePath("translation/models/fixture.gguf");put(request.translationModel,"synthetic translation identity, no weights");
        request.outputDirectory=dir.filePath("captions");request.recognitionPreferences={"cpu",{}};request.translationPreferences={"cpu",{}};request.translate=translate;
        qputenv("LUMA_PREPARATION_FIXTURE_LOG",QFile::encodeName(dir.filePath("fixture-log.jsonl")));return request;
    }
    static QVector<QJsonObject> events(const QString &path){QVector<QJsonObject> result;for(const auto &line:read(path).split('\n'))if(!line.isEmpty())result.append(QJsonDocument::fromJson(line).object());return result;}
    static CaptionPreparationResult result(const QSignalSpy &spy){return qvariant_cast<CaptionPreparationResult>(spy.first()[1]);}
private slots:
    void initTestCase(){qRegisterMetaType<CaptionPreparationProgress>();qRegisterMetaType<CaptionPreparationResult>();}
    void init(){QTest::failOnWarning(QRegularExpression("QProcess: Destroyed while process.*|QTemporaryDir: Unable to remove.*"));}
    void cleanup(){qunsetenv("LUMA_PREPARATION_FIXTURE_LOG");}
    void recognizesWholeRangeWithoutPlaybackAndPreservesTokens(){
        QTemporaryDir dir;const auto request=prepare(dir);CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed),progress(&service,&CaptionPreparationService::progress);
        const auto generation=service.start(request);QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),10000);QCOMPARE(failed.count(),0);QCOMPARE(ready.count(),1);QCOMPARE(ready.first()[0].toULongLong(),generation);QVERIFY(!service.busy());
        const auto output=result(ready);QCOMPARE(output.originals.size(),3);QCOMPARE(output.originals[0].startMs,0);QCOMPARE(output.originals[1].startMs,30000);QCOMPARE(output.originals[2].startMs,60000);QCOMPARE(output.originals[2].endMs,61000);QCOMPARE(output.originals[1].tokens.size(),2);QCOMPARE(output.originals[1].tokens[1].startMs,30500);
        QVector<qint64> starts,durations;for(const auto &event:events(dir.filePath("fixture-log.jsonl")))if(event["event"]=="extract"){starts.append(event["start_ms"].toInteger());durations.append(event["duration_ms"].toInteger());}
        QCOMPARE(starts,QVector<qint64>({0,30000,60000}));QCOMPARE(durations,QVector<qint64>({30000,30000,5000}));QVERIFY(read(output.originalSrt).contains("00:01:00,000 --> 00:01:01,000"));QVERIFY(output.koreanSrt.isEmpty());
        QVERIFY(CaptionPreparationService::loadProject(output.projectFile));const auto final=qvariant_cast<CaptionPreparationProgress>(progress.last()[1]);QCOMPARE(final.phase,QString("complete"));QCOMPARE(final.recognizedMs,65000);QCOMPARE(final.recognitionBackend,QString("CPU"));
    }
    void silenceStillAdvancesEveryChunk(){
        QTemporaryDir dir;const auto request=prepare(dir,"silent");CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed);service.start(request);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),10000);QCOMPARE(failed.count(),0);QVERIFY(result(ready).originals.isEmpty());QCOMPARE(events(dir.filePath("fixture-log.jsonl")).size(),3);
    }
    void fullOriginalPhasePrecedesTranslationAndSuppliesFutureContext(){
        QTemporaryDir dir;const auto request=prepare(dir,"speech",true);CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed);
        int originalsAtTranslation=0;connect(&service,&CaptionPreparationService::progress,this,[&](quint64,const CaptionPreparationProgress &p){if(p.phase=="translation" && originalsAtTranslation==0){for(const auto &event:events(dir.filePath("fixture-log.jsonl")))if(event["event"]=="recognize")++originalsAtTranslation;}});
        service.start(request);QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),12000);QCOMPARE(failed.count(),0);QCOMPARE(originalsAtTranslation,3);const auto output=result(ready);QCOMPARE(output.translations.size(),3);QCOMPARE(output.skippedTranslations,0);QVERIFY(!read(output.koreanSrt).isEmpty());
        const auto posts=events(request.translationModel+".requests");QCOMPARE(posts.size(),3);const auto context=posts.first()["data"].toObject()["context"].toArray();QCOMPARE(context.size(),2);QCOMPARE(context.last().toObject()["text"].toString(),QString::fromUtf8("原文60000"));
    }
    void boundedBatchesDoNotDropCaptionsAndReportSkips(){
        QTemporaryDir dir;auto request=prepare(dir,"dense",true);request.durationMs=10000;CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed);service.start(request);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),20000);QCOMPARE(failed.count(),0);const auto output=result(ready);QCOMPARE(output.originals.size(),80);QCOMPARE(output.translations.size(),79);QCOMPARE(output.skippedTranslations,1);QCOMPARE(events(request.translationModel+".requests").size(),80);
        const auto loaded=CaptionPreparationService::loadProject(output.projectFile);QVERIFY(loaded);QCOMPARE(loaded->translations.size(),79);QCOMPARE(loaded->skippedTranslations,1);
    }
    void canceledCheckpointResumesOnlyTheRemainingRange(){
        QTemporaryDir dir;const auto request=prepare(dir);CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed),canceled(&service,&CaptionPreparationService::canceled);
        const auto connection=connect(&service,&CaptionPreparationService::progress,this,[&](quint64,const CaptionPreparationProgress &p){if(p.phase=="recognition"&&p.recognizedMs==30000)service.cancel();});
        const auto old=service.start(request);QTRY_COMPARE_WITH_TIMEOUT(canceled.count(),1,10000);QVERIFY(!service.busy());QCOMPARE(ready.count(),0);QCOMPARE(canceled.first()[0].toULongLong(),old);disconnect(connection);
        const auto checkpoint=QDir(request.outputDirectory).filePath("luma-caption-project.json");QVERIFY(QFileInfo::exists(checkpoint));QVERIFY(!CaptionPreparationService::loadProject(checkpoint));service.start(request);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),10000);QCOMPARE(failed.count(),0);QCOMPARE(result(ready).originals.size(),3);QVector<qint64> starts;for(const auto &event:events(dir.filePath("fixture-log.jsonl")))if(event["event"]=="extract")starts.append(event["start_ms"].toInteger());QCOMPARE(starts,QVector<qint64>({0,30000,60000}));
    }
    void recognitionFailureKeepsAResumableIncompleteCheckpoint(){
        QTemporaryDir dir;const auto request=prepare(dir);put(request.source.source+".fail-once","fail once");CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed);service.start(request);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,10000);QCOMPARE(ready.count(),0);QVERIFY(!service.busy());service.start(request);QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);QCOMPARE(result(ready).originals.size(),3);
        QVector<qint64> starts;for(const auto &event:events(dir.filePath("fixture-log.jsonl")))if(event["event"]=="extract")starts.append(event["start_ms"].toInteger());QCOMPARE(starts,QVector<qint64>({0,30000,30000,60000}));
    }
    void translationCancellationResumesWithoutRepeatingCompletedWork(){
        QTemporaryDir dir;const auto request=prepare(dir,"speech",true);CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed),canceled(&service,&CaptionPreparationService::canceled);
        const auto connection=connect(&service,&CaptionPreparationService::progress,this,[&](quint64,const CaptionPreparationProgress &p){if(p.phase=="translation"&&p.completedTranslations==1)service.cancel();});service.start(request);QTRY_COMPARE_WITH_TIMEOUT(canceled.count(),1,10000);disconnect(connection);service.start(request);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),12000);QCOMPARE(failed.count(),0);QCOMPARE(result(ready).translations.size(),3);QCOMPARE(events(dir.filePath("fixture-log.jsonl")).size(),6);QCOMPARE(events(request.translationModel+".requests").size(),3);
    }
    void realTranslationRuntimeFailureDoesNotBecomeReady(){
        QTemporaryDir dir;auto request=prepare(dir,"speech",true);request.durationMs=1000;
#ifdef Q_OS_WIN
        QFile::remove(dir.filePath("translation/vulkan/llama-server.exe"));
#else
        QFile::remove(dir.filePath("translation/vulkan/llama-server"));
#endif
        CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed);service.start(request);QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,10000);QCOMPARE(ready.count(),0);QVERIFY(!service.busy());
    }
    void changedJobCannotOverwriteExistingProject_data(){QTest::addColumn<QString>("change");for(const auto &change:{"source","model","language","output-owner","worker","library"})QTest::newRow(change)<<QString(change);}
    void changedJobCannotOverwriteExistingProject(){
        QFETCH(QString,change);QTemporaryDir dir;auto request=prepare(dir);request.durationMs=1000;CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed);service.start(request);QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);const auto project=result(ready).projectFile;const auto before=read(project);
        if(change=="source")put(request.source.source,"changed source bytes");else if(change=="model")put(request.model,"changed model identity");else if(change=="language")request.source.language="en";else if(change=="output-owner"){request.source.source=dir.filePath("different.mkv");put(request.source.source,"speech");}else if(change=="library")put(dir.filePath("whisper/ggml.dll"),"changed library metadata, same executable");else{
#ifdef Q_OS_WIN
            put(dir.filePath("whisper/whisper-cli.exe"),"changed worker metadata");
#else
            put(dir.filePath("whisper/whisper-cli"),"changed worker metadata");
#endif
        }
        service.start(request);QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,3000);QCOMPARE(ready.count(),1);QCOMPARE(read(project),before);
    }
    void completedLoadNeedsOnlyTheUnchangedSource(){
        QTemporaryDir dir;auto request=prepare(dir,"speech",true);request.durationMs=1000;CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready);service.start(request);QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);const auto output=result(ready);
        QFile::remove(request.model);QFile::remove(request.translationModel);
#ifdef Q_OS_WIN
        QFile::remove(dir.filePath("whisper/whisper-cli.exe"));
#else
        QFile::remove(dir.filePath("whisper/whisper-cli"));
#endif
        const auto loaded=CaptionPreparationService::loadProject(output.projectFile);QVERIFY(loaded);QCOMPARE(loaded->sourceIdentity.size(),32);QVERIFY(CaptionPreparationService::sourceUnchanged(*loaded));QCOMPARE(loaded->originals.first().tokens.size(),2);
        put(request.source.source,"source changed");QString error;QVERIFY(!CaptionPreparationService::sourceUnchanged(*loaded,&error));QVERIFY(!error.isEmpty());QVERIFY(!CaptionPreparationService::loadProject(output.projectFile));
    }
    void corruptOrOversizedProjectsAreRejected(){
        QTemporaryDir dir;auto request=prepare(dir);request.durationMs=1000;CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready);service.start(request);QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);const auto project=result(ready).projectFile;const auto pristine=read(project);
        auto unexpected=QJsonDocument::fromJson(pristine).object();unexpected["executable"]="untrusted-tool.exe";put(project,QJsonDocument(unexpected).toJson());QVERIFY(!CaptionPreparationService::loadProject(project));
        auto data=QJsonDocument::fromJson(pristine).object();auto cues=data["originals"].toArray();auto cue=cues[0].toObject();cue["end_ms"]=2000;cues[0]=cue;data["originals"]=cues;put(project,QJsonDocument(data).toJson());QVERIFY(!CaptionPreparationService::loadProject(project));
        data=QJsonDocument::fromJson(pristine).object();cues=data["originals"].toArray();cue=cues[0].toObject();auto tokens=cue["tokens"].toArray();auto token=tokens[0].toObject();token["text_start"]=1;tokens[0]=token;cue["tokens"]=tokens;cues[0]=cue;data["originals"]=cues;put(project,QJsonDocument(data).toJson());QVERIFY(!CaptionPreparationService::loadProject(project));
        put(project,"{broken");QVERIFY(!CaptionPreparationService::loadProject(project));put(project,QByteArray(64*1024*1024+1,' '));QVERIFY(!CaptionPreparationService::loadProject(project));
    }
    void queuedOldGenerationCannotPublishIntoReplacement(){
        QTemporaryDir dir;auto oldRequest=prepare(dir);auto fresh=oldRequest;fresh.outputDirectory=dir.filePath("new-captions");fresh.source.source=dir.filePath("other-owned.mkv");put(fresh.source.source,"speech");
        CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed),canceled(&service,&CaptionPreparationService::canceled);
        const auto old=service.start(oldRequest),current=service.start(fresh);QVERIFY(current>old);QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),10000);
        QCOMPARE(failed.count(),0);QCOMPARE(ready.count(),1);QCOMPARE(ready.first()[0].toULongLong(),current);QCOMPARE(canceled.count(),1);QCOMPARE(canceled.first()[0].toULongLong(),old);QVERIFY(!QFileInfo::exists(oldRequest.outputDirectory));QCOMPARE(result(ready).request.source.source,QFileInfo(fresh.source.source).canonicalFilePath());
    }
    void blurayProjectTracksSelectedPlaylistBytesAndClipMetadata(){
        QTemporaryDir dir;auto request=prepare(dir);request.durationMs=2000;request.source.disc=true;request.source.playlist="00001";request.source.source=dir.filePath("disc");
        for(const auto &folder:{"BDMV/PLAYLIST","BDMV/STREAM","BDMV/CLIPINF"})QDir().mkpath(QDir(request.source.source).filePath(folder));
        QByteArray index(128,'\0');index.replace(0,8,"INDX0200");qToBigEndian<quint32>(64,reinterpret_cast<uchar*>(index.data()+8));qToBigEndian<quint32>(60,reinterpret_cast<uchar*>(index.data()+64));put(QDir(request.source.source).filePath("BDMV/index.bdmv"),index);
        QByteArray playlist(74,'\0');playlist.replace(0,8,"MPLS0200");qToBigEndian<quint32>(40,reinterpret_cast<uchar*>(playlist.data()+8));qToBigEndian<quint32>(30,reinterpret_cast<uchar*>(playlist.data()+40));qToBigEndian<quint16>(1,reinterpret_cast<uchar*>(playlist.data()+46));qToBigEndian<quint16>(20,reinterpret_cast<uchar*>(playlist.data()+50));playlist.replace(52,5,"00002");playlist.replace(57,4,"M2TS");qToBigEndian<quint32>(45000,reinterpret_cast<uchar*>(playlist.data()+64));qToBigEndian<quint32>(135000,reinterpret_cast<uchar*>(playlist.data()+68));
        const auto playlistPath=QDir(request.source.source).filePath("BDMV/PLAYLIST/00001.mpls");put(playlistPath,playlist);put(QDir(request.source.source).filePath("BDMV/STREAM/00002.m2ts"),"synthetic clip metadata");put(QDir(request.source.source).filePath("BDMV/CLIPINF/00002.clpi"),"synthetic CLPI metadata");
        CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed);service.start(request);QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),10000);QCOMPARE(failed.count(),0);const auto output=result(ready);QCOMPARE(output.originals.size(),1);QCOMPARE(output.originals.first().tokens.size(),2);QVERIFY(CaptionPreparationService::sourceUnchanged(output));
        const auto mtime=QFileInfo(playlistPath).lastModified();playlist[4]='9';put(playlistPath,playlist);QFile restore(playlistPath);QVERIFY(restore.open(QIODevice::ReadWrite));QVERIFY(restore.setFileTime(mtime,QFileDevice::FileModificationTime));restore.close();QVERIFY(!CaptionPreparationService::sourceUnchanged(output));
    }
    void cachedSilentBlurayChunksFinishAcrossSynchronousChildReady(){
        QTemporaryDir dir;auto request=prepare(dir);request.source.disc=true;request.source.playlist="00001";request.source.source=dir.filePath("silent-disc");discFiles(request.source.source,65000);
        CaptionPreparationService service(dir.path());QSignalSpy ready(&service,&CaptionPreparationService::ready),failed(&service,&CaptionPreparationService::failed),progress(&service,&CaptionPreparationService::progress);service.start(request);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty()||!failed.isEmpty(),4000);QCOMPARE(failed.count(),0);QCOMPARE(ready.count(),1);QVERIFY(result(ready).originals.isEmpty());QCOMPARE(events(dir.filePath("fixture-log.jsonl")).size(),1);QVERIFY(!service.busy());QCOMPARE(qvariant_cast<CaptionPreparationProgress>(progress.last()[1]).recognizedMs,65000);
        QVERIFY(!events(dir.filePath("fixture-log.jsonl")).first().value("throttled").toBool());
    }
    void invalidRequestFailsWithoutStartingTools_data(){QTest::addColumn<int>("kind");for(int i=0;i<5;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;}
    void invalidRequestFailsWithoutStartingTools(){
        QFETCH(int,kind);QTemporaryDir dir;auto request=prepare(dir);if(kind==0)request.durationMs=0;if(kind==1)request.durationMs=86400001;if(kind==2)request.source.source="https://example.invalid/video";if(kind==3){request.source.disc=true;request.source.playlist="../01";}if(kind==4)request.source.startMs=1000;
        CaptionPreparationService service(dir.path());QSignalSpy failed(&service,&CaptionPreparationService::failed),ready(&service,&CaptionPreparationService::ready);service.start(request);QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,2000);QCOMPARE(ready.count(),0);QVERIFY(events(dir.filePath("fixture-log.jsonl")).isEmpty());
    }
};
QTEST_GUILESS_MAIN(CaptionPreparationTests)
#include "caption_preparation_tests.moc"
#endif
