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
    }
    void cancelledCaptionChunkCanRetry() {
        CaptionCoverage coverage;QCOMPARE(coverage.begin(5000,90000).value(),5000);QVERIFY(!coverage.begin(5000,90000));
        coverage.cancel();QCOMPARE(coverage.begin(5000,90000).value(),5000);coverage.complete();
        QVERIFY(!coverage.begin(5000,90000));coverage.reset();QCOMPARE(coverage.begin(5000,90000).value(),5000);
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
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
