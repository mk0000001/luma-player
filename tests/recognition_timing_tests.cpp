#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <limits>
#include <functional>
#include <QFile>
#include <QDir>
#include "subtitles/recognition_timing.h"
using namespace luma;
namespace {
QJsonObject fixture(){
    return {{"schema",1},{"format","luma-asr-timing-v1"},{"alignment","dtw"},{"audioMs",2000},{"segments",QJsonArray{QJsonObject{
        {"startMs",120},{"endMs",1400},{"text",QString::fromUtf8("春の空")},{"tokens",QJsonArray{
            QJsonObject{{"byteStart",0},{"byteEnd",3},{"startMs",120},{"endMs",340}},
            QJsonObject{{"byteStart",3},{"byteEnd",6},{"startMs",340},{"endMs",800}},
            QJsonObject{{"byteStart",6},{"byteEnd",9},{"startMs",800},{"endMs",1400}}}}}}}};
}
QByteArray bytes(const QJsonObject &object){return QJsonDocument(object).toJson(QJsonDocument::Compact);}
void segment(QJsonObject &object,const std::function<void(QJsonObject &)> &change){auto item=object["segments"].toArray().first().toObject();change(item);object["segments"]=QJsonArray{item};}
}
class RecognitionTimingTests:public QObject{
    Q_OBJECT
private slots:
    void tokenTimesUseMediaOriginExactlyOnce(){
        const auto parsed=parseRecognitionTiming(bytes(fixture()),60000,2000);QVERIFY(parsed);QCOMPARE(parsed->alignment,QString("dtw"));QCOMPARE(parsed->cues.size(),1);
        const auto cue=parsed->cues.first();QCOMPARE(cue.text,QString::fromUtf8("春の空"));QCOMPARE(cue.startMs,qint64(60120));QCOMPARE(cue.endMs,qint64(61400));QCOMPARE(cue.tokens.size(),3);
        QCOMPARE(cue.tokens[1],(SubtitleTokenTiming{1,1,60340,60800}));
        const auto pages=captionPages({cue},2,1);QCOMPARE(pages.size(),3);QCOMPARE(pages[1].startMs,qint64(60340));QCOMPARE(pages[1].endMs,qint64(60800));
    }
    void utf8RangesBecomeUtf16Ranges(){
        auto object=fixture();segment(object,[](QJsonObject &item){item["text"]=QString::fromUtf8("😀海");item["tokens"]=QJsonArray{
            QJsonObject{{"byteStart",0},{"byteEnd",4},{"startMs",120},{"endMs",800}},QJsonObject{{"byteStart",4},{"byteEnd",7},{"startMs",800},{"endMs",1400}}};});
        const auto parsed=parseRecognitionTiming(bytes(object),0,2000);QVERIFY(parsed);QCOMPARE(parsed->cues[0].tokens[0].textLength,qsizetype(2));QCOMPARE(parsed->cues[0].tokens[1].textStart,qsizetype(2));
    }
    void missingTokenAnchorsRetainCoarseTiming(){
        auto object=fixture();segment(object,[](QJsonObject &item){item["tokens"]=QJsonArray{};});
        const auto parsed=parseRecognitionTiming(bytes(object),10000,2000);QVERIFY(parsed);QCOMPARE(parsed->alignment,QString("segment"));QCOMPARE(parsed->cues[0].startMs,qint64(10120));QVERIFY(parsed->cues[0].tokens.isEmpty());
    }
    void malformedRangesAndTimesAreRejected_data(){
        QTest::addColumn<int>("variant");for(int i=0;i<9;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;
    }
    void malformedRangesAndTimesAreRejected(){
        QFETCH(int,variant);auto object=fixture();segment(object,[variant](QJsonObject &item){
            auto tokens=item["tokens"].toArray();auto token=tokens[1].toObject();
            switch(variant){
            case 0:token["byteStart"]=4;break;
            case 1:{auto first=tokens[0].toObject();first["byteEnd"]=2;tokens[0]=first;token["byteStart"]=2;break;}
            case 2:token["byteEnd"]=10;break;
            case 3:token["startMs"]=200;break;
            case 4:token["endMs"]=2001;break;
            case 5:token["endMs"]=340;break;
            case 6:token["startMs"]=340.5;break;
            case 7:tokens.removeLast();break;
            case 8:item["endMs"]=199;break;
            }
            tokens[1]=token;item["tokens"]=tokens;
        });QVERIFY(!parseRecognitionTiming(bytes(object),60000,2000));
    }
    void malformedEnvelopeAndOverflowAreRejected(){
        auto object=fixture();object["audioMs"]=2100;QVERIFY(!parseRecognitionTiming(bytes(object),0,2000));
        object=fixture();object["schema"]=2;QVERIFY(!parseRecognitionTiming(bytes(object),0,2000));
        object=fixture();object["alignment"]="guessed-dtw";QVERIFY(!parseRecognitionTiming(bytes(object),0,2000));
        QVERIFY(!parseRecognitionTiming(bytes(fixture()),-1,2000));QVERIFY(!parseRecognitionTiming(bytes(fixture()),std::numeric_limits<qint64>::max()-1,2000));
        object=fixture();object["padding"]=QString(1024*1024,'x');QVERIFY(!parseRecognitionTiming(bytes(object),0,2000));
    }
    void realNativeSidecarsUseTheSameParserAndPager(){
        const auto directory=qEnvironmentVariable("LUMA_NATIVE_TIMING_DIRECTORY");if(directory.isEmpty())QSKIP("Set LUMA_NATIVE_TIMING_DIRECTORY for actual worker/consumer verification.");
        const auto files=QDir(directory).entryList({"request-*.timings.json"},QDir::Files);QVERIFY(!files.isEmpty());
        int segments=0,aligned=0,tokens=0,pages=0;const auto method=qEnvironmentVariable("LUMA_NATIVE_TIMING_METHOD");
        for(const auto &name:files){QFile file(QDir(directory).filePath(name));QVERIFY(file.open(QIODevice::ReadOnly));QVERIFY(file.size()<=1024*1024);const auto parsed=parseRecognitionTiming(file.readAll(),0,60000);QVERIFY2(parsed,"Actual native sidecar rejected by the player parser");
            if(!method.isEmpty())QVERIFY(parsed->alignment.startsWith(method));
            segments+=parsed->cues.size();for(const auto &cue:parsed->cues){if(!cue.tokens.isEmpty())++aligned;tokens+=cue.tokens.size();}pages+=captionPages(parsed->cues).size();
        }
        QVERIFY(segments>0);QVERIFY(aligned>0);QVERIFY(tokens>0);QVERIFY(pages>0);qInfo()<<"files"<<files.size()<<"segments"<<segments<<"aligned"<<aligned<<"tokens"<<tokens<<"pages"<<pages;
    }
};
QTEST_GUILESS_MAIN(RecognitionTimingTests)
#include "recognition_timing_tests.moc"
