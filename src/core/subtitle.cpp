#include "subtitle.h"
#include <QRegularExpression>
#include <QStringView>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <utility>
namespace luma {
namespace {
bool validTokenTimings(const SubtitleCue &cue){
    if(cue.tokens.isEmpty())return false;
    qsizetype next=0;qint64 previousEnd=cue.startMs;
    for(const auto &token:cue.tokens){
        if(token.textStart!=next || token.textLength<=0 || token.textLength>cue.text.size()-next || token.startMs<previousEnd || token.endMs<=token.startMs || token.endMs>cue.endMs)return false;
        next+=token.textLength;previousEnd=token.endMs;
        if(next<cue.text.size() && cue.text[next].isLowSurrogate())return false;
    }
    return next==cue.text.size();
}

int graphemeUnits(QStringView text){
    for(qsizetype index=0;index<text.size();++index){
        const auto first=text[index];
        char32_t point=first.unicode();
        if(first.isHighSurrogate() && index+1<text.size() && text[index+1].isLowSurrogate()){
            point=QChar::surrogateToUcs4(first,text[index+1]);
            ++index;
        }
        if((point>=0x1100 && point<=0x11ff) || (point>=0x2e80 && point<=0xa4cf) ||
           (point>=0xac00 && point<=0xd7af) || (point>=0xf900 && point<=0xfaff) ||
           (point>=0xff01 && point<=0xff60) || (point>=0x1f000 && point<=0x1ffff) ||
           (point>=0x20000 && point<=0x3ffff))return 2;
    }
    return 1;
}
}
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
        const bool timed=validTokenTimings(cue);
        QVector<Grapheme> characters;QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme,cue.text);qsizetype start=0,end=0;
        while((end=finder.toNextBoundary())>=0){
            static const QString sentenceEnds=QStringLiteral("。！？!?;；.\n");
            const auto text=QStringView(cue.text).sliced(start,end-start);
            const auto last=text.back();characters.append({start,end,graphemeUnits(text),last.isSpace(),sentenceEnds.contains(last)});start=end;
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
            // Keep sentence endings/closing quotes with the preceding page.
            // The small allowance cannot turn a punctuation-only input into
            // one unbounded presentation page.
            static const QString closing=QString::fromUtf8("。，、！？!?;；:：,.)]}」』】〉》〕”’»…");
            for(int extra=0;last<characters.size() && extra<4 && closing.contains(cue.text.at(characters[last].start));++extra)++last;
            while(timed && last<characters.size()){
                const auto boundary=characters[last-1].end;
                bool expanded=false;
                for(const auto &token:cue.tokens)if(token.textStart<boundary && token.textStart+token.textLength>boundary){
                    const auto tokenEnd=token.textStart+token.textLength;
                    while(last<characters.size() && characters[last-1].end<tokenEnd)++last;
                    expanded=true;
                    break;
                }
                if(!expanded)break;
            }
            ranges.append({first,last});first=last;
        }
        QVector<qint64> cumulative{0};for(const auto &character:characters)cumulative.append(cumulative.last()+character.units);
        const auto timeAt=[&](qsizetype index){return index==characters.size()?cue.endMs:cue.startMs+qint64((static_cast<long double>(cue.endMs-cue.startMs)*cumulative[index])/cumulative.last());};
        const auto startAt=[&](qsizetype index){
            if(timed){const auto boundary=characters[index].start;for(const auto &token:cue.tokens)if(token.textStart+token.textLength>boundary)return token.startMs;}
            return timeAt(index);
        };
        const auto endAt=[&](qsizetype index){
            if(timed){const auto boundary=characters[index-1].end;for(auto token=cue.tokens.crbegin();token!=cue.tokens.crend();++token)if(token->textStart<boundary)return token->endMs;}
            return timeAt(index);
        };
        // A short tail is merged with its neighbor rather than becoming a flash.
        for(qsizetype index=0;index<ranges.size() && ranges.size()>1;){
            if(endAt(ranges[index].second)-startAt(ranges[index].first)>=minPageMs){++index;continue;}
            if(index>0){ranges[index-1].second=ranges[index].second;ranges.removeAt(index);--index;}
            else{ranges[1].first=ranges[0].first;ranges.removeFirst();}
        }
        for(const auto &range:ranges){
            auto begin=characters[range.first].start,finish=characters[range.second-1].end;
            while(begin<finish && cue.text[begin].isSpace())++begin;while(finish>begin && cue.text[finish-1].isSpace())--finish;
            if(begin==finish)continue;
            SubtitleCue page{startAt(range.first),endAt(range.second),cue.text.mid(begin,finish-begin)};
            if(timed)for(const auto &token:cue.tokens){
                const auto tokenStart=std::max(begin,token.textStart),tokenEnd=std::min(finish,token.textStart+token.textLength);
                if(tokenEnd>tokenStart)page.tokens.append({tokenStart-begin,tokenEnd-tokenStart,token.startMs,token.endMs});
            }
            result.append(std::move(page));
        }
    }
    return result;
}
quint64 SubtitleTimeline::reset() { cues_.clear(); maximumEnds_.clear(); return ++generation_; }
bool SubtitleTimeline::attach(quint64 generation, QVector<SubtitleCue> cues) {
    if (generation!=generation_) return false;
    const auto byStart=[](const SubtitleCue &a,const SubtitleCue &b){return a.startMs<b.startMs;};
    if(!std::is_sorted(cues.cbegin(),cues.cend(),byStart))std::stable_sort(cues.begin(),cues.end(),byStart);
    cues_=std::move(cues);
    maximumEnds_.clear();
    maximumEnds_.reserve(cues_.size());
    for(const auto &cue:std::as_const(cues_))
        maximumEnds_.append(maximumEnds_.isEmpty()?cue.endMs:std::max(maximumEnds_.back(),cue.endMs));
    return true;
}
QString SubtitleTimeline::at(qint64 positionMs) const {
    QStringList active;
    const auto first=std::upper_bound(maximumEnds_.cbegin(),maximumEnds_.cend(),positionMs)-maximumEnds_.cbegin();
    for(auto index=first;index<cues_.size() && cues_[index].startMs<=positionMs;++index){
        if(positionMs<cues_[index].endMs)active.append(cues_[index].text);
    }
    return active.join('\n');
}
}
