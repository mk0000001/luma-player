#include <QtTest>
#include <QWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QSettings>
#include <QSet>
#include "media/playback.h"
#include "media/platform_features.h"
#include "core/disc.h"
#include "subtitles/subtitle_job.h"
using namespace luma;
namespace {
void report(const QString &suffix,const PlaybackState &s,const QJsonObject &context={}){
    const auto prefix=qEnvironmentVariable("LUMA_NATIVE_REPORT");if(prefix.isEmpty())return;
    QJsonObject data{{"durationMs",s.durationMs},{"positionMs",s.positionMs},{"title",s.currentTitle},{"chapter",s.currentChapter},{"chapters",s.chapters.size()},{"decodedVideo",s.decodedVideo},{"displayedPictures",s.displayedPictures},{"decodedAudio",s.decodedAudio},{"playedAudioBuffers",s.playedAudioBuffers},{"videoCodec",s.videoCodec},{"audioCodec",s.audioCodec},{"hardwareDecoder",s.hardwareDecoder},{"sourcePrimaries",s.colorPrimaries},{"sourceTransfer",s.transferFunction},{"outputPrimaries",s.outputPrimaries},{"outputTransfer",s.outputTransferFunction},{"audioSampleRate",s.audioSampleRate},{"audioChannels",s.audioChannels},{"audioOutput",s.audioOutput},{"losslessAudio",s.losslessAudio},{"sourceFps",s.sourceFps},{"filteredFps",s.filteredFps},{"motionStatus",s.motionStatus},{"runtimeVersion",s.runtimeVersion}};
    for(auto it=context.begin();it!=context.end();++it)data.insert(it.key(),it.value());
    QFile out(prefix+"-"+suffix+".json");if(out.open(QIODevice::WriteOnly))out.write(QJsonDocument(data).toJson());
}
// 테스트가 끝나거나 assertion으로 반환해도 사용자의 저장 설정을 복구한다.
// Restore stored preferences even when a test assertion returns early.
class ScopedFilePreferences {
    struct Saved { QString key;QVariant value;bool existed; };
    QVector<Saved> saved_;
public:
    ScopedFilePreferences(){
        QSettings settings;
        for(const auto &key:{QString("video/adapter"),QString("motion/mode"),QString("motion/fps")})saved_.append({key,settings.value(key),settings.contains(key)});
        settings.remove("video/adapter");settings.setValue("motion/mode","off");settings.setValue("motion/fps",120);settings.sync();
    }
    ~ScopedFilePreferences(){
        QSettings settings;
        for(const auto &entry:saved_)if(entry.existed)settings.setValue(entry.key,entry.value);else settings.remove(entry.key);
        settings.sync();
    }
    void setAdapter(const QString &adapter){QSettings settings;settings.setValue("video/adapter",adapter);settings.sync();}
};
}
class NativePlaybackTests:public QObject {
    Q_OBJECT
private slots:
    void physicalDiscMenusAndTransport(){
        const auto disc=qEnvironmentVariable("LUMA_NATIVE_DISC");if(disc.isEmpty())QSKIP("Physical disc test requires LUMA_NATIVE_DISC.");
        QWidget video;video.setWindowTitle("LumaPlayer · physical disc verification");video.resize(960,540);video.show();
        PlaybackEngine engine(PlaybackEngine::Kind::Disc,qEnvironmentVariable("LUMA_TEST_RUNTIME"));QVERIFY2(engine.available(),qPrintable(engine.lastError()));QVERIFY(engine.open(disc,video.winId()));
        QSignalSpy messages(&engine,&PlaybackEngine::logMessage);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().titles.size()>=3,30000);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().currentTitle==0 && engine.state().playing && !engine.state().videoCodec.isEmpty(),45000);
        const auto overlayReady=[&messages]{for(const auto &message:messages)if(message[0].toString().contains("Initializing overlay"))return true;return false;};
        QTRY_VERIFY_WITH_TIMEOUT(overlayReady(),45000);QTest::qWait(300);report("disc-menu-ready",engine.state());
        engine.navigate(discActionCode(DiscAction::Activate));
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().durationMs>3600000,20000);report("disc-started",engine.state());
        const auto transitionVideo=engine.state().displayedPictures,transitionAudio=engine.state().playedAudioBuffers;auto minimumTime=engine.state().positionMs;
        const auto mainProgresses=[&]{minimumTime=std::min(minimumTime,engine.state().positionMs);return engine.state().displayedPictures>transitionVideo+30 && engine.state().playedAudioBuffers>transitionAudio+30 && engine.state().positionMs>minimumTime+1500 && !engine.state().audioTracks.isEmpty();};
        QTRY_VERIFY_WITH_TIMEOUT(mainProgresses(),25000);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().positionMs>=25000 && !engine.state().audioTracks.isEmpty(),45000);
        QVERIFY(engine.state().chapters.size()>1);report("disc-playing",engine.state());
        const auto expectedDecoder=qEnvironmentVariable("LUMA_NATIVE_EXPECT_GPU");
        if(!expectedDecoder.isEmpty())QCOMPARE(engine.state().hardwareDecoder,expectedDecoder);
        engine.pauseToggle();QTRY_VERIFY_WITH_TIMEOUT(!engine.state().playing,5000);const auto paused=engine.state().positionMs;QTest::qWait(700);QVERIFY(qAbs(engine.state().positionMs-paused)<300);
        engine.seek(60000);QTRY_VERIFY_WITH_TIMEOUT(qAbs(engine.state().positionMs-60000)<2500,10000);
        engine.pauseToggle();QTRY_VERIFY_WITH_TIMEOUT(engine.state().playing,5000);engine.setChapter(2);QTRY_COMPARE_WITH_TIMEOUT(engine.state().currentChapter,2,12000);report("disc-chapter",engine.state());
        for(const qint64 target:{120000,600000,610000,30000}){
            engine.seek(target);QTRY_VERIFY_WITH_TIMEOUT(qAbs(engine.state().positionMs-target)<2000,15000);
            const auto videoBefore=engine.state().displayedPictures,audioBefore=engine.state().playedAudioBuffers;const auto timeBefore=engine.state().positionMs;
            QTRY_VERIFY_WITH_TIMEOUT(engine.state().positionMs>timeBefore+1500 && engine.state().displayedPictures>videoBefore+20 && engine.state().playedAudioBuffers>audioBefore+20 && !engine.state().audioTracks.isEmpty(),20000);
        }
        const auto steadyStart=messages.size();
        const auto steadyFrame=engine.state().displayedPictures;
        const auto steadyAudio=engine.state().playedAudioBuffers;
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().displayedPictures>steadyFrame+45 && engine.state().playedAudioBuffers>steadyAudio+45,5000);
        for(const auto &entry:messages.mid(steadyStart))
            QVERIFY2(!entry[0].toString().contains("hardware acceleration picture allocation failed"),"GPU surface allocation failed during steady playback");
        report("disc-steady",engine.state());
        engine.navigate(discActionCode(DiscAction::Popup));QTest::qWait(700);QVERIFY(!engine.state().failed);
        engine.setTitle(0);QTRY_COMPARE_WITH_TIMEOUT(engine.state().currentTitle,0,12000);report("disc-menu",engine.state());
        const bool softwareDecoder=engine.state().hardwareDecoder.startsWith("CPU");
        QElapsedTimer stopTime;stopTime.start();engine.stop();QVERIFY2(stopTime.elapsed()<10000,"Embedded video shutdown blocked for more than ten seconds");
        QStringList decoderErrors;for(const auto &entry:messages)if(entry[0].toString().contains("hardware acceleration picture allocation failed"))decoderErrors.append(entry[0].toString());
        QStringList hardwareMessages;for(const auto &entry:messages)if(entry[0].toString().contains("Using D3D11VA") || entry[0].toString().contains("Using DXVA2"))hardwareMessages.append(entry[0].toString());
        const auto prefix=qEnvironmentVariable("LUMA_NATIVE_REPORT");if(!prefix.isEmpty()){QFile out(prefix+"-decoder-errors.json");if(out.open(QIODevice::WriteOnly))out.write(QJsonDocument(QJsonObject{{"allocationErrors",decoderErrors.size()},{"decoderOverride",qEnvironmentVariable("LUMA_DISC_DECODER")},{"hardwareMessages",QJsonArray::fromStringList(hardwareMessages)}}).toJson());}
        if(softwareDecoder)
            QVERIFY2(decoderErrors.isEmpty(),"Software disc playback attempted GPU surface allocation");
    }
    void av1FileAndTransport(){
        const auto file=qEnvironmentVariable("LUMA_NATIVE_AV1");if(file.isEmpty())QSKIP("Native AV1 test requires LUMA_NATIVE_AV1.");
        QWidget video;video.setWindowTitle("LumaPlayer · AV1 verification");video.resize(960,540);video.show();PlaybackEngine engine(PlaybackEngine::Kind::File,qEnvironmentVariable("LUMA_TEST_RUNTIME"));
        QVERIFY2(engine.available(),qPrintable(engine.lastError()));QVERIFY(engine.open(file,video.winId()));QTRY_VERIFY_WITH_TIMEOUT(engine.state().positionMs>600 && !engine.state().videoCodec.isEmpty(),15000);
        QVERIFY(engine.state().videoCodec.contains("av1",Qt::CaseInsensitive) || engine.state().videoCodec.contains("dav1d",Qt::CaseInsensitive));report("av1",engine.state());
        engine.pauseToggle();QTRY_VERIFY_WITH_TIMEOUT(!engine.state().playing,3000);engine.seek(3000);QTRY_VERIFY_WITH_TIMEOUT(qAbs(engine.state().positionMs-3000)<500,5000);engine.pauseToggle();QTRY_VERIFY_WITH_TIMEOUT(engine.state().playing,3000);engine.stop();
    }
    void hdr10MetadataGuardAndSdrOutput(){
        const auto fixtures=qEnvironmentVariable("LUMA_NATIVE_OWNED_FIXTURES");if(fixtures.isEmpty())QSKIP("Set LUMA_NATIVE_OWNED_FIXTURES for owned HDR/lossless/native-render tests.");
#ifndef Q_OS_WIN
        QSKIP("This native HDR-to-SDR check currently targets Windows D3D11 output.");
#else
        if(QGuiApplication::platformName()!=QStringLiteral("windows"))QSKIP("Run this native display check with QT_QPA_PLATFORM=windows, not offscreen.");
        const auto file=QDir(fixtures).filePath("hdr10-owned.mkv");QVERIFY2(QFileInfo(file).isFile(),qPrintable(file));
        ScopedFilePreferences preferences;
        QWidget video;video.setWindowTitle("LumaPlayer · owned HDR metadata / SDR output");video.resize(960,540);video.show();
        const auto display=displayCapabilities(video.winId());
        QVERIFY2(display.value("verified").toBool(),"DXGI could not inspect the real display used by this native test.");
        if(display.value("hdrActive").toBool())QSKIP("This slot verifies HDR source to SDR output; run on an SDR desktop.");
        PlaybackEngine engine(PlaybackEngine::Kind::File,qEnvironmentVariable("LUMA_TEST_RUNTIME"));QSignalSpy errors(&engine,&PlaybackEngine::errorOccurred);
        QVERIFY2(engine.available(),qPrintable(engine.lastError()));QVERIFY(engine.open(file,video.winId()));
        // PQ/BT.2020 must come from the decoded fixture, not its filename or bit depth.
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().transferFunction=="pq" && engine.state().colorPrimaries=="bt.2020",20000);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().playing && engine.state().positionMs>400 && !engine.state().outputTransferFunction.isEmpty(),15000);
        QVERIFY2(!engine.state().failed,qPrintable(engine.lastError()));QVERIFY(errors.isEmpty());
        const QSet<QString> sdrTransfers={"bt.1886","srgb","linear","gamma1.8","gamma2.0","gamma2.2","gamma2.4","gamma2.6","gamma2.8","prophoto","st428"};
        QVERIFY2(sdrTransfers.contains(engine.state().outputTransferFunction),qPrintable("Expected a reported SDR output transfer, got: "+engine.state().outputTransferFunction));
        engine.setFrameInterpolation("optical",360);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().motionStatus.contains("HDR"),5000);
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().sourceFps>0 && engine.state().filteredFps>0 && engine.state().filteredFps<engine.state().sourceFps*2,5000);
        const auto position=engine.state().positionMs;
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().positionMs>position+700,5000);
        QVERIFY2(!engine.state().failed,qPrintable(engine.lastError()));QVERIFY(errors.isEmpty());
        report("hdr-pq-guard-sdr",engine.state(),{{"display",display},{"requestedMotion","optical"},{"requestedFps",360},{"metadataAndSdrOnly",true}});
        engine.stop();
#endif
    }
    void losslessAudioMetadataAndClock_data(){
        QTest::addColumn<QString>("fixture");QTest::addColumn<QString>("codec");
        QTest::newRow("flac") << QString("lossless.flac") << QString("flac");
        QTest::newRow("alac") << QString("lossless.m4a") << QString("alac");
    }
    void losslessAudioMetadataAndClock(){
        const auto fixtures=qEnvironmentVariable("LUMA_NATIVE_OWNED_FIXTURES");if(fixtures.isEmpty())QSKIP("Set LUMA_NATIVE_OWNED_FIXTURES for generated FLAC/ALAC playback.");
        QFETCH(QString,fixture);QFETCH(QString,codec);
        const auto file=QDir(fixtures).filePath(fixture);QVERIFY2(QFileInfo(file).isFile(),qPrintable(file));
        ScopedFilePreferences preferences;
        QWidget video;video.setWindowTitle("LumaPlayer · owned lossless audio");video.resize(640,360);video.show();
        PlaybackEngine engine(PlaybackEngine::Kind::File,qEnvironmentVariable("LUMA_TEST_RUNTIME"));QSignalSpy errors(&engine,&PlaybackEngine::errorOccurred);
        QVERIFY2(engine.available(),qPrintable(engine.lastError()));QVERIFY(engine.open(file,video.winId()));
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().playing && engine.state().audioCodec==codec && engine.state().losslessAudio && engine.state().positionMs>300,15000);
        QCOMPARE(engine.state().audioSampleRate,96000);QCOMPARE(engine.state().audioChannels,1);
        QVERIFY(engine.state().durationMs>=4800 && engine.state().durationMs<=5200);
        QVERIFY2(!engine.state().audioOutput.isEmpty() && engine.state().audioOutput!="null","A real audio output must be active; clock-only/null output is insufficient.");
        const auto position=engine.state().positionMs;
        QTRY_VERIFY_WITH_TIMEOUT(engine.state().positionMs>position+700,5000);
        engine.setPaused(true);QTRY_VERIFY_WITH_TIMEOUT(!engine.state().playing,3000);
        const auto paused=engine.state().positionMs;QTest::qWait(400);QVERIFY(qAbs(engine.state().positionMs-paused)<200);
        engine.setPaused(false);QTRY_VERIFY_WITH_TIMEOUT(engine.state().playing && engine.state().positionMs>paused+400,5000);
        QVERIFY2(!engine.state().failed,qPrintable(engine.lastError()));QVERIFY(errors.isEmpty());
        report("lossless-"+codec,engine.state(),{{"fixture",fixture},{"decodedMetadataAndClockOnly",true},{"bitPerfectVerified",false}});
        engine.stop();
    }
    void gpuAdapterReopenRestoresSeek_data(){
        QTest::addColumn<QString>("fixture");QTest::addColumn<bool>("resumePlayback");
        QTest::newRow("av1-paused") << QString("av1-owned.mkv") << false;
        QTest::newRow("av1-playing") << QString("av1-owned.mkv") << true;
        QTest::newRow("two-audio-paused") << QString("two-audio-owned.mkv") << false;
        QTest::newRow("two-audio-playing") << QString("two-audio-owned.mkv") << true;
    }
    void gpuAdapterReopenRestoresSeek(){
        const auto fixtures=qEnvironmentVariable("LUMA_NATIVE_OWNED_FIXTURES"),adapter=qEnvironmentVariable("LUMA_NATIVE_GPU_ADAPTER");
        if(fixtures.isEmpty() || adapter.isEmpty())QSKIP("Set owned fixtures and an explicitly chosen LUMA_NATIVE_GPU_ADAPTER; enumeration alone does not prove support.");
#ifndef Q_OS_WIN
        QSKIP("DXGI adapter reopen is a Windows native-render check.");
#else
        if(QGuiApplication::platformName()!=QStringLiteral("windows"))QSKIP("Run GPU reopen with QT_QPA_PLATFORM=windows, not offscreen.");
        QFETCH(QString,fixture);QFETCH(bool,resumePlayback);
        const auto file=QDir(fixtures).filePath(fixture);QVERIFY2(QFileInfo(file).isFile(),qPrintable(file));
        ScopedFilePreferences preferences;
        QWidget video;video.setWindowTitle("LumaPlayer · owned adapter reopen / seek");video.resize(960,540);video.show();
        auto engine=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::File,qEnvironmentVariable("LUMA_TEST_RUNTIME"));
        QVERIFY2(engine->available(),qPrintable(engine->lastError()));QVERIFY(engine->open(file,video.winId()));
        QTRY_VERIFY_WITH_TIMEOUT(engine->state().playing && engine->state().positionMs>400 && !engine->state().videoCodec.isEmpty(),15000);
        engine->setPaused(true);QTRY_VERIFY_WITH_TIMEOUT(!engine->state().playing,3000);
        engine->seek(3000);QTRY_VERIFY_WITH_TIMEOUT(qAbs(engine->state().positionMs-3000)<400,7000);
        const auto restorePosition=engine->state().positionMs;
        engine->stop();engine.reset();
        preferences.setAdapter(adapter);
        engine=std::make_unique<PlaybackEngine>(PlaybackEngine::Kind::File,qEnvironmentVariable("LUMA_TEST_RUNTIME"));QSignalSpy errors(engine.get(),&PlaybackEngine::errorOccurred);
        QVERIFY2(engine->available(),qPrintable(engine->lastError()));QVERIFY(engine->open(file,video.winId()));
        // Issue restoration before FILE_LOADED, while holding pause. Otherwise a
        // dropped seek could falsely pass when normal playback later reaches 3s.
        engine->setPaused(true);engine->seek(restorePosition);
        QTRY_VERIFY_WITH_TIMEOUT(!engine->state().playing && !engine->state().videoCodec.isEmpty() && engine->state().durationMs>restorePosition && qAbs(engine->state().positionMs-restorePosition)<700,15000);
        if(resumePlayback){
            engine->setPaused(false);
            QTRY_VERIFY_WITH_TIMEOUT(engine->state().playing && engine->state().positionMs>restorePosition+700,5000);
        }else{
            QTRY_VERIFY_WITH_TIMEOUT(!engine->state().playing,3000);const auto paused=engine->state().positionMs;QTest::qWait(500);
            QVERIFY(qAbs(engine->state().positionMs-paused)<200);QVERIFY(!engine->state().playing);
            engine->setPaused(false);QTRY_VERIFY_WITH_TIMEOUT(engine->state().playing && engine->state().positionMs>paused+500,5000);
        }
        QVERIFY2(!engine->state().failed,qPrintable(engine->lastError()));QVERIFY(errors.isEmpty());
        report(QString("gpu-reopen-%1-%2").arg(QFileInfo(fixture).completeBaseName(),resumePlayback?"playing":"paused"),engine->state(),{{"fixture",fixture},{"requestedAdapter",adapter},{"restorePositionMs",restorePosition},{"resumeRequested",resumePlayback},{"adapterIdentityVerified",false}});
        engine->stop();
#endif
    }
    void realLocalRecognition(){
        const auto speech=qEnvironmentVariable("LUMA_NATIVE_SPEECH");if(speech.isEmpty())QSKIP("Real inference requires LUMA_NATIVE_SPEECH and a model.");
        SubtitleJob job(qEnvironmentVariable("LUMA_TEST_RUNTIME"));if(qEnvironmentVariableIsSet("LUMA_ASR_GPU"))job.setPreferences({"gpu",qEnvironmentVariable("LUMA_ASR_GPU")});QSignalSpy results(&job,&SubtitleJob::ready);QSignalSpy errors(&job,&SubtitleJob::failed);QElapsedTimer latency;latency.start();
        RecognitionRequest r;r.source=speech;r.language="en";r.durationMs=11000;job.start(r);job.cancel();const auto current=job.start(r);
        QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty() || !errors.isEmpty(),60000);QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors[0][0].toString()));QCOMPARE(results.size(),1);QCOMPARE(results[0][0].toULongLong(),current);
        const auto cues=qvariant_cast<QVector<SubtitleCue>>(results[0][1]);QVERIFY(!cues.isEmpty());QString words;for(const auto &cue:cues){words+=cue.text;QVERIFY(cue.startMs>=0);QVERIFY(cue.endMs<=11000);}QVERIFY(words.contains("country",Qt::CaseInsensitive));
        if(qEnvironmentVariableIsSet("LUMA_REQUIRE_NPU"))QCOMPARE(job.selectedBackend(),QString("NPU"));
        if(qEnvironmentVariableIsSet("LUMA_ASR_GPU"))QCOMPARE(job.selectedBackend(),QString("GPU"));
        const auto first=latency.elapsed();latency.restart();job.start(r);QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,30000);QVERIFY(errors.isEmpty());
        const auto prefix=qEnvironmentVariable("LUMA_NATIVE_REPORT");if(!prefix.isEmpty()){QFile out(prefix+"-speech-latency.json");if(out.open(QIODevice::WriteOnly))out.write(QJsonDocument(QJsonObject{{"firstMs",first},{"warmMs",latency.elapsed()},{"backend",job.selectedBackend()},{"selectedGpu",qEnvironmentVariable("LUMA_ASR_GPU")}}).toJson());}
    }
    void physicalDiscCaptionAcquisition(){
        const auto disc=qEnvironmentVariable("LUMA_NATIVE_DISC");if(disc.isEmpty())QSKIP("Physical caption test requires the TUKI disc.");
        RecognitionRequest r;r.source=disc;r.disc=true;r.playlist=matchDiscPlaylist(inspectDisc(disc),6294288);r.startMs=60000;r.durationMs=6000;r.language="ja";r.audioTrackId=4352;
        QVERIFY(!r.playlist.isEmpty());SubtitleJob job(qEnvironmentVariable("LUMA_TEST_RUNTIME"));QSignalSpy results(&job,&SubtitleJob::ready);QSignalSpy errors(&job,&SubtitleJob::failed);job.start(r);
        QElapsedTimer latency;latency.start();QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty() || !errors.isEmpty(),45000);QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors[0][0].toString()));QCOMPARE(results.size(),1);
        const auto firstMs=latency.elapsed();r.startMs=66000;latency.restart();job.start(r);QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,45000);QVERIFY(errors.isEmpty());
        const auto cues=qvariant_cast<QVector<SubtitleCue>>(results[0][1]);for(const auto &cue:cues){QVERIFY(cue.startMs>=60000);QVERIFY(cue.endMs<=66000);}
        const auto prefix=qEnvironmentVariable("LUMA_NATIVE_REPORT");if(!prefix.isEmpty()){QFile out(prefix+"-disc-ai.json");if(out.open(QIODevice::WriteOnly))out.write(QJsonDocument(QJsonObject{{"playlist",r.playlist},{"startMs",60000},{"durationMs",6000},{"cueCount",cues.size()},{"completed",true},{"firstMs",firstMs},{"warmMs",latency.elapsed()},{"backend",job.selectedBackend()}}).toJson());}
    }
};
QTEST_MAIN(NativePlaybackTests)
#include "native_playback_tests.moc"
