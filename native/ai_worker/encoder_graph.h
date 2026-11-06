#pragma once

#include <openvino/openvino.hpp>
#include <map>
#include <string>
#include <vector>

namespace luma::encoder {

struct Dimensions {
    size_t mels;
    size_t context;
    size_t state;
    size_t heads;
    size_t layers;
};

struct TensorData {
    ov::Shape shape;
    std::vector<float> values;
};

class TensorSource {
public:
    virtual ~TensorSource() = default;
    virtual TensorData read(const std::string &name, const ov::Shape &expected) const = 0;
};

// An in-memory source is also useful to callers supplying converted tensors.
// The Whisper source reads one tensor at a time instead, bounding export memory.
class TensorSet final : public TensorSource {
public:
    void add(std::string name, ov::Shape shape, std::vector<float> values);
    TensorData read(const std::string &name, const ov::Shape &expected) const override;
private:
    std::map<std::string, TensorData> tensors_;
};

ov::Output<ov::Node> layerNorm(const ov::Output<ov::Node> &input,
                              const std::vector<float> &scale,
                              const std::vector<float> &bias);
ov::Output<ov::Node> buildStem(const ov::Output<ov::Node> &mel,
                              const TensorSource &source, const Dimensions &dims);
ov::Output<ov::Node> buildBlock(const ov::Output<ov::Node> &input,
                               const TensorSource &source, const Dimensions &dims,
                               size_t layer);
std::shared_ptr<ov::Model> buildGraph(const TensorSource &source, const Dimensions &dims);

} // namespace luma::encoder
