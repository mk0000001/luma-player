#pragma once
#include <QString>
#include <QVector>
#include <QSet>
#include <QMetaType>
#include <optional>
namespace luma {
struct SubtitleTokenTiming {
    qsizetype textStart=0,textLength=0;
    qint64 startMs=0,endMs=0;
    bool operator==(const SubtitleTokenTiming &)const=default;
};
struct SubtitleCue { qint64 startMs = 0, endMs = 0; QString text; QVector<SubtitleTokenTiming> tokens; };
QVector<SubtitleCue> parseSrt(const QString &text);
QVector<SubtitleCue> captionPages(const QVector<SubtitleCue> &cues,int maxUnits=60,qint64 minPageMs=900);
// The original page boundaries are part of prepared project keys and exported SRTs.
QVector<SubtitleCue> preparedCaptionPages(const QVector<SubtitleCue> &cues);
class CaptionCoverage {
public:
    static constexpr qint64 chunkMs=15000;
    static constexpr qint64 lookAheadMs=10000;
    static constexpr qint64 prefetchMs=60000;
    std::optional<qint64> begin(qint64 positionMs,qint64 durationMs,qint64 prerollMs=0,qint64 limitMs=chunkMs);
    qint64 pendingDuration()const{return pending_?pending_->second-pending_->first:0;}
    qint64 bufferedAhead(qint64 positionMs,qint64 durationMs)const;
    bool readyForPlayback(qint64 positionMs,qint64 durationMs)const;
    void complete();
    void cancel();
    void reset();
private:
    QVector<QPair<qint64,qint64>> completed_;
    std::optional<QPair<qint64,qint64>> pending_;
};
class SubtitleTimeline {
public:
    quint64 reset();
    bool attach(quint64 generation, QVector<SubtitleCue> cues);
    QString at(qint64 positionMs) const;
    quint64 generation() const { return generation_; }
private:
    quint64 generation_ = 0;
    QVector<SubtitleCue> cues_;
    QVector<qint64> maximumEnds_;
};
}
Q_DECLARE_METATYPE(QVector<luma::SubtitleCue>)
