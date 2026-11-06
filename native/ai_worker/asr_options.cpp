#include "asr_options.h"
#include <array>
#include <algorithm>
#include <stdexcept>

namespace luma::asr {
namespace {
bool indexedDevice(const std::string &value,const std::string &base){
    return value==base || (value.size()>base.size()+1 && value.compare(0,base.size()+1,base+".")==0 &&
        std::all_of(value.begin()+base.size()+1,value.end(),[](unsigned char c){return c>='0' && c<='9';}));
}
bool largeV3Shape(const ModelArchitecture &m){
    return m.vocabulary==51866 && m.audioContext==1500 && m.audioState==1280 && m.audioHeads==20 && m.audioLayers==32 &&
        m.textContext==448 && m.textState==1280 && m.textHeads==20 && m.melBins==128;
}
}
Quality parseQuality(const std::string &value){
    if(value=="auto")return Quality::Auto;
    if(value=="fast")return Quality::Fast;
    if(value=="accurate")return Quality::Accurate;
    throw std::runtime_error("Expected --quality auto, fast or accurate");
}
ModelArchitecture readModelArchitecture(std::istream &input){
    // Pinned whisper.cpp GGML format: magic followed by eleven int32 hparams.
    // Do not inspect tensors or allocate using dimensions from an untrusted file.
    std::array<unsigned char,48> bytes{};
    if(!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Truncated Whisper GGML header");
    const auto word=[&](size_t i){const auto p=bytes.data()+i*4;return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;};
    if(word(0)!=0x67676d6c)throw std::runtime_error("Invalid Whisper GGML magic");
    const auto field=[&](size_t i,uint32_t maximum){const auto value=word(i);if(value==0 || value>maximum)throw std::runtime_error("Unreasonable Whisper GGML architecture");return int32_t(value);};
    ModelArchitecture m;
    m.vocabulary=field(1,1000000);m.audioContext=field(2,16384);m.audioState=field(3,4096);m.audioHeads=field(4,128);m.audioLayers=field(5,128);
    m.textContext=field(6,16384);m.textState=field(7,4096);m.textHeads=field(8,128);m.textLayers=field(9,128);m.melBins=field(10,256);
    if(word(11)>100000)throw std::runtime_error("Unreasonable Whisper GGML file type");m.fileType=int32_t(word(11));
    if(m.audioState!=m.textState || m.audioState%m.audioHeads!=0 || m.textState%m.textHeads!=0 || (m.melBins!=80 && m.melBins!=128))
        throw std::runtime_error("Inconsistent Whisper GGML architecture");
    return m;
}
DecodeOptions selectDecodeOptions(Quality quality,const ModelArchitecture &model){
    DecodeOptions options;
    const bool full=largeV3Shape(model) && model.textLayers==32;
    const bool turbo=largeV3Shape(model) && model.textLayers==4;
    if(quality==Quality::Fast || (quality==Quality::Auto && !full))return options;
    options.beamSearch=true;options.beamSize=5;options.bestOf=5;options.maxLength=0;
    if(full)options.dtw=DtwPreset::LargeV3;
    else if(turbo)options.dtw=DtwPreset::LargeV3Turbo;
    // Capturing cross-attention heads requires the non-flash decoder graph.
    options.flashAttention=options.dtw==DtwPreset::None;
    return options;
}
DeviceOptions parseDevice(const std::string &requested){
    DeviceOptions options;options.requested=requested;
    options.cpu=requested=="CPU";options.explicitOpenVino=requested.rfind("OpenVINO:",0)==0;
    options.device=options.explicitOpenVino?requested.substr(9):requested;
    options.npu=indexedDevice(options.device,"NPU");
    options.explicitNative=!options.explicitOpenVino && (options.device.rfind("Vulkan",0)==0 || options.device.rfind("CUDA",0)==0);
    if(!options.cpu && !options.npu && !indexedDevice(options.device,"GPU") && !options.explicitNative)
        throw std::runtime_error("Explicit CPU, NPU or GPU device required");
    options.useGpu=!options.cpu && !options.npu && !options.explicitOpenVino;
    return options;
}
}
