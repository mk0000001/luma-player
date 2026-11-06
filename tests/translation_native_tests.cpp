#include <QtTest>
#include <QElapsedTimer>
#include <QRegularExpression>
#include "subtitles/translation_service.h"
#include "subtitles/translation_support.h"
using namespace luma;
class TranslationNativeTests:public QObject{
    Q_OBJECT
private slots:
    void timedContextCaptionsUseActualLocalBackend(){
        const auto runtime=qEnvironmentVariable("LUMA_TRANSLATION_RUNTIME");
        if(runtime.isEmpty())QSKIP("Set LUMA_TRANSLATION_RUNTIME for native synthetic context translation verification.");
        TranslationService service(runtime);service.setPreferences({qEnvironmentVariable("LUMA_TRANSLATION_MODE","gpu"),qEnvironmentVariable("LUMA_TRANSLATION_DEVICE","Vulkan0")});
        const auto model=qEnvironmentVariable("LUMA_TRANSLATION_MODEL");if(!model.isEmpty())service.setModel(model);
        service.setTimeouts(75000,45000);
        QSignalSpy results(&service,&TranslationService::translatedCue),legacy(&service,&TranslationService::translated),errors(&service,&TranslationService::failed),skips(&service,&TranslationService::skippedCue),progress(&service,&TranslationService::progress);
        const SubtitleCue first{65000,68000,QString::fromUtf8("それを持ってきて。")},second{160000,165000,QString::fromUtf8("彼女はもう着いた？雨が降る前に駅まで迎えに行こう。")};
        const QVector<SubtitleCue> originals{
            {71000,74000,QString::fromUtf8("その工具箱で自転車を直すよ。")},first,{60000,63000,QString::fromUtf8("赤い工具箱が棚の上にある。")},
            {168000,171000,QString::fromUtf8("ミナが改札の前で待っているよ。")},second,{155000,158000,QString::fromUtf8("ミナは駅に向かっている。")}};
        service.setContextCues(originals);const auto initialGeneration=service.generation();
        const QRegularExpression hangul("[\\x{AC00}-\\x{D7A3}\\x{1100}-\\x{11FF}]");
        const QRegularExpression foreignOrArtifact("[A-Za-z\\x{3040}-\\x{30FF}\\x{3400}-\\x{9FFF}]|[{}<>]|```|(?:번역|문맥|대상|설명)[ ]*[:：]");
        QElapsedTimer timer;int index=0;
        for(const auto &target:QVector<SubtitleCue>{first,second}){
            timer.restart();service.requestCues({target});
            QTRY_VERIFY_WITH_TIMEOUT(results.count()>index || !errors.isEmpty() || !skips.isEmpty(),150000);
            QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.first()[0].toString()));
            QVERIFY2(skips.isEmpty(),skips.isEmpty()?"":qPrintable(skips.first()[3].toString()));QCOMPARE(results.count(),index+1);
            const auto result=results[index];const auto korean=result[3].toString();
            QCOMPARE(result[0].toULongLong(),initialGeneration);QCOMPARE(result[1].toString(),translationCueKey(target));QCOMPARE(result[2].toString(),target.text);
            QVERIFY(hangul.match(korean).hasMatch());QVERIFY(!foreignOrArtifact.match(korean).hasMatch());QVERIFY(!korean.contains('\n'));
            QVERIFY(korean.size()<=qMin<qsizetype>(2048,qMax<qsizetype>(80,target.text.size()*4+32)));
            qInfo().noquote()<<"context_smoke_index="<<index<<"backend="<<service.selectedBackend()<<"elapsed_ms="<<timer.elapsed()<<"target_key="<<result[1].toString()<<"source="<<target.text<<"korean="<<korean;
            ++index;
        }
        const auto expected=qEnvironmentVariable("LUMA_TRANSLATION_EXPECT_BACKEND");if(!expected.isEmpty())QVERIFY2(service.selectedBackend().startsWith(expected),qPrintable(service.selectedBackend()));
        QCOMPARE(legacy.count(),0);
        // A new neighbor invalidates the second cue's cache before cancellation.
        auto revised=originals;revised.append({173000,175000,QString::fromUtf8("傘を忘れずに持っていこう。")});service.setContextCues(revised);
        service.requestCues({second});QTest::qWait(25);QVERIFY(service.busy());QCOMPARE(results.count(),2);
        const auto cancelledGeneration=service.generation();service.cancel();QVERIFY(service.generation()>cancelledGeneration);
        service.setContextCues(originals);timer.restart();service.requestCues({first});
        QTRY_VERIFY_WITH_TIMEOUT(results.count()>2 || !errors.isEmpty() || !skips.isEmpty(),45000);
        QVERIFY2(errors.isEmpty(),errors.isEmpty()?"":qPrintable(errors.first()[0].toString()));QVERIFY(skips.isEmpty());QCOMPARE(results.count(),3);
        QCOMPARE(results.last()[0].toULongLong(),service.generation());QCOMPARE(results.last()[1].toString(),translationCueKey(first));QCOMPARE(results.last()[2].toString(),first.text);
        QTest::qWait(250);QCOMPARE(results.count(),3);QVERIFY(!service.busy());
        qInfo().noquote()<<"context_smoke_cancelled_generation="<<cancelledGeneration<<"resumed_generation="<<service.generation()<<"resumed_ms="<<timer.elapsed()<<"korean="<<results.last()[3].toString();
        for(const auto &entry:progress)qInfo().noquote()<<entry[0].toString();
    }
    void generatedJapaneseCaptionUsesActualLocalBackend(){
        const auto runtime=qEnvironmentVariable("LUMA_TRANSLATION_RUNTIME");
        if(runtime.isEmpty())QSKIP("Set LUMA_TRANSLATION_RUNTIME for native synthetic translation verification.");
        TranslationService service(runtime);service.setPreferences({qEnvironmentVariable("LUMA_TRANSLATION_MODE","gpu"),qEnvironmentVariable("LUMA_TRANSLATION_DEVICE","Vulkan0")});
        const auto model=qEnvironmentVariable("LUMA_TRANSLATION_MODEL");if(!model.isEmpty())service.setModel(model);
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
