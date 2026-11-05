#include "subtitle.h"
#include <QRegularExpression>
#include <algorithm>
namespace luma {
QVector<SubtitleCue> parseSrt(const QString &input) {
    QString text = input; text.remove(QChar(0xfeff)); text.replace("\r\n","\n"); text.replace('\r','\n');
    static const QRegularExpression timing(R"(^(\d{1,3}):([0-5]\d):([0-5]\d)[,.](\d{3})\s+-->\s+(\d{1,3}):([0-5]\d):([0-5]\d)[,.](\d{3})\s*$)");
    QVector<SubtitleCue> cues;
    for (const auto &block : text.split(QRegularExpression("\n[\\t ]*\n"),Qt::SkipEmptyParts)) {
        auto lines = block.trimmed().split('\n');
        if (lines.size()<2) continue;
        int index = lines[0].contains("-->") ? 0 : 1;
        if (index>=lines.size()-1) continue;
        const auto m = timing.match(lines[index].trimmed());
        if (!m.hasMatch()) continue;
        auto timestamp = [&m](int n) -> qint64 { return ((m.captured(n).toLongLong()*60+m.captured(n+1).toLongLong())*60+m.captured(n+2).toLongLong())*1000+m.captured(n+3).toLongLong(); };
        const qint64 start = timestamp(1), end = timestamp(5);
        if (end<=start) continue;
        const auto content = lines.mid(index+1).join('\n').trimmed();
        if (!content.isEmpty()) cues.append({start,end,content});
    }
    std::stable_sort(cues.begin(),cues.end(),[](const auto &a,const auto &b){ return a.startMs<b.startMs; });
    return cues;
}
quint64 SubtitleTimeline::reset() { cues_.clear(); return ++generation_; }
bool SubtitleTimeline::attach(quint64 generation, QVector<SubtitleCue> cues) {
    if (generation!=generation_) return false;
    std::stable_sort(cues.begin(),cues.end(),[](const auto &a,const auto &b){ return a.startMs<b.startMs; });
    cues_=std::move(cues); return true;
}
QString SubtitleTimeline::at(qint64 positionMs) const {
    QStringList active;
    for (const auto &cue : cues_) { if (cue.startMs>positionMs) break; if (positionMs<cue.endMs) active.append(cue.text); }
    return active.join('\n');
}
}
