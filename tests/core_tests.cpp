#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QtEndian>
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
        const auto info = inspectDisc(d.path()); QVERIFY(info.valid); QVERIFY(info.aacs); QCOMPARE(info.menuKind, QString("HDMV"));
    }
    void boundedPlaylist() {
        QByteArray b(74, '\0'); b.replace(0,8,"MPLS0200"); put32(b,8,40); put32(b,40,30); put16(b,46,1); put16(b,50,20);
        b.replace(52,5,"00002"); b.replace(57,4,"M2TS"); put32(b,64,45000); put32(b,68,495000);
        const auto title = parsePlaylist(b,"00001"); QCOMPARE(title.durationMs,10000); QCOMPARE(title.clips,QStringList{"00002"});
        b.truncate(65); QCOMPARE(parsePlaylist(b,"broken").durationMs,0);
        put32(b,8,0xfffffff0); QCOMPARE(parsePlaylist(b,"offset").durationMs,0);
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
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
