#pragma once
#include <cstdint>
#include <istream>
#include <string>

namespace luma::asr {
enum class Quality { Auto, Fast, Accurate };
enum class DtwPreset { None, LargeV3, LargeV3Turbo };
struct ModelArchitecture {
    int32_t vocabulary=0, audioContext=0, audioState=0, audioHeads=0, audioLayers=0;
    int32_t textContext=0, textState=0, textHeads=0, textLayers=0, melBins=0, fileType=0;
};
struct DecodeOptions {
    bool beamSearch=false, flashAttention=true;
    int beamSize=1, bestOf=1, maxLength=48;
    DtwPreset dtw=DtwPreset::None;
};
struct DeviceOptions {
    std::string requested, device;
    bool cpu=false, explicitOpenVino=false, npu=false, explicitNative=false, useGpu=false;
};
Quality parseQuality(const std::string &value);
ModelArchitecture readModelArchitecture(std::istream &input);
DecodeOptions selectDecodeOptions(Quality quality,const ModelArchitecture &model);
DeviceOptions parseDevice(const std::string &requested);
}
