#include <QtTest>
#include <QElapsedTimer>
#include "subtitles/translation_service.h"
using namespace luma;
class TranslationNativeTests:public QObject{
    Q_OBJECT
private slots:
    void generatedJapaneseCaptionUsesActualLocalBackend(){
        const auto runtime=qEnvironmentVariable("LUMA_TRANSLATION_RUNTIME");
        if(runtime.isEmpty())QSKIP("Set LUMA_TRANSLATION_RUNTIME for native synthetic translation verification.");
        TranslationService service(runtime);service.setPreferences({qEnvironmentVariable("LUMA_TRANSLATION_MODE","gpu"),qEnvironmentVariable("LUMA_TRANSLATION_DEVICE","Vulkan0")});
        service.setTimeouts(75000,45000);
        QSignalSpy result(&service,&TranslationService::translated),errors(&service,&TranslationService::failed),progress(&service,&TranslationService::progress);
        QElapsedTimer timer;timer.start();service.request({"今日はいい天気ですね。"});
        QTRY_VERIFY_WITH_TIMEOUT(!result.isEmpty()||!errors.isEmpty(),150000);
        for(const auto &entry:progress)qInfo().noquote()<<entry[0].toString();
        QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.first()[0].toString()));QCOMPARE(result.count(),1);
        qInfo().noquote()<<"backend="<<service.selectedBackend()<<"first_ms="<<timer.elapsed()<<"source="<<result.first()[1].toString()<<"korean="<<result.first()[2].toString();
        QVERIFY(result.first()[2].toString().contains("날씨"));
        const auto expected=qEnvironmentVariable("LUMA_TRANSLATION_EXPECT_BACKEND");if(!expected.isEmpty())QVERIFY2(service.selectedBackend().startsWith(expected),qPrintable(service.selectedBackend()));
        timer.restart();service.request({"明日の午後三時に、図書館の前で会いましょう。"});QTRY_COMPARE_WITH_TIMEOUT(result.count(),2,45000);
        qInfo().noquote()<<"warm_ms="<<timer.elapsed()<<"korean="<<result.last()[2].toString();QVERIFY(result.last()[2].toString().contains("도서관"));
        timer.restart();service.request({"今日はいい天気ですね。"});QTRY_COMPARE_WITH_TIMEOUT(result.count(),3,1000);qInfo()<<"cached_ms="<<timer.elapsed();
        const auto before=service.generation();service.request({"この文章は取り消してください。"});service.cancel();QVERIFY(service.generation()>before);QTest::qWait(100);QCOMPARE(result.count(),3);
    }
};
QTEST_GUILESS_MAIN(TranslationNativeTests)
#include "translation_native_tests.moc"
