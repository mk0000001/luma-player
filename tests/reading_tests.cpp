#include <QtTest>
#include <QDir>
#include <QFile>
#include "subtitles/japanese_reading.h"
using namespace luma;
class ReadingTests:public QObject {
    Q_OBJECT
private slots:
    void kana_data(){
        QTest::addColumn<QString>("input");QTest::addColumn<QString>("expected");
        QTest::newRow("greeting")<<QString("コンニチワ")<<QString("곤니치와");
        QTest::newRow("hiragana")<<QString("ありがとう")<<QString("아리가토우");
        QTest::newRow("digraphs")<<QString("シャシシュシェショ チャチチュチェチョ")<<QString("샤시슈셰쇼 차치추체초");
        QTest::newRow("sokuon")<<QString("ガッコー イッショ キップ マッテ")<<QString("각코오 잇쇼 깁푸 맛테");
        QTest::newRow("nasal")<<QString("サンポ シンカンセン")<<QString("삼포 싱칸센");
        QTest::newRow("half-width")<<QString("ｺﾝﾆﾁﾜ")<<QString("곤니치와");
        QTest::newRow("punctuation")<<QString("ハイ、Luma! 2026")<<QString("하이、Luma! 2026");
        QTest::newRow("non-japanese")<<QString("안녕하세요 English 123")<<QString("안녕하세요 English 123");
    }
    void kana(){QFETCH(QString,input);QFETCH(QString,expected);QCOMPARE(kanaToHangul(input),expected);}
    void dictionaryReadsKanjiAndParticles(){
        const auto runtime=qEnvironmentVariable("LUMA_READING_RUNTIME");if(runtime.isEmpty())QSKIP("Set LUMA_READING_RUNTIME for the actual dictionary integration test");
        JapaneseReading reader(runtime);QVERIFY2(reader.available(),qPrintable(reader.error()));
        QCOMPARE(reader.reading("こんにちは"),QString("곤니치와"));
        const auto annotated=reader.annotate("私は音楽が好きです。");
        QVERIFY(annotated.startsWith("私は音楽が好きです。\n"));
        QVERIFY2(annotated.contains("와타시"),qPrintable(annotated));
        QVERIFY2(annotated.contains("옹가쿠"),qPrintable(annotated));
        QVERIFY2(annotated.contains("스키"),qPrintable(annotated));
        QCOMPARE(reader.annotate("Hello, world!"),QString("Hello, world!"));
        QCOMPARE(reader.annotate("안녕하세요"),QString("안녕하세요"));
        QVERIFY(!reader.annotate("未登録𠮷野").isEmpty());
    }
    void dictionaryKeepsDependentWordsTogether(){
        const auto runtime=qEnvironmentVariable("LUMA_READING_RUNTIME");if(runtime.isEmpty())QSKIP("Set LUMA_READING_RUNTIME for the actual dictionary integration test");
        JapaneseReading reader(runtime);QVERIFY2(reader.available(),qPrintable(reader.error()));
        QCOMPARE(reader.reading("私は音楽が好きです。"),QString("와타시와 옹가쿠가 스키데스。"));
        QCOMPARE(reader.reading("ゆっくり歩いていた。"),QString("육쿠리 아루이테이타。"));
    }
    void missingDictionaryKeepsOriginal(){JapaneseReading reader("/nonexistent/luma-runtime");QVERIFY(!reader.available());QVERIFY(!reader.error().isEmpty());QCOMPARE(reader.annotate("音楽"),QString("音楽"));}
};
QTEST_GUILESS_MAIN(ReadingTests)
#include "reading_tests.moc"
