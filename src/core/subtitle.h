#pragma once
#include <QString>
#include <QVector>
namespace luma {
struct SubtitleCue { qint64 startMs = 0, endMs = 0; QString text; };
QVector<SubtitleCue> parseSrt(const QString &text);
class SubtitleTimeline {
public:
    quint64 reset();
    bool attach(quint64 generation, QVector<SubtitleCue> cues);
    QString at(qint64 positionMs) const;
    quint64 generation() const { return generation_; }
private:
    quint64 generation_ = 0;
    QVector<SubtitleCue> cues_;
};
}
