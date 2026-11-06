#pragma once
#include <cstdint>
#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace luma::asr {
enum class Alignment { Token, Dtw };
struct RawTokenTiming {
    std::string text;
    int64_t startTicks=-1, endTicks=-1, dtwTicks=-1;
};
struct RawSegmentTiming {
    int64_t startTicks=0, endTicks=0;
    std::string text;
    std::vector<RawTokenTiming> tokens;
};
struct TokenTiming {
    size_t byteStart=0, byteEnd=0;
    int64_t startMs=0, endMs=0;
};
struct SegmentTiming {
    int64_t startMs=0, endMs=0;
    std::string text;
    std::vector<TokenTiming> tokens;
};
struct TimingDocument {
    Alignment alignment=Alignment::Token;
    int64_t audioMs=0;
    std::vector<SegmentTiming> segments;
};
std::string trimSegmentText(const std::string &text);
TimingDocument buildTimingDocument(const std::vector<RawSegmentTiming> &segments,int64_t audioMs,Alignment alignment);
void writeTimingJson(std::ostream &output,const TimingDocument &document);
}
