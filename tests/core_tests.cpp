#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QtEndian>
#include <QUrl>
#include "core/disc.h"
#include "core/subtitle.h"
using namespace luma;
namespace {
void put32(QByteArray &b, int offset, quint32 n) { qToBigEndian(n, reinterpret_cast<uchar *>(b.data() + offset)); }
void put16(QByteArray &b, int offset, quint16 n) { qToBigEndian(n, reinterpret_cast<uchar *>(b.data() + offset)); }
void writeFile(const QString &path, const QByteArray &bytes) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes), bytes.size()); }
}
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void timeFormatting() { QCOMPARE(formatTime(3661000), QString("01:01:01")); QCOMPARE(formatTime(-1), QString("00:00")); }
    void missingDisc() { QTemporaryDir d; QVERIFY(!inspectDisc(d.path()).valid); QVERIFY(!inspectDisc(d.path()).error.isEmpty()); }
    void malformedDisc() {
        QTemporaryDir d; QDir(d.path()).mkpath("BDMV");
        for (const auto &bytes : {QByteArray("INDX"), QByteArray(64, '\0')}) {
            writeFile(d.filePath("BDMV/index.bdmv"), bytes); QVERIFY(!inspectDisc(d.path()).valid);
        }
    }
    void protectedHdmvDisc() {
        QTemporaryDir d; QDir(d.path()).mkpath("BDMV"); QDir(d.path()).mkpath("AACS");
        QByteArray b(128, '\0'); b.replace(0,8,"INDX0200"); put32(b,8,64); put32(b,64,60);
        writeFile(d.filePath("BDMV/index.bdmv"), b); writeFile(d.filePath("BDMV/MovieObject.bdmv"), QByteArray("MOBJ0200"));
        auto url=QUrl::fromLocalFile(d.path());url.setScheme("bluray");const auto fromUrl=inspectDisc(url.toString());
        QVERIFY(fromUrl.valid);QVERIFY(fromUrl.aacs);QCOMPARE(fromUrl.root,inspectDisc(d.path()).root);
        auto singleSlash=url.toString(QUrl::FullyEncoded);singleSlash.replace("bluray:///","bluray:/");
        const auto fromSingleSlash=inspectDisc(singleSlash);QVERIFY(fromSingleSlash.valid);QCOMPARE(fromSingleSlash.root,fromUrl.root);
        const auto info = inspectDisc(d.path()); QVERIFY(info.valid); QVERIFY(info.aacs); QCOMPARE(info.menuKind, QString("HDMV"));
    }
    void boundedPlaylist() {
        QByteArray b(74, '\0'); b.replace(0,8,"MPLS0200"); put32(b,8,40); put32(b,40,30); put16(b,46,1); put16(b,50,20);
        b.replace(52,5,"00002"); b.replace(57,4,"M2TS"); put32(b,64,45000); put32(b,68,495000);
        const auto title = parsePlaylist(b,"00001"); QCOMPARE(title.durationMs,10000); QCOMPARE(title.clips,QStringList{"00002"});
        b.truncate(65); QCOMPARE(parsePlaylist(b,"broken").durationMs,0);
        put32(b,8,0xfffffff0); QCOMPARE(parsePlaylist(b,"offset").durationMs,0);
    }
    void ambiguousDiscTitlesDoNotProduceWrongCaptions(){
        DiscInfo d;d.titles={{"00002",6294288,{"00002"}},{"00003",6296832,{"00006"}}};
        QCOMPARE(matchDiscPlaylist(d,6294288),QString("00002"));QCOMPARE(matchDiscPlaylist(d,6296832),QString("00003"));QVERIFY(matchDiscPlaylist(d,123).isEmpty());
        d.titles.append({"00004",6294288,{"00007"}});QVERIFY(matchDiscPlaylist(d,6294288).isEmpty());
    }
    void unicodeMultilineSrt() {
        const auto cues = parseSrt(QString::fromUtf8("\xEF\xBB\xBF" "1\r\n00:00:01,200 --> 00:00:03,400\r\nこんにちは\r\n안녕하세요\r\n\r\n2\r\n00:00:04.000 --> 00:00:05.000\r\nHello\r\n"));
        QCOMPARE(cues.size(),2); QCOMPARE(cues[0].startMs,1200); QCOMPARE(cues[0].text,QString::fromUtf8("こんにちは\n안녕하세요"));
    }
    void rejectsInvalidSrt() { QVERIFY(parseSrt("1\n00:00:05,000 --> 00:00:01,000\nwrong\n\n2\n00:99:00,000 --> 01:00:00,000\ninvalid").isEmpty()); }
    void timelineOverlapAndStaleJobs() {
        SubtitleTimeline t; const auto old = t.reset(); const auto fresh = t.reset();
        QVERIFY(!t.attach(old,{{0,2000,"stale"}})); QVERIFY(t.attach(fresh,{{1000,3000,"one"},{2000,4000,"two"}}));
        QCOMPARE(t.at(2500),QString("one\ntwo")); QCOMPARE(t.at(4000),QString()); QCOMPARE(t.at(1000),QString("one"));
        const auto generation=t.reset();
        QVERIFY(t.attach(generation,{{6000,7000,"later"},{0,8000,"long"},{1000,2000,"expired"},{6000,7500,"same start"}}));
        QCOMPARE(t.at(6500),QString("long\nlater\nsame start"));
        QCOMPARE(t.at(7600),QString("long"));
        QCOMPARE(t.at(8000),QString{});
        QCOMPARE(t.at(1500),QString("long\nexpired"));
    }
    void cancelledCaptionChunkCanRetry() {
        CaptionCoverage coverage;QCOMPARE(coverage.begin(5000,90000).value(),5000);QVERIFY(!coverage.begin(5000,90000));
        coverage.cancel();QCOMPARE(coverage.begin(5000,90000).value(),5000);coverage.complete();
        QVERIFY(coverage.readyForPlayback(5000,90000));QCOMPARE(coverage.begin(5000,90000).value(),20000);coverage.reset();QCOMPARE(coverage.begin(5000,90000).value(),5000);
    }
    void captionBufferTracksCoverageInsteadOfText(){
        CaptionCoverage coverage;QVERIFY(!coverage.readyForPlayback(1250,90000));
        QCOMPARE(coverage.begin(1250,90000).value(),1250);coverage.complete();
        QCOMPARE(coverage.bufferedAhead(1250,90000),qint64(15000));QVERIFY(coverage.readyForPlayback(6250,90000));
        QVERIFY(!coverage.readyForPlayback(6251,90000));QCOMPARE(coverage.begin(6251,90000).value(),16250);coverage.complete();
        QCOMPARE(coverage.bufferedAhead(10000,90000),qint64(21250));
        QCOMPARE(coverage.begin(88000,90000).value(),88000);coverage.complete();QVERIFY(coverage.readyForPlayback(88000,90000));
        QVERIFY(!coverage.readyForPlayback(60000,90000));QVERIFY(!coverage.readyForPlayback(-1,90000));
    }
    void discSeekPrerollCoversTheActualBackwardClockCorrection(){
        CaptionCoverage coverage;constexpr qint64 duration=6294288,target=709366;
        const auto start=coverage.begin(target,duration,2000);QVERIFY(start);QCOMPARE(*start,qint64(707366));
        QVERIFY(!coverage.readyForPlayback(708461,duration));coverage.complete();
        QVERIFY(coverage.readyForPlayback(708461,duration));QVERIFY(coverage.readyForPlayback(708373,duration));
        QCOMPARE(coverage.bufferedAhead(708373,duration),qint64(13993));
        // Only audio actually read is covered; the preroll must not invent earlier readiness.
        QCOMPARE(coverage.bufferedAhead(707365,duration),qint64(0));QVERIFY(!coverage.readyForPlayback(707365,duration));
        QCOMPARE(coverage.begin(720000,duration,2000).value(),qint64(722366));coverage.complete();
        QCOMPARE(coverage.bufferedAhead(720000,duration),qint64(17366));
        coverage.reset();QCOMPARE(coverage.begin(500,duration,2000).value(),qint64(0));
    }
    void discPrerollReusesPreviouslyRecognizedAudio(){
        CaptionCoverage coverage;QCOMPARE(coverage.begin(0,90000).value(),qint64(0));coverage.complete();
        // The old range supplies the preroll; do not recognize its captions twice.
        QCOMPARE(coverage.begin(16000,90000,2000).value(),qint64(15000));coverage.complete();
        QVERIFY(coverage.readyForPlayback(14900,90000));QCOMPARE(coverage.bufferedAhead(16000,90000),qint64(14000));
    }
    void backwardSeekNeverRecognizesCachedForwardCaptionsTwice(){
        CaptionCoverage coverage;QCOMPARE(coverage.begin(98000,200000).value(),qint64(98000));coverage.complete();
        QCOMPARE(coverage.begin(95000,200000,2000).value(),qint64(93000));
        QCOMPARE(coverage.pendingDuration(),qint64(5000));coverage.complete();
        QCOMPARE(coverage.bufferedAhead(95000,200000),qint64(18000));
        QCOMPARE(coverage.begin(95000,200000,2000).value(),qint64(113000));
    }
    void captionCoverageKeepsPrefetchingAfterPlaybackIsReady(){
        CaptionCoverage coverage;QCOMPARE(coverage.begin(0,120000).value(),qint64(0));coverage.complete();
        QVERIFY(coverage.readyForPlayback(0,120000));
        for(const qint64 start:{15000,30000,45000}){const auto next=coverage.begin(0,120000);QVERIFY2(next.has_value(),"Reaching the ten-second minimum must not stop sixty-second prefetch");QCOMPARE(*next,start);coverage.complete();}
        QCOMPARE(coverage.bufferedAhead(0,120000),qint64(60000));QVERIFY(!coverage.begin(0,120000));
        QCOMPARE(coverage.begin(5000,120000).value(),qint64(60000));coverage.complete();
        QVERIFY(coverage.bufferedAhead(5000,120000)>=60000);
    }
    void longCaptionsUseShortConsecutivePagesWithoutDroppingText(){
        const auto pages=captionPages({{1000,2000,"ABCDEFGHIJ"}},4,200);
        QCOMPARE(pages.size(),3);QCOMPARE(pages[0].text,QString("ABCD"));QCOMPARE(pages[1].text,QString("EFGH"));QCOMPARE(pages[2].text,QString("IJ"));
        QCOMPARE(pages[0].startMs,qint64(1000));QCOMPARE(pages[0].endMs,qint64(1400));QCOMPARE(pages[1].startMs,qint64(1400));QCOMPARE(pages[1].endMs,qint64(1800));QCOMPARE(pages[2].startMs,qint64(1800));QCOMPARE(pages[2].endMs,qint64(2000));
    }
    void timedPagesFollowNonUniformSpokenTokens(){
        SubtitleCue cue{1000,9000,"ABCDEFGH"};
        cue.tokens={{0,2,1000,7000},{2,2,7000,7300},{4,2,7300,8800},{6,2,8800,9000}};
        const auto pages=captionPages({cue},4,1);
        QCOMPARE(pages.size(),2);QCOMPARE(pages[0].text,QString("ABCD"));QCOMPARE(pages[1].text,QString("EFGH"));
        QCOMPARE(pages[0].startMs,qint64(1000));QCOMPARE(pages[0].endMs,qint64(7300));
        QCOMPARE(pages[1].startMs,qint64(7300));QCOMPARE(pages[1].endMs,qint64(9000));
        SubtitleTimeline timeline;const auto generation=timeline.reset();QVERIFY(timeline.attach(generation,pages));
        QCOMPARE(timeline.at(7299),QString("ABCD"));QCOMPARE(timeline.at(7300),QString("EFGH"));
    }
    void timedPagesKeepTokenBoundariesAndSilentGaps(){
        SubtitleCue cue{0,5000,"ABCDEFGH"};
        cue.tokens={{0,5,800,2200},{5,3,3500,4600}};
        const auto pages=captionPages({cue},4,1);
        QCOMPARE(pages.size(),2);QCOMPARE(pages[0].text,QString("ABCDE"));QCOMPARE(pages[1].text,QString("FGH"));
        QCOMPARE(pages[0].startMs,qint64(800));QCOMPARE(pages[0].endMs,qint64(2200));
        QCOMPARE(pages[1].startMs,qint64(3500));QCOMPARE(pages[1].endMs,qint64(4600));
    }
    void invalidTokenTimingUsesLegacyCueBounds(){
        SubtitleCue cue{1000,5000,"ABCDEFGH"};cue.tokens={{0,8,7000,8000}};
        const auto pages=captionPages({cue},4,1);QCOMPARE(pages.size(),2);
        QCOMPARE(pages[0].startMs,qint64(1000));QCOMPARE(pages[0].endMs,qint64(3000));
    }
    void timedPagesDoNotSplitATokenAcrossACombiningGrapheme(){
        SubtitleCue cue{0,5000,QString::fromUtf8("AがBC")};
        cue.tokens={{0,2,0,2000},{2,2,2000,4000},{4,1,4000,5000}};
        const auto pages=captionPages({cue},1,1);
        QCOMPARE(pages.size(),2);QCOMPARE(pages[0].text,QString::fromUtf8("AがB"));QCOMPARE(pages[0].endMs,qint64(4000));QCOMPARE(pages[1].text,QString("C"));
    }
    void captionPagesPreferWholeSentencesAndWords(){
        const auto japanese=captionPages({{0,12000,QString::fromUtf8("あいうえお。かきくけこ。")}},16,900);
        QCOMPARE(japanese.size(),2);QCOMPARE(japanese[0].text,QString::fromUtf8("あいうえお。"));QCOMPARE(japanese[1].text,QString::fromUtf8("かきくけこ。"));QCOMPARE(japanese[0].endMs,qint64(6000));QCOMPARE(japanese[1].startMs,qint64(6000));
        const auto words=captionPages({{0,9000,"Keep every word in this very long sentence."}},14,900);QStringList text;
        for(const auto &page:words)text.append(page.text);QCOMPARE(text.join(' '),QString("Keep every word in this very long sentence."));
    }
    void captionPagesPreferCompleteSentencesEvenBelowTheWidthLimit(){
        const auto check=[](const QString &text,const QStringList &expected){
            QStringList actual;
            for(const auto &page:captionPages({{0,16000,text}}))actual.append(page.text);
            QCOMPARE(actual,expected);
        };
        check(QString::fromUtf8("「そうですね。」次に進みましょう。"),{QString::fromUtf8("「そうですね。」"),QString::fromUtf8("次に進みましょう。")});
        check(QString::fromUtf8("조금 기다려요. 이제 다시 시작해요."),{QString::fromUtf8("조금 기다려요."),QString::fromUtf8("이제 다시 시작해요.")});
        check("It is 12.5 seconds. Then start again.",{"It is 12.5 seconds.","Then start again."});
    }
    void captionPagesKeepJapaneseTransitionWithItsSpokenPhrase(){
        const auto pages=captionPages({{0,10000,QString::fromUtf8("公園で写真を撮ってから駅まで歩きました")}},27,600);
        QCOMPARE(pages.size(),2);
        QCOMPARE(pages[0].text,QString::fromUtf8("公園で写真を撮ってから"));
        QCOMPARE(pages[1].text,QString::fromUtf8("駅まで歩きました"));
    }
    void captionPagesUseTokenTimesForBothSidesOfASentencePause(){
        SubtitleCue cue{0,8000,QString::fromUtf8("그래요. 기다릴게요.")};
        cue.tokens={{0,4,500,1500},{4,7,3700,6200}};
        const auto pages=captionPages({cue});
        QCOMPARE(pages.size(),2);
        QCOMPARE(pages[0].text,QString::fromUtf8("그래요."));
        QCOMPARE(pages[0].startMs,qint64(500));
        QCOMPARE(pages[0].endMs,qint64(1500));
        QCOMPARE(pages[1].text,QString::fromUtf8("기다릴게요."));
        QCOMPARE(pages[1].startMs,qint64(3700));
        QCOMPARE(pages[1].endMs,qint64(6200));
    }
    void preparedCaptionPagesKeepExistingTranslationCueBoundaries(){
        const QVector<SubtitleCue> stored{{1000,7000,QString::fromUtf8("기다려요. 이제 시작해요.")}};
        QCOMPARE(captionPages(stored).size(),2);
        const auto prepared=preparedCaptionPages(stored);
        QCOMPARE(prepared.size(),1);
        QCOMPARE(prepared[0].text,stored[0].text);
        QCOMPARE(prepared[0].startMs,stored[0].startMs);
        QCOMPARE(prepared[0].endMs,stored[0].endMs);
    }
    void captionPagesPreserveEmojiAndCombiningCharacters(){
        const auto pages=captionPages({{0,7000,QString::fromUtf8("A👩‍💻BがC")}},3,100);
        QCOMPARE(pages.size(),3);QCOMPARE(pages[0].text,QString::fromUtf8("A👩‍💻"));QCOMPARE(pages[1].text,QString::fromUtf8("Bが"));QCOMPARE(pages[2].text,QString("C"));
        QCOMPARE(pages[0].endMs,qint64(3000));QCOMPARE(pages[1].endMs,qint64(6000));QCOMPARE(pages[2].endMs,qint64(7000));
    }
    void veryShortCaptionDoesNotBecomeUnreadableFlashes(){
        const auto pages=captionPages({{500,1500,"ABCDEFGHIJ"}},4,900);QCOMPARE(pages.size(),1);QCOMPARE(pages[0].text,QString("ABCDEFGHIJ"));QCOMPARE(pages[0].startMs,qint64(500));QCOMPARE(pages[0].endMs,qint64(1500));
        QVERIFY(captionPages({{100,100,"Invalid"},{0,1000,""}}).isEmpty());
    }
    void punctuationDoesNotBecomeTheFirstCharacterOfANewPage(){
        const auto pages=captionPages({{0,12000,QString(30,QChar(0x82b1))+QString::fromUtf8("。")+QString(10,QChar(0x98a8))}});
        QCOMPARE(pages.size(),2);QVERIFY(pages[0].text.endsWith(QChar(0x3002)));QCOMPARE(pages[1].text.front(),QChar(0x98a8));
        const auto ascii=captionPages({{0,12000,QString(60,'A')+", "+QString(20,'B')}});QCOMPARE(ascii.size(),2);QVERIFY(ascii[0].text.endsWith(','));QCOMPARE(ascii[1].text,QString(20,'B'));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
