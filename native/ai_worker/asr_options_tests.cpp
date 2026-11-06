#include "asr_options.h"
#include <array>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace luma::asr;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action){try{action();}catch(const std::runtime_error &){return;}throw std::runtime_error("invalid input accepted");}
// The fixture is the 48-byte little-endian GGML header; it contains no weights.
std::string header(int decoderLayers=32){
    const std::array<uint32_t,12> values={0x67676d6c,51866,1500,1280,20,32,448,1280,20,uint32_t(decoderLayers),128,2009};
    std::string bytes;for(const auto value:values)for(int i=0;i<4;++i)bytes.push_back(char(value>>(i*8)));return bytes;
}
ModelArchitecture model(int layers){std::istringstream input(header(layers));return readModelArchitecture(input);}
}
int main(){
    int failures=0;const auto run=[&](const char *name,auto test){try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception &e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    run("GGML architecture inspection reads only the bounded header",[]{
        std::istringstream input(header()+"unread tensor data");const auto m=readModelArchitecture(input);
        require(input.tellg()==std::streampos(48),"reader consumed tensor data or no header");
        require(m.vocabulary==51866 && m.audioLayers==32 && m.textLayers==32 && m.melBins==128 && m.audioState==1280,"wrong architecture fields");
    });
    run("truncated, wrong-magic and unreasonable GGML headers are rejected",[]{
        rejects([]{std::istringstream input(header().substr(0,47));readModelArchitecture(input);});
        rejects([]{auto bytes=header();bytes[0]='!';std::istringstream input(bytes);readModelArchitecture(input);});
        rejects([]{auto bytes=header();bytes[20]=char(0xff);bytes[21]=char(0xff);bytes[22]=char(0xff);bytes[23]=char(0x7f);std::istringstream input(bytes);readModelArchitecture(input);});
        rejects([]{auto bytes=header();bytes[16]=0;std::istringstream input(bytes);readModelArchitecture(input);});
    });
    run("auto full-v3 uses beam5 and its own nonflash DTW heads",[]{
        const auto o=selectDecodeOptions(Quality::Auto,model(32));
        require(o.beamSearch && o.beamSize==5 && o.bestOf==5 && o.maxLength==0,"full-v3 did not choose accurate decoding");
        require(o.dtw==DtwPreset::LargeV3 && !o.flashAttention,"wrong full-v3 alignment context");
    });
    run("auto Kotoba and turbo preserve existing fast decoding",[]{
        for(int layers:{2,4}){const auto o=selectDecodeOptions(Quality::Auto,model(layers));require(!o.beamSearch && o.bestOf==1 && o.maxLength==48 && o.flashAttention && o.dtw==DtwPreset::None,"automatic distilled path changed");}
    });
    run("explicit accurate turbo chooses turbo heads and Kotoba never borrows them",[]{
        const auto turbo=selectDecodeOptions(Quality::Accurate,model(4));
        require(turbo.beamSearch && turbo.dtw==DtwPreset::LargeV3Turbo && !turbo.flashAttention && turbo.maxLength==0,"wrong turbo accurate selection");
        const auto kotoba=selectDecodeOptions(Quality::Accurate,model(2));
        require(kotoba.beamSearch && kotoba.beamSize==5 && kotoba.dtw==DtwPreset::None && kotoba.flashAttention,"invalid heads selected for Kotoba");
    });
    run("near-matching architecture cannot activate an official DTW preset",[]{
        auto m=model(32);m.audioState=1024;m.textState=1024;m.audioHeads=16;m.textHeads=16;
        require(!selectDecodeOptions(Quality::Auto,m).beamSearch,"near-match enabled automatic accurate mode");
        require(selectDecodeOptions(Quality::Accurate,m).dtw==DtwPreset::None,"near-match enabled incompatible DTW heads");
        m=model(32);m.melBins=80;require(selectDecodeOptions(Quality::Accurate,m).dtw==DtwPreset::None,"old large model accepted v3 heads");
    });
    run("fast overrides full-v3 automatic accurate mode",[]{const auto o=selectDecodeOptions(Quality::Fast,model(32));require(!o.beamSearch && o.dtw==DtwPreset::None && o.flashAttention && o.maxLength==48,"explicit fast ignored");});
    run("quality values are exact and unknown values fail before inference",[]{require(parseQuality("fast")==Quality::Fast && parseQuality("accurate")==Quality::Accurate && parseQuality("auto")==Quality::Auto,"quality values not parsed");rejects([]{parseQuality("quality");});});
    run("CPU requests never require GPU or OpenVINO encoder inference",[]{const auto o=parseDevice("CPU");require(o.cpu && o.device=="CPU" && !o.useGpu && !o.npu && !o.explicitOpenVino,"CPU request routed to acceleration");});
    run("explicit accelerator requests preserve their backend identity",[]{
        const auto npu=parseDevice("OpenVINO:NPU.0");require(npu.explicitOpenVino && npu.npu && !npu.useGpu && npu.device=="NPU.0","NPU request changed");
        const auto gpu=parseDevice("CUDA0");require(gpu.explicitNative && gpu.useGpu && !gpu.cpu && gpu.device=="CUDA0","native GPU request changed");
        rejects([]{parseDevice("AUTO");});rejects([]{parseDevice("OpenVINO:CPU");});
    });
    return failures?1:0;
}
