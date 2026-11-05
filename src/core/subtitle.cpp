#include "subtitle.h"
#include <QRegularExpression>
#include <algorithm>
namespace luma {
std::optional<qint64> CaptionCoverage::begin(qint64 positionMs,qint64 durationMs,qint64 prerollMs){
    if(pending_ || positionMs<0 || positionMs>=durationMs || readyForPlayback(positionMs,durationMs))return {};
    const auto ahead=bufferedAhead(positionMs,durationMs);
    // A disc seek/pause can settle slightly before the requested timestamp.
    // Recognize that real audio too; contiguous prefetch never reads it twice.
    auto start=ahead>0?positionMs+ahead:std::max<qint64>(0,positionMs-std::clamp<qint64>(prerollMs,0,chunkMs-lookAheadMs));
    if(!ahead)for(const auto &range:completed_)if(range.second<=positionMs)start=std::max(start,range.second);
    pending_=qMakePair(start,std::min(start+chunkMs,durationMs));return start;
}
qint64 CaptionCoverage::bufferedAhead(qint64 positionMs,qint64 durationMs)const{
    if(positionMs<0 || positionMs>=durationMs)return 0;
    auto end=positionMs;for(const auto &range:completed_){if(range.first>end)break;if(range.second>end)end=range.second;}
    return std::max<qint64>(0,std::min(end,durationMs)-positionMs);
}
bool CaptionCoverage::readyForPlayback(qint64 positionMs,qint64 durationMs)const{
    return positionMs>=0 && durationMs>positionMs && bufferedAhead(positionMs,durationMs)>=std::min(lookAheadMs,durationMs-positionMs);
}
void CaptionCoverage::complete(){
    if(pending_){completed_.append(*pending_);std::sort(completed_.begin(),completed_.end());QVector<QPair<qint64,qint64>> merged;for(const auto &range:completed_){if(!merged.isEmpty() && merged.last().second>=range.first)merged.last().second=std::max(merged.last().second,range.second);else merged.append(range);}completed_=std::move(merged);}pending_.reset();
}
void CaptionCoverage::cancel(){pending_.reset();}
void CaptionCoverage::reset(){completed_.clear();pending_.reset();}
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
