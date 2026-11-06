#include "asr_timing.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace luma::asr {
namespace {
constexpr size_t maxTextBytes=1024*1024, maxSegments=4096, maxTokens=16384;

std::vector<bool> utf8Boundaries(const std::string &text){
    std::vector<bool> boundaries(text.size()+1,false);boundaries[0]=true;
    for(size_t i=0;i<text.size();){
        const auto lead=static_cast<unsigned char>(text[i]);size_t width=0;
        if(lead<=0x7f)width=1;
        else if(lead>=0xc2 && lead<=0xdf)width=2;
        else if(lead>=0xe0 && lead<=0xef)width=3;
        else if(lead>=0xf0 && lead<=0xf4)width=4;
        if(!width || width>text.size()-i)throw std::runtime_error("Invalid UTF-8 segment text");
        for(size_t j=1;j<width;++j){const auto c=static_cast<unsigned char>(text[i+j]);if(c<0x80 || c>0xbf)throw std::runtime_error("Invalid UTF-8 segment text");}
        if(width>=3){
            const auto second=static_cast<unsigned char>(text[i+1]);
            if((lead==0xe0 && second<0xa0) || (lead==0xed && second>0x9f) || (lead==0xf0 && second<0x90) || (lead==0xf4 && second>0x8f))
                throw std::runtime_error("Invalid UTF-8 segment text");
        }
        i+=width;boundaries[i]=true;
    }
    return boundaries;
}
std::pair<size_t,size_t> trimmedRange(const std::string &text){
    const auto boundaries=utf8Boundaries(text);size_t first=text.size(),last=text.size();bool found=false;
    for(size_t i=0;i<text.size();){
        size_t end=i+1;while(!boundaries[end])++end;
        const auto lead=static_cast<unsigned char>(text[i]);
        uint32_t codepoint=lead;
        if(end-i>1){codepoint=lead & ((1u<<(7-(end-i)))-1);for(size_t j=i+1;j<end;++j)codepoint=codepoint<<6 | (static_cast<unsigned char>(text[j])&0x3f);}
        // Unicode White_Space, matching QString::trimmed() at the consumer.
        const bool space=codepoint==0x20 || (codepoint>=0x09 && codepoint<=0x0d) || codepoint==0x85 || codepoint==0xa0 || codepoint==0x1680 ||
            (codepoint>=0x2000 && codepoint<=0x200a) || codepoint==0x2028 || codepoint==0x2029 || codepoint==0x202f || codepoint==0x205f || codepoint==0x3000;
        if(!space){if(!found){first=i;found=true;}last=end;}
        i=end;
    }
    return {first,last};
}
int64_t boundedTime(int64_t ticks,int64_t audioMs){
    if(ticks<=0)return 0;
    if(ticks>audioMs/10)return audioMs;
    return ticks*10;
}
bool tokenTime(int64_t ticks,int64_t audioMs,int64_t &ms){
    if(ticks<0 || ticks>audioMs/10)return false;
    ms=ticks*10;return true;
}
std::vector<TokenTiming> alignedTokens(const RawSegmentTiming &raw,const SegmentTiming &segment,const std::vector<bool> &boundaries,
                                     size_t trimStart,size_t trimEnd,int64_t audioMs,Alignment alignment,int64_t previousEnd){
    std::string reconstructed;
    for(const auto &t:raw.tokens){if(t.text.size()>raw.text.size()-reconstructed.size())return {};reconstructed+=t.text;}
    if(reconstructed!=raw.text)return {};

    std::vector<TokenTiming> groups;TokenTiming pending;bool collecting=false;
    int64_t lastRawStart=-1;size_t offset=0;
    for(const auto &t:raw.tokens){
        const size_t rawStart=offset;offset+=t.text.size();
        if(offset<=trimStart || rawStart>=trimEnd || t.text.empty())continue;
        const size_t byteStart=std::max(rawStart,trimStart)-trimStart,byteEnd=std::min(offset,trimEnd)-trimStart;
        int64_t start=0,end=0;
        if(!tokenTime(alignment==Alignment::Dtw?t.dtwTicks:t.startTicks,audioMs,start) || start<lastRawStart)return {};
        lastRawStart=start;
        if(alignment==Alignment::Token && (!tokenTime(t.endTicks,audioMs,end) || end<start))return {};
        if(!collecting){pending={byteStart,byteEnd,start,end};collecting=true;}
        else {pending.byteEnd=byteEnd;pending.endMs=std::max(pending.endMs,end);}
        // Byte-pair tokens can end in the middle of one UTF-8 code point.
        if(!boundaries[byteEnd])continue;
        if(!groups.empty() && pending.startMs==groups.back().startMs){
            groups.back().byteEnd=pending.byteEnd;groups.back().endMs=std::max(groups.back().endMs,pending.endMs);
        }else groups.push_back(pending);
        collecting=false;
    }
    if(collecting || groups.empty() || groups.front().byteStart!=0 || groups.back().byteEnd!=segment.text.size())return {};
    for(size_t i=0;i<groups.size();++i){
        auto &t=groups[i];
        if(i && (t.byteStart!=groups[i-1].byteEnd || t.startMs<=groups[i-1].startMs))return {};
        if(alignment==Alignment::Dtw){
            // DTW supplies points, not measured vocal ends. Use the next point
            // and retain the bounded coarse end for the final token interval.
            t.endMs=i+1<groups.size()?groups[i+1].startMs:segment.endMs;
            if(t.startMs<previousEnd)return {};
        }else if(t.startMs<segment.startMs || (i && t.startMs<groups[i-1].endMs))return {};
        if(t.startMs>=t.endMs || t.endMs>segment.endMs)return {};
    }
    return groups;
}
void jsonText(std::ostream &output,const std::string &text){
    constexpr const char *hex="0123456789abcdef";output<<'"';
    for(const unsigned char c:text){
        if(c=='"' || c=='\\')output<<'\\'<<c;
        else if(c<32)output<<"\\u00"<<hex[c>>4]<<hex[c&15];
        else output<<c;
    }
    output<<'"';
}
}
std::string trimSegmentText(const std::string &text){
    const auto range=trimmedRange(text);return text.substr(range.first,range.second-range.first);
}
TimingDocument buildTimingDocument(const std::vector<RawSegmentTiming> &segments,int64_t audioMs,Alignment alignment){
    if(audioMs<=0 || audioMs>60000 || segments.size()>maxSegments)throw std::runtime_error("Unreasonable caption timing document bounds");
    TimingDocument document{alignment,audioMs,{}};int64_t previousEnd=0;size_t textBytes=0,tokenCount=0;
    for(const auto &raw:segments){
        if(raw.text.size()>maxTextBytes-textBytes || raw.tokens.size()>maxTokens-tokenCount)throw std::runtime_error("Caption timing document too large");
        textBytes+=raw.text.size();tokenCount+=raw.tokens.size();
        SegmentTiming segment{std::max(previousEnd,boundedTime(raw.startTicks,audioMs)),boundedTime(raw.endTicks,audioMs),trimSegmentText(raw.text),{}};
        if(segment.text.empty() || segment.startMs>=segment.endMs)continue;
        const auto boundaries=utf8Boundaries(segment.text);
        const auto range=trimmedRange(raw.text);const auto trimStart=range.first,trimEnd=range.second;
        segment.tokens=alignedTokens(raw,segment,boundaries,trimStart,trimEnd,audioMs,alignment,previousEnd);
        if(alignment==Alignment::Dtw && !segment.tokens.empty())segment.startMs=segment.tokens.front().startMs;
        previousEnd=segment.endMs;document.segments.push_back(std::move(segment));
    }
    return document;
}
void writeTimingJson(std::ostream &output,const TimingDocument &document){
    output<<"{\"schema\":1,\"format\":\"luma-asr-timing-v1\",\"alignment\":\""<<(document.alignment==Alignment::Dtw?"dtw":"token")
          <<"\",\"audioMs\":"<<document.audioMs<<",\"segments\":[";
    bool segmentComma=false;
    for(const auto &s:document.segments){
        if(segmentComma)output<<',';segmentComma=true;
        output<<"{\"startMs\":"<<s.startMs<<",\"endMs\":"<<s.endMs<<",\"text\":";jsonText(output,s.text);output<<",\"tokens\":[";
        bool tokenComma=false;
        for(const auto &t:s.tokens){
            if(tokenComma)output<<',';tokenComma=true;
            output<<"{\"byteStart\":"<<t.byteStart<<",\"byteEnd\":"<<t.byteEnd<<",\"startMs\":"<<t.startMs<<",\"endMs\":"<<t.endMs<<'}';
        }
        output<<"]}";
    }
    output<<"]}\n";
}
}
