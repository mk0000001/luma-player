#include "encoder_graph.h"
#include <openvino/opsets/opset13.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace luma::encoder {
namespace op = ov::opset13;
namespace {
size_t elements(const ov::Shape &shape) {
    if (shape.empty()) throw std::runtime_error("empty tensor shape");
    size_t count=1;
    for (const auto extent:shape) {
        if (!extent || extent>std::numeric_limits<size_t>::max()/count)
            throw std::runtime_error("invalid tensor size");
        count*=extent;
    }
    return count;
}
void validate(const Dimensions &dims) {
    if (!dims.mels || !dims.context || !dims.state || !dims.heads || dims.state%dims.heads ||
        dims.mels>128 || dims.context>1500 || dims.state>1280 || dims.layers>32)
        throw std::runtime_error("invalid encoder dimensions");
}
ov::Output<ov::Node> constant(const TensorSource &source,const std::string &name,const ov::Shape &shape) {
    auto data=source.read(name,shape);
    if (data.shape!=shape || data.values.size()!=elements(shape))
        throw std::runtime_error("invalid tensor shape/size: "+name);
    auto result=op::Constant::create(ov::element::f32,shape,data.values);
    result->set_friendly_name(name);
    return result;
}
ov::Output<ov::Node> reshape(const ov::Output<ov::Node> &value,const ov::Shape &shape) {
    std::vector<int64_t> dims(shape.begin(),shape.end());
    return std::make_shared<op::Reshape>(value,op::Constant::create(ov::element::i64,{dims.size()},dims),false);
}
ov::Output<ov::Node> transpose(const ov::Output<ov::Node> &value,const std::vector<int64_t> &order) {
    return std::make_shared<op::Transpose>(value,op::Constant::create(ov::element::i64,{order.size()},order));
}
ov::Output<ov::Node> gelu(const ov::Output<ov::Node> &value) {
    return std::make_shared<op::Gelu>(value,ov::op::GeluApproximationMode::TANH);
}
ov::Output<ov::Node> norm(const ov::Output<ov::Node> &input,const TensorSource &source,
                          const std::string &prefix,size_t width) {
    auto scale=source.read(prefix+".weight",{width});
    auto bias=source.read(prefix+".bias",{width});
    return layerNorm(input,scale.values,bias.values);
}
ov::Output<ov::Node> linear(const ov::Output<ov::Node> &input,const TensorSource &source,
                            const std::string &prefix,size_t in,size_t out,bool bias=true) {
    ov::Output<ov::Node> result=std::make_shared<op::MatMul>(input,constant(source,prefix+".weight",{out,in}),false,true);
    if (bias) result=std::make_shared<op::Add>(result,constant(source,prefix+".bias",{out}));
    return result;
}
}

void TensorSet::add(std::string name, ov::Shape shape, std::vector<float> values) {
    if (tensors_.count(name)) throw std::runtime_error("duplicate tensor: "+name);
    if (values.size()!=elements(shape)) throw std::runtime_error("wrong tensor size: "+name);
    for (const auto value:values) if (!std::isfinite(value)) throw std::runtime_error("nonfinite tensor: "+name);
    tensors_.emplace(std::move(name),TensorData{std::move(shape),std::move(values)});
}
TensorData TensorSet::read(const std::string &name, const ov::Shape &expected) const {
    const auto found=tensors_.find(name);
    if (found==tensors_.end()) throw std::runtime_error("missing tensor: "+name);
    if (found->second.shape!=expected) throw std::runtime_error("wrong tensor shape: "+name);
    return found->second;
}
ov::Output<ov::Node> layerNorm(const ov::Output<ov::Node> &input,
                              const std::vector<float> &scale,const std::vector<float> &bias) {
    const auto shape=input.get_shape();
    if (shape.empty() || scale.size()!=shape.back() || bias.size()!=shape.back())
        throw std::runtime_error("wrong normalization parameter shape");
    const int64_t axis=static_cast<int64_t>(shape.size()-1);
    auto normalized=std::make_shared<op::MVN>(input,op::Constant::create(ov::element::i64,{1},{axis}),
                                             true,1e-5f,ov::op::MVNEpsMode::INSIDE_SQRT);
    auto scaled=std::make_shared<op::Multiply>(normalized,op::Constant::create(ov::element::f32,{scale.size()},scale));
    return std::make_shared<op::Add>(scaled,op::Constant::create(ov::element::f32,{bias.size()},bias));
}
ov::Output<ov::Node> buildStem(const ov::Output<ov::Node> &mel,
                              const TensorSource &source,const Dimensions &dims) {
    validate(dims);
    if (mel.get_shape()!=ov::Shape({1,dims.mels,dims.context*2}))
        throw std::runtime_error("wrong mel input shape");
    const auto convolution=[&](const ov::Output<ov::Node> &value,int index,size_t in,size_t stride) {
        const auto prefix="encoder.conv"+std::to_string(index);
        auto conv=std::make_shared<op::Convolution>(value,constant(source,prefix+".weight",{dims.state,in,3}),
                        ov::Strides{stride},ov::CoordinateDiff{1},ov::CoordinateDiff{1},ov::Strides{1});
        auto biased=std::make_shared<op::Add>(conv,reshape(constant(source,prefix+".bias",{dims.state}),{1,dims.state,1}));
        return gelu(biased);
    };
    auto first=convolution(mel,1,dims.mels,1);
    auto second=convolution(first,2,dims.state,2);
    return std::make_shared<op::Add>(transpose(second,{0,2,1}),
                constant(source,"encoder.positional_embedding",{dims.context,dims.state}));
}
ov::Output<ov::Node> buildBlock(const ov::Output<ov::Node> &input,
                               const TensorSource &source,const Dimensions &dims,size_t layer) {
    validate(dims);
    if (input.get_shape()!=ov::Shape({1,dims.context,dims.state}))
        throw std::runtime_error("wrong encoder block input shape");
    const std::string prefix="encoder.blocks."+std::to_string(layer)+".";
    auto normalized=norm(input,source,prefix+"attn_ln",dims.state);
    const auto head=[&](const ov::Output<ov::Node> &value) {
        return transpose(reshape(value,{1,dims.context,dims.heads,dims.state/dims.heads}),{0,2,1,3});
    };
    auto q=head(linear(normalized,source,prefix+"attn.query",dims.state,dims.state));
    auto k=head(linear(normalized,source,prefix+"attn.key",dims.state,dims.state,false));
    auto v=head(linear(normalized,source,prefix+"attn.value",dims.state,dims.state));
    auto logits=std::make_shared<op::MatMul>(q,k,false,true);
    const float scale=1.0f/std::sqrt(static_cast<float>(dims.state/dims.heads));
    auto scaled=std::make_shared<op::Multiply>(logits,op::Constant::create(ov::element::f32,{}, {scale}));
    auto probabilities=std::make_shared<op::Softmax>(scaled,-1);
    auto context=std::make_shared<op::MatMul>(probabilities,v,false,false);
    auto merged=reshape(transpose(context,{0,2,1,3}),{1,dims.context,dims.state});
    auto attention=std::make_shared<op::Add>(input,linear(merged,source,prefix+"attn.out",dims.state,dims.state));
    auto feedForward=norm(attention,source,prefix+"mlp_ln",dims.state);
    auto expanded=gelu(linear(feedForward,source,prefix+"mlp.0",dims.state,dims.state*4));
    return std::make_shared<op::Add>(attention,linear(expanded,source,prefix+"mlp.2",dims.state*4,dims.state));
}
std::shared_ptr<ov::Model> buildGraph(const TensorSource &source,const Dimensions &dims) {
    validate(dims);
    auto mel=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{1,dims.mels,dims.context*2});
    mel->set_friendly_name("mel"); mel->output(0).get_tensor().set_names({"mel"});
    auto value=buildStem(mel,source,dims);
    for (size_t layer=0;layer<dims.layers;++layer) value=buildBlock(value,source,dims,layer);
    value=norm(value,source,"encoder.ln_post",dims.state);
    value.get_tensor().set_names({"output_features"});
    auto model=std::make_shared<ov::Model>(ov::OutputVector{value},ov::ParameterVector{mel},"luma-kotoba-q5-encoder");
    model->validate_nodes_and_infer_types();
    return model;
}
} // namespace luma::encoder
