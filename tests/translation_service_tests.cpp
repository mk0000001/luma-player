#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <algorithm>
#include "subtitles/translation_service.h"
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
    void missingModelReportsFailureWithoutResults(){
        QTemporaryDir dir;TranslationService service(dir.path());QSignalSpy errors(&service,&TranslationService::failed),results(&service,&TranslationService::translated);
        service.request({"こんにちは"});QTRY_COMPARE_WITH_TIMEOUT(errors.count(),1,1000);QCOMPARE(results.count(),0);QVERIFY(!service.busy());
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
};
QTEST_GUILESS_MAIN(TranslationServiceTests)
#include "translation_service_tests.moc"
