#pragma once
#include "encoder_graph.h"
#include <whisper.h>
#include <filesystem>
#include <memory>

namespace luma::encoder {
class WhisperSource final : public TensorSource {
public:
    explicit WhisperSource(const std::filesystem::path &model,bool withState=false,bool flashAttention=false);
    TensorData read(const std::string &name,const ov::Shape &expected) const override;
    Dimensions dimensions() const;
    size_t tensorCount() const;
    whisper_context *context() const { return context_.get(); }
    std::vector<float> encode(const std::vector<float> &mel,int threads) const;
private:
    std::unique_ptr<whisper_context,decltype(&whisper_free)> context_;
};
}
