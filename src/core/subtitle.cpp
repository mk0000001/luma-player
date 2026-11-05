#include "subtitle.h"
#include <QRegularExpression>
#include <QTextBoundaryFinder>
#include <algorithm>
namespace luma {
std::optional<qint64> CaptionCoverage::begin(qint64 positionMs,qint64 durationMs,qint64 prerollMs){
    if(pending_ || positionMs<0 || positionMs>=durationMs || bufferedAhead(positionMs,durationMs)>=std::min(prefetchMs,durationMs-positionMs))return {};
    const auto ahead=bufferedAhead(positionMs,durationMs);
    // A disc seek/pause can settle slightly before the requested timestamp.
    // Recognize that real audio too; contiguous prefetch never reads it twice.
    auto start=ahead>0?positionMs+ahead:std::max<qint64>(0,positionMs-std::clamp<qint64>(prerollMs,0,chunkMs-lookAheadMs));
    if(!ahead)for(const auto &range:completed_)if(range.second<=positionMs)start=std::max(start,range.second);
    auto end=std::min(start+chunkMs,durationMs);
    for(const auto &range:completed_)if(range.first>start){end=std::min(end,range.first);break;}
    pending_=qMakePair(start,end);return start;
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
QVector<SubtitleCue> captionPages(const QVector<SubtitleCue> &cues,int maxUnits,qint64 minPageMs){
    struct Grapheme {qsizetype start,end;int units;bool wordEnd,sentenceEnd;};
    QVector<SubtitleCue> result;maxUnits=std::max(1,maxUnits);minPageMs=std::max<qint64>(1,minPageMs);
    for(const auto &cue:cues){
        if(cue.endMs<=cue.startMs || cue.text.trimmed().isEmpty())continue;
        QVector<Grapheme> characters;QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme,cue.text);qsizetype start=0,end=0;
        while((end=finder.toNextBoundary())>=0){
            const auto text=cue.text.mid(start,end-start);int units=1;
            for(const auto point:text.toUcs4())if((point>=0x1100 && point<=0x11ff) || (point>=0x2e80 && point<=0xa4cf) || (point>=0xac00 && point<=0xd7af) || (point>=0xf900 && point<=0xfaff) || (point>=0xff01 && point<=0xff60) || (point>=0x1f000 && point<=0x1ffff) || (point>=0x20000 && point<=0x3ffff)){units=2;break;}
            const auto last=text.back();characters.append({start,end,units,last.isSpace(),QString::fromUtf8("。！？!?;；.\n").contains(last)});start=end;
        }
        if(characters.isEmpty())continue;
        QVector<QPair<qsizetype,qsizetype>> ranges;qsizetype first=0;
        const qint64 maxPages=std::max<qint64>(1,(cue.endMs-cue.startMs)/minPageMs);
        while(first<characters.size()){
            qsizetype last=first,lastWord=-1,lastSentence=-1;int used=0;
            while(last<characters.size() && (last==first || used+characters[last].units<=maxUnits)){
                used+=characters[last].units;
                if(used>=maxUnits/2){if(characters[last].wordEnd)lastWord=last+1;if(characters[last].sentenceEnd)lastSentence=last+1;}
                ++last;
            }
            if(qint64(ranges.size())+1>=maxPages)last=characters.size();
            else if(last<characters.size()){if(lastSentence>first)last=lastSentence;else if(lastWord>first)last=lastWord;}
            ranges.append({first,last});first=last;
        }
        QVector<qint64> cumulative{0};for(const auto &character:characters)cumulative.append(cumulative.last()+character.units);
        const auto timeAt=[&](qsizetype index){return index==characters.size()?cue.endMs:cue.startMs+qint64((static_cast<long double>(cue.endMs-cue.startMs)*cumulative[index])/cumulative.last());};
        // A short tail is merged with its neighbor rather than becoming a flash.
        for(qsizetype index=0;index<ranges.size() && ranges.size()>1;){
            if(timeAt(ranges[index].second)-timeAt(ranges[index].first)>=minPageMs){++index;continue;}
            if(index>0){ranges[index-1].second=ranges[index].second;ranges.removeAt(index);--index;}
            else{ranges[1].first=ranges[0].first;ranges.removeFirst();}
        }
        for(const auto &range:ranges){const auto begin=characters[range.first].start,finish=characters[range.second-1].end;result.append({timeAt(range.first),timeAt(range.second),cue.text.mid(begin,finish-begin).trimmed()});}
    }
    return result;
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
