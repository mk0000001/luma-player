#include "lyric_captions.h"
#include <QRegularExpression>
#include <QTextBoundaryFinder>
#include <algorithm>

namespace luma {
namespace {
using namespace Qt::StringLiterals;

const QRegularExpression &sakuraKimiWatashi(){
    static const QRegularExpression pattern(
        uR"((?:桜|櫻|さくら|サクラ)[\t 、，,・]*(?:君|きみ|キミ)[\t 、，,・]*(?:私(?!たち|達)|わたし(?!たち)|ワタシ(?!タチ)))"_s);
    return pattern;
}

bool onlyPunctuationOrSpace(QStringView text){
    return std::all_of(text.begin(),text.end(),[](QChar ch){return ch.isPunct() || ch.isSpace();});
}

SubtitleCue lyricSlice(const SubtitleCue &cue,qsizetype first,qsizetype past){
    const auto &tokens=cue.tokens;
    auto begin=tokens[first].textStart;
    auto end=tokens[past-1].textStart+tokens[past-1].textLength;
    while(begin<end && cue.text.at(begin).isSpace())++begin;
    while(end>begin && cue.text.at(end-1).isSpace())--end;
    SubtitleCue result{0,0,cue.text.mid(begin,end-begin)};
    for(auto index=first;index<past;++index){
        const auto &token=tokens[index];
        const auto left=std::max(begin,token.textStart);
        const auto right=std::min(end,token.textStart+token.textLength);
        if(left<right)result.tokens.append({left-begin,right-left,token.startMs,token.endMs});
    }
    if(!result.tokens.isEmpty()){
        result.startMs=result.tokens.first().startMs;
        result.endMs=result.tokens.last().endMs;
    }
    return result;
}

void appendTimedPhrases(QVector<SubtitleCue> &result,const SubtitleCue &cue,const QVector<bool> &boundary,
                        qsizetype first,qsizetype past){
    if(first>=past)return;
    constexpr qint64 shortestPhraseMs=900,preferredPhraseMs=1800,longPhraseMs=4600,pauseMs=450;
    const auto append=[&](qsizetype end){
        auto phrase=lyricSlice(cue,first,end);
        if(!phrase.text.isEmpty())result.append(std::move(phrase));
        first=end;
    };
    qsizetype preferred=-1;
    for(auto index=first+1;index<past;++index){
        if(!boundary[index])continue;
        const auto &next=cue.tokens[index];
        const auto &previous=cue.tokens[index-1];
        const auto elapsed=previous.endMs-cue.tokens[first].startMs;
        if(elapsed<shortestPhraseMs)continue;
        if(cue.tokens[past-1].endMs-next.startMs<shortestPhraseMs)break;
        const auto left=QStringView(cue.text).first(next.textStart);
        const auto trimmed=left.trimmed();
        const auto right=QStringView(cue.text).sliced(next.textStart).trimmed();
        if(trimmed.isEmpty() || right.isEmpty() || (right.front().isPunct() && right.front().category()!=QChar::Punctuation_Open))continue;
        const auto last=trimmed.back();
        const bool punctuation=QStringView(u"。！？!?、，,；;").contains(last) || left.endsWith(u'\n');
        const bool silence=next.startMs-previous.endMs>=pauseMs;
        if(elapsed<=longPhraseMs && elapsed>=preferredPhraseMs &&
           (left.back().isSpace() || cue.text.at(next.textStart).isSpace() ||
            (last.script()==QChar::Script_Hiragana &&
             (right.front().script()==QChar::Script_Han || right.front().script()==QChar::Script_Katakana))))
            preferred=index;
        if(punctuation || silence || next.endMs-cue.tokens[first].startMs>longPhraseMs){
            const auto cut=punctuation || silence || preferred<0?index:preferred;
            append(cut);
            index=cut;
            preferred=-1;
        }
    }
    append(past);
}
}

bool isSakuraKimiWatashi(QStringView text){
    const auto match=sakuraKimiWatashi().matchView(text);
    return match.hasMatch() && onlyPunctuationOrSpace(text.first(match.capturedStart())) &&
           onlyPunctuationOrSpace(text.sliced(match.capturedEnd()));
}

QVector<SubtitleCue> splitTimedLyrics(const QVector<SubtitleCue> &cues,bool tukiBudokan){
    QVector<SubtitleCue> result;
    result.reserve(cues.size());
    for(const auto &cue:cues){
        if(!validSubtitleTokenTimings(cue)){
            result.append(cue);
            continue;
        }
        const auto &tokens=cue.tokens;
        const QStringView text(cue.text);
        QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme,cue.text);
        QVector<bool> boundary(tokens.size()+1,true);
        for(qsizetype index=1;index<tokens.size();++index){
            graphemes.setPosition(tokens[index].textStart);
            boundary[index]=graphemes.isAtBoundary();
        }

        qsizetype remaining=0;
        if(tukiBudokan){
            auto matches=sakuraKimiWatashi().globalMatchView(text);
            while(matches.hasNext()){
                const auto match=matches.next();
                const auto firstToken=std::partition_point(tokens.cbegin()+remaining,tokens.cend(),
                    [&](const auto &token){return token.textStart+token.textLength<=match.capturedStart();});
                const auto pastToken=std::partition_point(firstToken,tokens.cend(),
                    [&](const auto &token){return token.textStart<match.capturedEnd();});
                if(firstToken==pastToken)continue;
                const qsizetype first=firstToken-tokens.cbegin(),past=pastToken-tokens.cbegin();
                const auto end=tokens[past-1].textStart+tokens[past-1].textLength;
                // Splitting a token that straddles two spoken words would
                // assign a timestamp the recognizer did not provide.
                if(!boundary[first] || !boundary[past] ||
                   !onlyPunctuationOrSpace(text.sliced(firstToken->textStart,match.capturedStart()-firstToken->textStart)) ||
                   !onlyPunctuationOrSpace(text.sliced(match.capturedEnd(),end-match.capturedEnd())))continue;
                appendTimedPhrases(result,cue,boundary,remaining,first);
                result.append(lyricSlice(cue,first,past));
                remaining=past;
            }
        }
        appendTimedPhrases(result,cue,boundary,remaining,tokens.size());
    }
    return result;
}
}
