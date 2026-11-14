#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>
#include "subtitles/translation_service.h"
#include "subtitles/translation_support.h"
using namespace luma;
class TranslationServiceTests:public QObject{
    Q_OBJECT
    QString prepare(QTemporaryDir &dir){
#ifdef Q_OS_WIN
        const QString suffix=".exe";
#else
        const QString suffix;
#endif
        for(const auto &folder:{"translation/vulkan","translation/openvino","translation/models"})QDir(dir.path()).mkpath(folder);
        const auto fixture=QCoreApplication::applicationDirPath()+"/translation_worker_fixture"+suffix;
        for(const auto &folder:{"vulkan","openvino"})QFile::copy(fixture,dir.filePath("translation/"+QString(folder)+"/llama-server"+suffix));
        const auto model=dir.filePath("translation/models/fixture.gguf");QFile file(model);if(file.open(QIODevice::WriteOnly))file.write("test fixture, not a model");return model;
    }
    QByteArray contents(const QString &name){QFile f(name);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
private slots:
    void liveWindowReplacesExpiredQueuedAndRunningCues(){
        QTemporaryDir dir;const auto model=prepare(dir);
        TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translatedCue),skips(&service,&TranslationService::skippedCue),errors(&service,&TranslationService::failed);
        const SubtitleCue running{0,200,"遅い"},old{200,400,"過ぎた"},near{800,1040,"今から"},later{1040,1250,"次は"};
        service.setContextCues({running,old,near,later});service.requestCues({running,old,near,later},0);
        QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),1,2000);
        service.requestCues({near,later},750);
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,2000);
        QCOMPARE(results[0][1].toString(),translationCueKey(near));
        QCOMPARE(results[1][1].toString(),translationCueKey(later));
        QTest::qWait(400);
        QCOMPARE(results.count(),2);QCOMPARE(skips.count(),0);QCOMPARE(errors.count(),0);
        QCOMPARE(contents(model+".requests").count('\n'),3);
    }
    void liveContextCanImproveFutureCuesWithoutRepeatingInFlightTranslation(){
        QTemporaryDir dir;const auto model=prepare(dir);
        TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translatedCue),errors(&service,&TranslationService::failed);
        const SubtitleCue running{400,1800,"遅い"},next{2100,3000,"次の字幕"},context{3400,3800,"新しい文脈"};
        service.setContextCues({running,next});service.requestCues({running,next},0);
        QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),1,2000);
        service.setContextCues({running,next,context});service.requestCues({running,next},500);
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,2000);
        QCOMPARE(errors.count(),0);QCOMPARE(contents(model+".requests").count('\n'),2);
        QCOMPARE(results[0][1].toString(),translationCueKey(running));
        QCOMPARE(results[1][1].toString(),translationCueKey(next));
        const auto request=QJsonDocument::fromJson(contents(model+".requests").trimmed().split('\n').last()).object();
        const auto neighbors=request.value("data").toObject().value("context").toArray();
        QVERIFY(std::any_of(neighbors.begin(),neighbors.end(),[&](const auto &cue){return cue.toObject().value("text").toString()==context.text;}));
        QVERIFY(service.needsTranslation(running));QVERIFY(!service.needsTranslation(next));
    }
    void contextCannotExpandAcceptedTargetLengthOrBlockFollowingCue(){
        QTemporaryDir dir;const auto model=prepare(dir);TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translatedCue),skips(&service,&TranslationService::skippedCue),errors(&service,&TranslationService::failed);
        const SubtitleCue rejected{1000,2000,"context-dump"},next{3000,4000,QString::fromUtf8("次の字幕")};
        service.setContextCues({rejected,next,{5000,6000,QString(400,QChar(0x65e5))}});service.requestCues({rejected,next});
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);QCOMPARE(errors.count(),0);QCOMPARE(skips.count(),1);QVERIFY(!service.busy());
        QCOMPARE(skips.first()[1].toString(),translationCueKey(rejected));QCOMPARE(skips.first()[2].toString(),rejected.text);
        QCOMPARE(results.first()[1].toString(),translationCueKey(next));QCOMPARE(contents(model+".requests").count('\n'),2);
    }
    void smallNpuProfileFallsBackBeforeAnyInferencePost(){
        QTemporaryDir dir;const auto fixture=prepare(dir);QVERIFY(QDir(dir.path()).mkpath("translation/models/fixture-npu-budget"));
        const auto model=dir.filePath("translation/models/fixture-npu-budget/Hy-MT2-7B-Q4_K_M.gguf");QVERIFY(QFile::copy(fixture,model));
        TranslationService service(dir.path());service.setModel(model);service.setPreferences({"npu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translatedCue),skips(&service,&TranslationService::skippedCue),errors(&service,&TranslationService::failed),progress(&service,&TranslationService::progress);
        const SubtitleCue target{1000,2000,QString::fromUtf8("短い字幕です。")},context{3000,4000,QString::fromUtf8("同じ動画の文脈です。")};service.setContextCues({target,context});service.requestCues({target});
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);QCOMPARE(skips.count(),0);QCOMPARE(errors.count(),0);QVERIFY(!service.busy());
        QCOMPARE(results.first()[1].toString(),translationCueKey(target));QCOMPARE(results.first()[2].toString(),target.text);QCOMPARE(service.selectedBackend(),QString("CPU / llama.cpp"));
        QCOMPARE(contents(model+".starts").count('\n'),2);QCOMPARE(contents(model+".requests").count('\n'),1);
        const auto request=QJsonDocument::fromJson(contents(model+".requests").trimmed()).object();QVERIFY(!request.value("npu").toBool());
        QCOMPARE(request.value("data").toObject().value("context").toArray().first().toObject().value("text").toString(),context.text);
        QVERIFY(std::any_of(progress.begin(),progress.end(),[](const QList<QVariant> &entry){return entry[0].toString().contains("NPU") && entry[0].toString().contains("입력");}));
    }
    void queuedCueUsesFutureContextThatArrivesAfterEnqueue(){
        for(const bool plain:{false,true}){
            QTemporaryDir dir;auto model=prepare(dir);if(plain){const auto quality=dir.filePath("translation/models/Hy-MT2-7B-Q4_K_M.gguf");QVERIFY(QFile::copy(model,quality));model=quality;}
            TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
            QSignalSpy results(&service,&TranslationService::translatedCue),errors(&service,&TranslationService::failed);
            const SubtitleCue first{0,1000,QString::fromUtf8("遅い")},past{2000,3000,QString::fromUtf8("箱の話です。")},target{5000,6000,QString::fromUtf8("それを渡して。")},future{7000,8000,QString::fromUtf8("赤い箱を使います。")};
            service.setContextCues({first,past,target});service.requestCues({first,target});
            QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),1,2000);
            service.setContextCues({first,past,target,future});
            // The active timed cue also gained context and must be retried first.
            QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),3,2000);
            const auto request=QJsonDocument::fromJson(contents(model+".requests").trimmed().split('\n').last()).object();
            const auto data=request.value("data").toObject();QCOMPARE(data.value("target").toObject().value("text").toString(),target.text);
            QStringList context;for(const auto &cue:data.value("context").toArray())context.append(cue.toObject().value("text").toString());
            QVERIFY(context.contains(past.text));QVERIFY(context.contains(future.text));QVERIFY(!context.contains(target.text));
            QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,2000);QCOMPARE(errors.count(),0);
            QCOMPARE(results.last()[1].toString(),translationCueKey(target));QCOMPARE(results.last()[2].toString(),target.text);
        }
    }
    void contextChangeInvalidatesTimedCacheWithoutConflatingRepeatedPhrases(){
        QTemporaryDir dir;const auto model=prepare(dir);TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translatedCue),errors(&service,&TranslationService::failed);
        const SubtitleCue first{5000,6000,QString::fromUtf8("そうです。")},repeated{15000,16000,first.text},future{7000,8000,QString::fromUtf8("新しい手掛かりです。")};
        service.setContextCues({first,repeated});service.requestCues({first});QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);
        service.requestCues({first});QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,1000);QCOMPARE(contents(model+".requests").count('\n'),1);
        service.setContextCues({first,repeated,future});service.requestCues({first});QTRY_COMPARE_WITH_TIMEOUT(results.count(),3,2000);QCOMPARE(contents(model+".requests").count('\n'),2);
        service.requestCues({repeated});QTRY_COMPARE_WITH_TIMEOUT(results.count(),4,2000);QCOMPARE(contents(model+".requests").count('\n'),3);
        QVERIFY(results[2][1].toString()!=results[3][1].toString());QCOMPARE(results[2][2].toString(),results[3][2].toString());QCOMPARE(errors.count(),0);
    }
    void completedTimedTranslationNeedsOnlyChangedBoundedContext(){
        for(const bool plain:{false,true}){
            QTemporaryDir dir;auto model=prepare(dir);if(plain){const auto quality=dir.filePath("translation/models/Hy-MT2-7B-Q4_K_M.gguf");QVERIFY(QFile::copy(model,quality));model=quality;}
            TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
            const auto &state=service;
            QSignalSpy results(&service,&TranslationService::translatedCue),errors(&service,&TranslationService::failed);
            const SubtitleCue target{5000,6000,QString::fromUtf8("それを渡して。")},past{2000,3000,QString::fromUtf8("箱の話です。")},future{7000,8000,QString::fromUtf8("赤い箱を使います。")},outside{67000,68000,QString::fromUtf8("範囲外の文脈です。")};
            service.setContextCues({past,target});QVERIFY(state.needsTranslation(target));service.requestCues({target});
            QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);QVERIFY(!state.needsTranslation(target));
            QVERIFY(state.needsTranslation({15000,16000,target.text}));
            service.setContextCues({outside,target,past,past});QVERIFY(!state.needsTranslation(target));
            service.requestCues({target});QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,1000);QCOMPARE(contents(model+".requests").count('\n'),1);
            service.setContextCues({future,past,target});QVERIFY(state.needsTranslation(target));service.requestCues({target});
            QTRY_COMPARE_WITH_TIMEOUT(results.count(),3,2000);QVERIFY(!state.needsTranslation(target));QCOMPARE(contents(model+".requests").count('\n'),2);QCOMPARE(errors.count(),0);
            service.cancel();QVERIFY(state.needsTranslation(target));
        }
    }
    void inFlightContextChangePublishesOnlyTheLatestTimedResult(){
        for(const bool plain:{false,true}){
            QTemporaryDir dir;auto model=prepare(dir);if(plain){const auto quality=dir.filePath("translation/models/Hy-MT2-7B-Q4_K_M.gguf");QVERIFY(QFile::copy(model,quality));model=quality;}
            TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
            QSignalSpy results(&service,&TranslationService::translatedCue),skips(&service,&TranslationService::skippedCue),errors(&service,&TranslationService::failed);
            const SubtitleCue target{5000,6000,QString::fromUtf8("遅い")},future{7000,8000,QString::fromUtf8("新しい手掛かりです。")};
            service.setContextCues({target});const auto generation=service.generation();service.requestCues({target});
            QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),1,2000);
            service.setContextCues({target,future});
            QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),2,2000);
            QCOMPARE(results.count(),0);
            const auto request=QJsonDocument::fromJson(contents(model+".requests").trimmed().split('\n').last()).object().value("data").toObject();
            QCOMPARE(request.value("target").toObject().value("text").toString(),target.text);
            QCOMPARE(request.value("context").toArray().size(),1);QCOMPARE(request.value("context").toArray().first().toObject().value("text").toString(),future.text);
            QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,2000);QVERIFY(!service.busy());QCOMPARE(skips.count(),0);QCOMPARE(errors.count(),0);
            QCOMPARE(results.first()[0].toULongLong(),generation);QCOMPARE(results.first()[1].toString(),translationCueKey(target));QCOMPARE(results.first()[2].toString(),target.text);
        }
    }
    void inFlightContextOutsideTheBoundedPayloadDoesNotRepeatInference(){
        QTemporaryDir dir;const auto model=prepare(dir);TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translatedCue),skips(&service,&TranslationService::skippedCue),errors(&service,&TranslationService::failed);
        const SubtitleCue target{5000,6000,QString::fromUtf8("遅い")},outside{67000,68000,QString::fromUtf8("範囲外の文脈です。")};
        service.setContextCues({target});service.requestCues({target});QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),1,2000);
        service.setContextCues({outside,target,target});QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,2000);
        QCOMPARE(contents(model+".requests").count('\n'),1);QVERIFY(!service.busy());QCOMPARE(skips.count(),0);QCOMPARE(errors.count(),0);
    }
    void cancellationAfterAnInFlightContextRefreshDropsTheRetriedResult(){
        QTemporaryDir dir;const auto model=prepare(dir);TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translatedCue),skips(&service,&TranslationService::skippedCue),errors(&service,&TranslationService::failed);
        const SubtitleCue old{5000,6000,QString::fromUtf8("遅い")},future{7000,8000,QString::fromUtf8("新しい手掛かりです。")},current{10000,11000,QString::fromUtf8("新しい字幕")};
        service.setContextCues({old});service.requestCues({old});QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),1,2000);
        service.setContextCues({old,future});QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),2,2000);QCOMPARE(results.count(),0);
        const auto generation=service.generation();service.cancel();service.setContextCues({current});service.requestCues({current});
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,2000);QTest::qWait(400);QCOMPARE(results.count(),1);QCOMPARE(skips.count(),0);QCOMPARE(errors.count(),0);QVERIFY(!service.busy());
        QVERIFY(service.generation()>generation);QCOMPARE(results.first()[0].toULongLong(),service.generation());QCOMPARE(results.first()[1].toString(),translationCueKey(current));
    }
    void generationAndModelChangesClearRawContext_data(){
        QTest::addColumn<QString>("change");QTest::newRow("cancel-seek-session")<<QString("cancel");QTest::newRow("model")<<QString("model");QTest::newRow("preferences")<<QString("preferences");
    }
    void generationAndModelChangesClearRawContext(){
        QFETCH(QString,change);QTemporaryDir dir;const auto model=prepare(dir);TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translatedCue),errors(&service,&TranslationService::failed);
        const SubtitleCue target{5000,6000,QString::fromUtf8("これは何ですか。")},context{7000,8000,QString::fromUtf8("前の動画の原文です。")};
        service.setContextCues({target,context});service.requestCues({target});QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);QVERIFY(!service.needsTranslation(target));const auto before=service.generation();
        if(change=="model")service.setModel(model);else if(change=="preferences")service.setPreferences({"gpu","Vulkan0"});else service.cancel();
        QVERIFY(service.generation()>before);QVERIFY(service.needsTranslation(target));service.requestCues({target});QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,3000);QVERIFY(!service.needsTranslation(target));
        QCOMPARE(contents(model+".requests").count('\n'),2);const auto request=QJsonDocument::fromJson(contents(model+".requests").trimmed().split('\n').last()).object();
        QCOMPARE(request.value("data").toObject().value("context").toArray().size(),0);QCOMPARE(errors.count(),0);
    }
    void cancellingContextRequestCannotPublishIntoNewSession(){
        QTemporaryDir dir;const auto model=prepare(dir);TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translatedCue),skips(&service,&TranslationService::skippedCue),errors(&service,&TranslationService::failed);
        const SubtitleCue old{0,1000,QString::fromUtf8("遅い")},current{0,1000,QString::fromUtf8("新しい字幕")};
        service.setContextCues({old,{2000,3000,"old neighbor"}});service.requestCues({old});QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),1,2000);
        const auto previous=service.generation();service.cancel();service.setContextCues({current});service.requestCues({current});
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,2000);QTest::qWait(400);QCOMPARE(results.count(),1);QCOMPARE(skips.count(),0);QCOMPARE(errors.count(),0);
        QVERIFY(results.first()[0].toULongLong()>previous);QCOMPARE(results.first()[1].toString(),translationCueKey(current));
    }
    void missingModelReportsFailureWithoutResults(){
        QTemporaryDir dir;TranslationService service(dir.path());QSignalSpy errors(&service,&TranslationService::failed),results(&service,&TranslationService::translated);
        service.request({"こんにちは"});QTRY_COMPARE_WITH_TIMEOUT(errors.count(),1,1000);QCOMPARE(results.count(),0);QVERIFY(!service.busy());
    }
    void selectedVulkanCanRecoverWithPartialOffload(){
        QTemporaryDir dir;
        const auto model=prepare(dir);
        TranslationService service(dir.path());
        service.setModel(model);
        service.setPreferences({"gpu","Vulkan8"});
        service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translated),errors(&service,&TranslationService::failed);

        service.request({"こんにちは"});
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,4000);
        QCOMPARE(errors.count(),0);
        QCOMPARE(service.selectedBackend(),QString("GPU / Vulkan8 / Fixture GPU"));
        QVERIFY(service.selectedVulkanCompatibility());
    }
    void missingRuntimeDoesNotReturnAPlaceholderTranslation(){
        QTemporaryDir dir;QFile model(dir.filePath("fixture.gguf"));QVERIFY(model.open(QIODevice::WriteOnly));model.write("test fixture");model.close();
        TranslationService service(dir.path());service.setModel(model.fileName());
        QSignalSpy errors(&service,&TranslationService::failed),results(&service,&TranslationService::translated);
        service.request({"こんにちは"});QTRY_COMPARE_WITH_TIMEOUT(errors.count(),1,1000);QCOMPARE(results.count(),0);QVERIFY(!service.busy());
    }
    void nativeWorkerReusesModelCachesDuplicatesAndFallsBackHonestly(){
        QTemporaryDir dir;const auto model=prepare(dir);TranslationService service(dir.path());service.setModel(model);service.setTimeouts(2000,1000);
        QSignalSpy errors(&service,&TranslationService::failed),results(&service,&TranslationService::translated),backend(&service,&TranslationService::backendSelected),progress(&service,&TranslationService::progress);
        service.request({"こんにちは","こんにちは"});QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,4000);QCOMPARE(errors.count(),0);QVERIFY(service.selectedBackend().contains("Vulkan0"));QVERIFY(backend.count()>0);
        service.request({"こんにちは"});QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,1000);QCOMPARE(contents(model+".requests").count('\n'),1);QCOMPARE(contents(model+".starts").count('\n'),1);
        const auto generation=service.generation();service.request({"遅い"});QTest::qWait(30);service.cancel();QVERIFY(service.generation()>generation);
        service.request({"新しい"});QTRY_COMPARE_WITH_TIMEOUT(results.count(),3,1500);QTest::qWait(400);QCOMPARE(results.count(),3);QCOMPARE(results.last()[0].toULongLong(),service.generation());QCOMPARE(results.last()[1].toString(),QString::fromUtf8("新しい"));
        QVERIFY(std::any_of(progress.begin(),progress.end(),[](const QList<QVariant> &entry){return entry[0].toString().contains("NPU");}));
    }
    void invalidTranslationNeverBecomesSuccessfulCaption(){
        QTemporaryDir dir;const auto model=prepare(dir);TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy errors(&service,&TranslationService::failed),results(&service,&TranslationService::translated),skipped(&service,&TranslationService::skipped);
        service.request({"bad"});QTRY_COMPARE_WITH_TIMEOUT(skipped.count(),1,3000);QCOMPARE(errors.count(),0);QCOMPARE(results.count(),0);QVERIFY(!service.busy());
    }
    void rejectedCueDoesNotStopFollowingCue_data(){
        QTest::addColumn<QString>("source");QTest::addColumn<int>("requestMs");
        QTest::newRow("non-Korean")<<QString("bad")<<1000;
        QTest::newRow("truncated-generation")<<QString("truncated")<<1000;
        QTest::newRow("malformed-response")<<QString("malformed")<<1000;
        QTest::newRow("cutoff-http-response")<<QString("cutoff-response")<<1000;
        QTest::newRow("oversized-http-request")<<QString("rejected-request")<<1000;
        QTest::newRow("oversized-response")<<QString("oversized-response")<<1000;
        QTest::newRow("request-timeout")<<QString::fromUtf8("遅い")<<80;
        QTest::newRow("oversized-source")<<QString(801,'x')<<1000;
    }
    void rejectedCueDoesNotStopFollowingCue(){
        QFETCH(QString,source);QFETCH(int,requestMs);QTemporaryDir dir;const auto model=prepare(dir);
        TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,requestMs);
        QSignalSpy errors(&service,&TranslationService::failed),results(&service,&TranslationService::translated),skipped(&service,&TranslationService::skipped);
        const auto generation=service.generation();service.request({source,"次の字幕"});
        QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty() || !errors.isEmpty(),4000);QCOMPARE(errors.count(),0);
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,4000);QCOMPARE(results.first()[1].toString(),QString::fromUtf8("次の字幕"));
        QCOMPARE(skipped.count(),1);QCOMPARE(skipped.first()[0].toULongLong(),generation);QCOMPARE(skipped.first()[1].toString(),source);QVERIFY(!skipped.first()[2].toString().isEmpty());
        QVERIFY(!service.busy());QCOMPARE(service.generation(),generation);QCOMPARE(contents(model+".starts").count('\n'),1);
    }
    void cancelledInvalidCueCannotSkipTheNextGeneration(){
        QTemporaryDir dir;const auto model=prepare(dir);TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy errors(&service,&TranslationService::failed),results(&service,&TranslationService::translated),skipped(&service,&TranslationService::skipped);
        service.request({"delayed-invalid"});QTRY_COMPARE_WITH_TIMEOUT(contents(model+".requests").count('\n'),1,2000);
        const auto previous=service.generation();service.cancel();service.request({"新しい字幕"});
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,2000);QTest::qWait(400);
        QCOMPARE(errors.count(),0);QCOMPARE(skipped.count(),0);QCOMPARE(results.count(),1);QCOMPARE(results.first()[1].toString(),QString::fromUtf8("新しい字幕"));QVERIFY(results.first()[0].toULongLong()>previous);
    }
    void deadlineAndPreferenceChangesInvalidateOutstandingWork(){
        QTemporaryDir dir;const auto model=prepare(dir);TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,80);
        QSignalSpy errors(&service,&TranslationService::failed),results(&service,&TranslationService::translated),skipped(&service,&TranslationService::skipped);
        service.request({"遅い"});QTRY_COMPARE_WITH_TIMEOUT(skipped.count(),1,3000);QCOMPARE(errors.count(),0);QCOMPARE(results.count(),0);
        service.setTimeouts(2000,1000);service.request({"遅い"});QTest::qWait(30);const auto before=service.generation();service.setPreferences({"gpu","Vulkan7"});QVERIFY(service.generation()>before);
        service.request({"こんにちは"});QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);QCOMPARE(service.selectedBackend(),QString("CPU / llama.cpp"));QTest::qWait(400);QCOMPARE(results.count(),1);
    }
    void seekDuringModelLoadingPreservesOneNativeModelLoad(){
        QTemporaryDir dir;const auto fixture=prepare(dir),model=fixture+"boot.gguf";QVERIFY(QFile::copy(fixture,model));
        TranslationService service(dir.path());service.setModel(model);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translated);service.request({"前の場面"});
        QTRY_COMPARE_WITH_TIMEOUT(contents(model+".starts").count('\n'),1,1000);
        service.cancel();service.request({"次の場面"});QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);
        QCOMPARE(contents(model+".starts").count('\n'),1);QCOMPARE(results.first()[1].toString(),QString::fromUtf8("次の場面"));
    }
    void dedicatedProfilePreservesSourceSkipsBadCueAndClearsCrossModelCache(){
        QTemporaryDir dir;const auto legacy=prepare(dir),quality=dir.filePath("translation/models/Hy-MT2-7B-Q4_K_M.gguf");QVERIFY(QFile::copy(legacy,quality));
        TranslationService service(dir.path());service.setModel(legacy);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translated),errors(&service,&TranslationService::failed),skipped(&service,&TranslationService::skipped);
        const QString source=QString::fromUtf8("行かなくてもいい。");service.request({source});QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);
        QCOMPARE(results.last()[2].toString(),QString::fromUtf8("안녕하세요."));const auto oldGeneration=service.generation();
        service.setModel(quality);QVERIFY(service.generation()>oldGeneration);service.request({"bad",source});
        QTRY_COMPARE_WITH_TIMEOUT(results.count(),2,3000);QCOMPARE(errors.count(),0);QCOMPARE(skipped.count(),1);
        QCOMPARE(results.last()[1].toString(),source);QCOMPARE(results.last()[2].toString(),QString::fromUtf8("가지 않아도 돼."));
        QCOMPARE(results.last()[0].toULongLong(),service.generation());QCOMPARE(service.selectedBackend(),QString("CPU / llama.cpp"));
        QCOMPARE(contents(quality+".requests").count('\n'),2);service.request({source});QTRY_COMPARE_WITH_TIMEOUT(results.count(),3,1000);QCOMPARE(contents(quality+".requests").count('\n'),2);
        service.setModel(legacy);service.request({source});QTRY_COMPARE_WITH_TIMEOUT(results.count(),4,3000);
        QCOMPARE(results.last()[2].toString(),QString::fromUtf8("안녕하세요."));QCOMPARE(contents(legacy+".requests").count('\n'),2);QCOMPARE(errors.count(),0);
    }
    void changingModelInvalidatesInFlightLegacyResponseBeforePlainRequest(){
        QTemporaryDir dir;const auto legacy=prepare(dir),quality=dir.filePath("translation/models/Hy-MT2-7B-Q4_K_M.gguf");QVERIFY(QFile::copy(legacy,quality));
        TranslationService service(dir.path());service.setModel(legacy);service.setPreferences({"cpu",{}});service.setTimeouts(2000,1000);
        QSignalSpy results(&service,&TranslationService::translated),errors(&service,&TranslationService::failed),skipped(&service,&TranslationService::skipped);
        service.request({"遅い"});QTRY_COMPARE_WITH_TIMEOUT(contents(legacy+".requests").count('\n'),1,2000);const auto previous=service.generation();
        service.setModel(quality);service.request({"新しい原文"});QTRY_COMPARE_WITH_TIMEOUT(results.count(),1,3000);QTest::qWait(400);
        QCOMPARE(errors.count(),0);QCOMPARE(skipped.count(),0);QCOMPARE(results.count(),1);QVERIFY(service.generation()>previous);
        QCOMPARE(results.first()[0].toULongLong(),service.generation());QCOMPARE(results.first()[1].toString(),QString::fromUtf8("新しい原文"));QCOMPARE(results.first()[2].toString(),QString::fromUtf8("가지 않아도 돼."));
    }
};
QTEST_GUILESS_MAIN(TranslationServiceTests)
#include "translation_service_tests.moc"
