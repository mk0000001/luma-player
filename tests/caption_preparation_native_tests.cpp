#include <QtTest>
#include <QFileInfo>
#include "subtitles/caption_preparation.h"
#include "subtitles/preparation_media_probe.h"
#include "subtitles/subtitle_file.h"
#include "core/disc.h"
using namespace luma;
class CaptionPreparationNativeTests:public QObject {
    Q_OBJECT
private slots:
    void connectedBluRayPreparesWholeShortPlaylistWithoutPlayback(){
        const auto runtime=qEnvironmentVariable("LUMA_PREPARATION_RUNTIME"),discRoot=qEnvironmentVariable("LUMA_PREPARATION_DISC"),output=qEnvironmentVariable("LUMA_PREPARATION_DISC_OUTPUT");
        if(runtime.isEmpty() || discRoot.isEmpty() || output.isEmpty())QSKIP("Set the preparation runtime, physical disc and fresh output directory for native Blu-ray preparation.");
        const auto disc=inspectDisc(discRoot);QVERIFY2(disc.valid,qPrintable(disc.error));std::optional<DiscTitle> selected;
        for(const auto &title:disc.titles)if(title.durationMs>=1000 && title.durationMs<=120000 && (!selected || title.durationMs<selected->durationMs))selected=title;
        if(!selected)QSKIP("This disc has no short complete playlist suitable for bounded physical verification.");
        CaptionPreparationRequest request;request.source.source=disc.root;request.source.disc=true;request.source.playlist=selected->playlist;request.source.language="ja";request.durationMs=selected->durationMs;request.outputDirectory=output;
        request.model=qEnvironmentVariable("LUMA_PREPARATION_ASR_MODEL");request.translationModel=qEnvironmentVariable("LUMA_PREPARATION_TRANSLATION_MODEL");request.recognitionPreferences={"gpu","CUDA0"};request.translationPreferences={"gpu","CUDA0"};request.translate=true;
        CaptionPreparationService service(runtime);QSignalSpy ready(&service,&CaptionPreparationService::ready),errors(&service,&CaptionPreparationService::failed),progress(&service,&CaptionPreparationService::progress);
        const auto generation=service.start(request);QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !errors.isEmpty(),240000);QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.first()[1].toString()));QCOMPARE(ready.size(),1);QCOMPARE(ready.first()[0].toULongLong(),generation);
        const auto result=qvariant_cast<CaptionPreparationResult>(ready.first()[1]);QCOMPARE(result.request.durationMs,selected->durationMs);QVERIFY(!service.busy());QString error;QVERIFY2(CaptionPreparationService::sourceUnchanged(result,&error),qPrintable(error));QVERIFY2(CaptionPreparationService::loadProject(result.projectFile,&error).has_value(),qPrintable(error));
        bool covered=false;for(const auto &signal:progress)if(qvariant_cast<CaptionPreparationProgress>(signal[1]).recognizedMs==selected->durationMs)covered=true;QVERIFY(covered);
        qInfo().noquote()<<"physical_disc_preparation playlist="<<selected->playlist<<"duration_ms="<<selected->durationMs<<"original_cues="<<result.originals.size()<<"korean_pages="<<result.translations.size()<<"skipped="<<result.skippedTranslations;
    }
    void wholeOwnedFilePreparesWithoutPlayback(){
        const auto runtime=qEnvironmentVariable("LUMA_PREPARATION_RUNTIME"),source=qEnvironmentVariable("LUMA_PREPARATION_MEDIA"),output=qEnvironmentVariable("LUMA_PREPARATION_OUTPUT");
        if(runtime.isEmpty() || source.isEmpty() || output.isEmpty())QSKIP("Set the preparation runtime, owned media and fresh output directory for actual offline preparation.");
        PreparationMediaProbe probe(runtime);QSignalSpy info(&probe,&PreparationMediaProbe::ready),probeErrors(&probe,&PreparationMediaProbe::failed);probe.inspect(source);
        QTRY_VERIFY_WITH_TIMEOUT(!info.isEmpty() || !probeErrors.isEmpty(),35000);QVERIFY2(probeErrors.isEmpty(),probeErrors.isEmpty()?"":qPrintable(probeErrors.first()[1].toString()));
        const auto media=qvariant_cast<PreparationMediaInfo>(info.first()[1]);QVERIFY(media.durationMs>0);QVERIFY(!media.audioTracks.isEmpty());
        CaptionPreparationRequest request;request.source.source=source;request.source.language=qEnvironmentVariable("LUMA_PREPARATION_LANGUAGE","ja");request.durationMs=media.durationMs;
        request.model=qEnvironmentVariable("LUMA_PREPARATION_ASR_MODEL");request.translationModel=qEnvironmentVariable("LUMA_PREPARATION_TRANSLATION_MODEL");request.outputDirectory=output;
        request.recognitionPreferences={"gpu","CUDA0"};request.translationPreferences={"gpu","CUDA0"};request.translate=true;
        CaptionPreparationService service(runtime);QSignalSpy ready(&service,&CaptionPreparationService::ready),errors(&service,&CaptionPreparationService::failed),progress(&service,&CaptionPreparationService::progress);
        const auto generation=service.start(request);QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !errors.isEmpty(),240000);
        QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.first()[1].toString()));QCOMPARE(ready.size(),1);QCOMPARE(ready.first()[0].toULongLong(),generation);
        const auto result=qvariant_cast<CaptionPreparationResult>(ready.first()[1]);QVERIFY(!result.originals.isEmpty());QVERIFY(!result.translations.isEmpty());QVERIFY(!service.busy());QVERIFY(QFileInfo::exists(result.projectFile));
        QString error;QVERIFY2(CaptionPreparationService::sourceUnchanged(result,&error),qPrintable(error));QVERIFY2(CaptionPreparationService::loadProject(result.projectFile,&error).has_value(),qPrintable(error));
        QVERIFY(readSrtFile(result.originalSrt).valid());QVERIFY(readSrtFile(result.koreanSrt).valid());bool translationAfterWholeOriginal=false,gpu=false;
        for(const auto &signal:progress){const auto p=qvariant_cast<CaptionPreparationProgress>(signal[1]);if(p.phase=="translation"){QCOMPARE(p.recognizedMs,media.durationMs);translationAfterWholeOriginal=true;}if(p.recognitionBackend=="GPU")gpu=true;}
        QVERIFY(translationAfterWholeOriginal);QVERIFY(gpu);
        qInfo().noquote()<<"offline_preparation project="<<result.projectFile<<"duration_ms="<<media.durationMs<<"original_cues="<<result.originals.size()<<"korean_pages="<<result.translations.size()<<"skipped="<<result.skippedTranslations;
    }
};
QTEST_GUILESS_MAIN(CaptionPreparationNativeTests)
#include "caption_preparation_native_tests.moc"
