#include "asr_timing.h"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace luma::asr;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){try{action();}catch(const std::runtime_error &){return;}throw std::runtime_error("invalid input accepted");}
RawSegmentTiming nonuniform(){return {0,600,u8"あいう",{{u8"あ",0,100,100},{u8"い",100,200,400},{u8"う",200,300,450}}};}
const SegmentTiming &single(const TimingDocument &document){require(document.segments.size()==1,"expected one timing segment");return document.segments[0];}
void token(const TokenTiming &t,size_t first,size_t last,int64_t start,int64_t end){require(t.byteStart==first && t.byteEnd==last && t.startMs==start && t.endMs==end,"wrong byte span or timing");}
}
int main(){
    int failures=0;const auto run=[&](const char *name,auto test){try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception &e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    run("DTW uses nonuniform token points instead of coarse or proportional times",[]{
        const auto d=buildTimingDocument({nonuniform()},6000,Alignment::Dtw);const auto &s=single(d);
        require(d.audioMs==6000 && d.alignment==Alignment::Dtw && s.startMs==1000 && s.endMs==6000 && s.text==u8"あいう","wrong DTW segment bounds");
        require(s.tokens.size()==3,"DTW points not exported");token(s.tokens[0],0,3,1000,4000);token(s.tokens[1],3,6,4000,4500);token(s.tokens[2],6,9,4500,6000);
    });
    run("split UTF8 tokens join into complete trimmed byte spans",[]{
        const RawSegmentTiming raw={0,600,std::string(" \t")+u8"あい"+" \r\n",{{" \t",0,20,0},{"\xe3",20,30,100},{"\x81\x82",30,40,100},{u8"い",40,50,400},{" \r\n",50,60,500}}};
        const auto d=buildTimingDocument({raw},6000,Alignment::Dtw);const auto &s=single(d);
        require(s.text==u8"あい" && s.tokens.size()==2,"UTF8 fragments were dropped or not joined");token(s.tokens[0],0,3,1000,4000);token(s.tokens[1],3,6,4000,6000);
    });
    run("Unicode whitespace trimming matches the Qt consumer byte contract",[]{
        const RawSegmentTiming raw={0,600,u8"\u3000あい\u00a0",{{u8"\u3000",0,20,0},{u8"あ",20,30,100},{u8"い",30,40,400},{u8"\u00a0",40,50,500}}};
        const auto d=buildTimingDocument({raw},6000,Alignment::Dtw);const auto &s=single(d);
        require(s.text==u8"あい" && s.tokens.size()==2,"Unicode trim changed text or offsets");token(s.tokens[0],0,3,1000,4000);token(s.tokens[1],3,6,4000,6000);
        require(trimSegmentText("\v\fword\r\n")=="word","Unicode whitespace control trim differs");
    });
    run("same-point tokens merge without zero duration or lost combining bytes",[]{
        const RawSegmentTiming raw={0,600,u8"か\u3099う",{{u8"か",0,100,100},{u8"\u3099",100,100,100},{u8"う",100,200,450}}};
        const auto d=buildTimingDocument({raw},6000,Alignment::Dtw);const auto &s=single(d);
        require(s.tokens.size()==2 && s.text==u8"か\u3099う","same-point combining tokens did not merge");token(s.tokens[0],0,6,1000,4500);token(s.tokens[1],6,9,4500,6000);
    });
    run("ordinary token mode uses actual token intervals and preserves pauses",[]{
        const RawSegmentTiming raw={0,600," one two ",{{" one",100,200,500},{" two ",400,450,500}}};
        const auto d=buildTimingDocument({raw},6000,Alignment::Token);const auto &s=single(d);
        require(s.text=="one two" && s.tokens.size()==2,"trimmed regular tokens missing");token(s.tokens[0],0,3,1000,2000);token(s.tokens[1],3,7,4000,4500);
    });
    run("invalid or reversed DTW anchors safely omit token metadata",[]{
        for(int64_t invalid:{int64_t(-1),int64_t(700),std::numeric_limits<int64_t>::max()}){
            auto raw=nonuniform();raw.tokens[1].dtwTicks=invalid;const auto d=buildTimingDocument({raw},6000,Alignment::Dtw);const auto &s=single(d);
            require(s.tokens.empty() && s.startMs==0 && s.endMs==6000 && s.text==u8"あいう","invalid DTW timing was fabricated");
        }
        auto raw=nonuniform();raw.tokens[1].dtwTicks=99;require(single(buildTimingDocument({raw},6000,Alignment::Dtw)).tokens.empty(),"backward DTW accepted");
        raw=nonuniform();raw.tokens[2].dtwTicks=600;require(single(buildTimingDocument({raw},6000,Alignment::Dtw)).tokens.empty(),"end-point token given invented duration");
    });
    run("invalid regular intervals cannot fabricate token timing",[]{
        auto raw=nonuniform();raw.tokens[1].startTicks=-1;require(single(buildTimingDocument({raw},6000,Alignment::Token)).tokens.empty(),"negative interval accepted");
        raw=nonuniform();raw.tokens[1].startTicks=50;require(single(buildTimingDocument({raw},6000,Alignment::Token)).tokens.empty(),"overlapping interval accepted");
        raw=nonuniform();raw.tokens[2].endTicks=raw.tokens[2].startTicks;require(single(buildTimingDocument({raw},6000,Alignment::Token)).tokens.empty(),"zero duration accepted");
    });
    run("text reconstruction mismatch preserves coarse cue with empty tokens",[]{
        auto raw=nonuniform();raw.tokens[1].text="unexpected";const auto d=buildTimingDocument({raw},6000,Alignment::Dtw);const auto &s=single(d);
        require(s.text==u8"あいう" && s.tokens.empty() && s.startMs==0,"mismatched byte offsets exported");
    });
    run("coarse cue bounds clamp to audio and cannot move backward",[]{
        const std::vector<RawSegmentTiming> raw={{-99,100,"one",{{"one",0,100,0}}},{90,99999,"two",{{"two",90,200,90}}}};
        const auto d=buildTimingDocument(raw,3000,Alignment::Dtw);require(d.segments.size()==2,"valid bounded cues missing");
        require(d.segments[0].startMs==0 && d.segments[0].endMs==1000,"first audio bound wrong");
        require(d.segments[1].startMs==1000 && d.segments[1].endMs==3000 && d.segments[1].tokens.empty(),"overlapping anchor retimed or audio limit ignored");
    });
    run("empty segments are omitted without emitting negative or zero cues",[]{
        const auto d=buildTimingDocument({{0,100," \r\n\t",{}},{100,100,"zero",{}},{500,900,"outside",{}}},3000,Alignment::Token);
        require(d.segments.empty(),"empty or invalid duration cue emitted");require(trimSegmentText(" \tword \r\n")=="word","SRT trim differs from sidecar");
    });
    run("invalid UTF8 and oversized documents are rejected before JSON output",[]{
        rejects([]{buildTimingDocument({{0,100,"\xc0\x80",{}}},1000,Alignment::Token);});
        rejects([]{buildTimingDocument({{0,100,"\xed\xa0\x80",{}}},1000,Alignment::Token);});
        rejects([]{buildTimingDocument({{0,100,"\xf4\x90\x80\x80",{}}},1000,Alignment::Token);});
        rejects([]{buildTimingDocument({{0,100,std::string(1024*1024+1,'x'),{}}},1000,Alignment::Token);});
        rejects([]{buildTimingDocument({},0,Alignment::Token);});rejects([]{buildTimingDocument({},60001,Alignment::Token);});
    });
    run("JSON contains the versioned contract and escaped exact text",[]{
        const auto d=buildTimingDocument({{0,100,"A\"\\\nB",{{"A\"\\\nB",0,100,-1}}}},1000,Alignment::Token);std::ostringstream out;writeTimingJson(out,d);
        const std::string expected="{\"schema\":1,\"format\":\"luma-asr-timing-v1\",\"alignment\":\"token\",\"audioMs\":1000,\"segments\":[{\"startMs\":0,\"endMs\":1000,\"text\":\"A\\\"\\\\\\u000aB\",\"tokens\":[{\"byteStart\":0,\"byteEnd\":5,\"startMs\":0,\"endMs\":1000}]}]}\n";
        require(out.str()==expected,"serialized timing contract or JSON escaping changed");
    });
    return failures?1:0;
}
