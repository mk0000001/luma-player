#include "recognition_timing.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringDecoder>
#include <cmath>
#include <limits>
namespace luma {
namespace {
bool integer(const QJsonValue &value,qint64 maximum,qint64 &result){
    if(!value.isDouble())return false;
    const auto number=value.toDouble();
    if(!std::isfinite(number) || number<0 || number>double(maximum) || std::floor(number)!=number)return false;
    result=static_cast<qint64>(number);return true;
}
std::optional<QVector<qsizetype>> utf8Boundaries(const QString &text){
    QVector<qsizetype> result(text.toUtf8().size()+1,-1);qsizetype byte=0;
    for(qsizetype index=0;index<text.size();){
        result[byte]=index;const auto first=text[index];char32_t point=first.unicode();qsizetype units=1;
        if(first.isHighSurrogate()){
            if(index+1>=text.size() || !text[index+1].isLowSurrogate())return {};
            point=QChar::surrogateToUcs4(first,text[index+1]);units=2;
        }else if(first.isLowSurrogate())return {};
        if(point==0 || point==0xfffd || (point<32 && point!='\n' && point!='\r' && point!='\t'))return {};
        byte+=point<=0x7f?1:point<=0x7ff?2:point<=0xffff?3:4;index+=units;
    }
    result.last()=text.size();return result;
}
}
std::optional<RecognitionTiming> parseRecognitionTiming(const QByteArray &json,qint64 requestStartMs,qint64 requestDurationMs){
    if(json.isEmpty() || json.size()>1024*1024 || requestStartMs<0 || requestDurationMs<=0 || requestDurationMs>60000 || requestStartMs>std::numeric_limits<qint64>::max()-requestDurationMs)return {};
    QStringDecoder decoder(QStringDecoder::Utf8);const QString decoded=decoder(json);Q_UNUSED(decoded);if(decoder.hasError())return {};
    QJsonParseError error;const auto document=QJsonDocument::fromJson(json,&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject())return {};
    const auto root=document.object();qint64 schema=0,audioMs=0;
    if(!integer(root.value("schema"),1,schema) || schema!=1 || root.value("format").toString()!="luma-asr-timing-v1" || !integer(root.value("audioMs"),requestDurationMs,audioMs) || audioMs<=0 || !root.value("segments").isArray())return {};
    const auto method=root.value("alignment").toString();if(method!="dtw" && method!="token")return {};
    const auto segments=root.value("segments").toArray();if(segments.size()>1024)return {};
    RecognitionTiming result;qsizetype tokenCount=0,alignedCues=0;qint64 previousEnd=0;
    for(const auto &value:segments){
        if(!value.isObject())return {};const auto segment=value.toObject();qint64 start=0,end=0;
        if(!integer(segment.value("startMs"),audioMs,start) || !integer(segment.value("endMs"),audioMs,end) || start<previousEnd || end<=start || !segment.value("text").isString() || !segment.value("tokens").isArray())return {};
        const auto text=segment.value("text").toString();if(text.isEmpty() || text!=text.trimmed() || text.toUtf8().size()>16384)return {};
        const auto boundaries=utf8Boundaries(text);if(!boundaries)return {};
        SubtitleCue cue{requestStartMs+start,requestStartMs+end,text};
        const auto tokens=segment.value("tokens").toArray();tokenCount+=tokens.size();if(tokenCount>8192)return {};
        qint64 nextByte=0,lastEnd=start;
        for(const auto &tokenValue:tokens){
            if(!tokenValue.isObject())return {};const auto token=tokenValue.toObject();qint64 beginByte=0,endByte=0,tokenStart=0,tokenEnd=0;
            if(!integer(token.value("byteStart"),boundaries->size()-1,beginByte) || !integer(token.value("byteEnd"),boundaries->size()-1,endByte) || beginByte!=nextByte || endByte<=beginByte || boundaries->at(beginByte)<0 || boundaries->at(endByte)<0 || !integer(token.value("startMs"),end,tokenStart) || !integer(token.value("endMs"),end,tokenEnd) || tokenStart<lastEnd || tokenEnd<=tokenStart)return {};
            cue.tokens.append({boundaries->at(beginByte),boundaries->at(endByte)-boundaries->at(beginByte),requestStartMs+tokenStart,requestStartMs+tokenEnd});
            nextByte=endByte;lastEnd=tokenEnd;
        }
        if(!tokens.isEmpty()){
            if(nextByte!=boundaries->size()-1)return {};
            ++alignedCues;
        }
        result.cues.append(std::move(cue));previousEnd=end;
    }
    result.alignment=alignedCues==0?QString("segment"):alignedCues==result.cues.size()?method:method+"-partial";
    return result;
}
}
