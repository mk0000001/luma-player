#include "encoder_graph.h"
#include <openvino/opsets/opset13.hpp>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace luma::encoder;
namespace op = ov::opset13;

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void near(const std::vector<float> &got, const std::vector<float> &want, float tolerance = 2e-5f) {
    require(got.size() == want.size(), "wrong result size");
    for (size_t i=0; i<got.size(); ++i) if (!std::isfinite(got[i]) || std::abs(got[i]-want[i])>tolerance)
        throw std::runtime_error("element " + std::to_string(i) + ": got " + std::to_string(got[i]) + ", expected " + std::to_string(want[i]));
}
template<class F> void rejects(F fn, const std::string &part) {
    try { fn(); } catch (const std::runtime_error &error) {
        require(std::string(error.what()).find(part)!=std::string::npos, "wrong rejection reason"); return;
    }
    throw std::runtime_error("invalid tensors were accepted");
}
std::vector<float> inferModel(const std::shared_ptr<ov::Model> &graph,std::vector<float> values) {
    ov::Core core;
    auto compiled=core.compile_model(graph,"CPU",ov::hint::inference_precision(ov::element::f32));
    auto request=compiled.create_infer_request();
    request.set_input_tensor(ov::Tensor(ov::element::f32,graph->input().get_shape(),values.data()));
    request.infer();
    const auto tensor=request.get_output_tensor();
    return {tensor.data<const float>(),tensor.data<const float>()+tensor.get_size()};
}
std::vector<float> infer(const ov::Output<ov::Node> &output, const std::shared_ptr<op::Parameter> &input,
                         std::vector<float> values) {
    return inferModel(std::make_shared<ov::Model>(ov::OutputVector{output},ov::ParameterVector{input}),std::move(values));
}
TensorSet stemWeights() {
    TensorSet weights;
    // Noncentral taps and stride 2 expose swapped channels, padding, and time axes.
    weights.add("encoder.conv1.weight",{2,1,3},{0,1,0, 1,0,0});
    weights.add("encoder.conv1.bias",{2},{0,0});
    weights.add("encoder.conv2.weight",{2,2,3},{0,1,0,0,0,0, 0,0,0,0,1,0});
    weights.add("encoder.conv2.bias",{2},{0,0});
    weights.add("encoder.positional_embedding",{2,2},{0.25f,-0.5f,1,2});
    return weights;
}
TensorSet blockWeights() {
    TensorSet weights;
    const std::string p="encoder.blocks.0.";
    weights.add(p+"attn_ln.weight",{2},{1,1}); weights.add(p+"attn_ln.bias",{2},{0,0});
    weights.add(p+"attn.query.weight",{2,2},{1,0,0,1}); weights.add(p+"attn.query.bias",{2},{0.2f,-0.1f});
    weights.add(p+"attn.key.weight",{2,2},{0.5f,0,0,1});
    weights.add(p+"attn.value.weight",{2,2},{1,0,0,2}); weights.add(p+"attn.value.bias",{2},{0.1f,0.3f});
    weights.add(p+"attn.out.weight",{2,2},{0.5f,0.25f,-0.1f,0.3f}); weights.add(p+"attn.out.bias",{2},{0.2f,-0.2f});
    weights.add(p+"mlp_ln.weight",{2},{1,1}); weights.add(p+"mlp_ln.bias",{2},{0,0});
    weights.add(p+"mlp.0.weight",{8,2},{0.2f,0, 0,-0.3f, 0.4f,0, 0,0.5f, 0,0, 0,0, 0,0, 0,0});
    weights.add(p+"mlp.0.bias",{8},{0.1f,0.2f,-0.1f,0,0,0,0,0});
    weights.add(p+"mlp.2.weight",{2,8},{0.7f,0,0.2f,0,0,0,0,0, 0,-0.5f,0,0.1f,0,0,0,0});
    weights.add(p+"mlp.2.bias",{2},{0.05f,-0.05f});
    return weights;
}
}

int main() {
    int failures=0;
    const auto run=[&](const char *name,const std::function<void()> &test){
        try { test(); std::cout<<"PASS "<<name<<'\n'; }
        catch(const std::exception &error){ ++failures; std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
    };
    // Catches reducing across time, outside-sqrt epsilon, or omitted affine parameters.
    run("layer normalization uses each frame's state axis",[]{
        auto input=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{1,2,2});
        near(infer(layerNorm(input,{2,3},{0.5f,-0.5f}),input,{1,3,10,14}),
             {-1.49999f,2.499985f,-1.4999975f,2.4999962f});
    });
    // Hand-checked tanh GELU values: g(g(1))=.6727551755; g(g(2))=1.905175266;
    // g(g(3))=2.992682869. Position constants are applied after the transpose.
    run("stem preserves convolution channels padding and stride",[]{
        auto input=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{1,1,4});
        auto weights=stemWeights();
        auto output=buildStem(input,weights,{1,2,2,1,1});
        require(output.get_shape()==ov::Shape({1,2,2}),"wrong stem shape");
        near(infer(output,input,{1,2,3,4}),{0.92275518f,-0.5f,3.9926829f,3.9051752f});
    });
    // Independent scalar double calculation for two frames and a nonzero Q/K/V,
    // output projection, both residuals, and a nonzero 8-wide feed-forward network.
    run("attention projection residual and feed forward affect features",[]{
        auto input=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{1,2,2});
        auto weights=blockWeights();
        near(infer(buildBlock(input,weights,{1,2,2,1,1},0),input,{1,3,4,2}),
             {1.3119311f,3.3954496f,4.5418334f,1.0577182f},8e-5f);
    });
    // Two heads have different attention probabilities. This catches reshaping
    // time into heads or merging heads in the wrong order.
    run("multiple attention heads preserve independent feature groups",[]{
        TensorSet weights;const std::string p="encoder.blocks.0.";
        const std::vector<float> identity={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        weights.add(p+"attn_ln.weight",{4},{1,1,1,1});weights.add(p+"attn_ln.bias",{4},{0,0,0,0});
        for(const auto *name:{"query","key","value"})weights.add(p+"attn."+name+".weight",{4,4},identity);
        weights.add(p+"attn.query.bias",{4},{0,0,0,0});weights.add(p+"attn.value.bias",{4},{.1f,.2f,.3f,.4f});
        weights.add(p+"attn.out.weight",{4,4},{1,0,0,0,0,2,0,0,0,0,3,0,0,0,0,4});weights.add(p+"attn.out.bias",{4},{.5f,-.5f,.25f,-.25f});
        weights.add(p+"mlp_ln.weight",{4},{1,1,1,1});weights.add(p+"mlp_ln.bias",{4},{0,0,0,0});
        weights.add(p+"mlp.0.weight",{16,4},std::vector<float>(64));weights.add(p+"mlp.0.bias",{16},std::vector<float>(16));
        weights.add(p+"mlp.2.weight",{4,16},std::vector<float>(64));weights.add(p+"mlp.2.bias",{4},std::vector<float>(4));
        auto input=std::make_shared<op::Parameter>(ov::element::f32,ov::Shape{1,2,4});
        near(infer(buildBlock(input,weights,{1,2,4,2,1},0),input,{1,3,5,7,1,4,-2,3}),
             {.45469575f,2.5432573f,6.1989878f,13.116961f,1.0826151f,5.2628775f,-4.6029358f,7.3534389f},8e-5f);
    });
    run("compressed saved IR preserves float32 ABI and encoder values",[]{
        auto weights=stemWeights();weights.add("encoder.ln_post.weight",{2},{1,1});weights.add("encoder.ln_post.bias",{2},{0,0});
        auto graph=buildGraph(weights,{1,2,2,1,0});
        const auto xml=std::filesystem::temp_directory_path()/("luma-encoder-roundtrip-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".xml");
        ov::save_model(graph,xml.u8string(),true);
        ov::Core core;auto loaded=core.read_model(xml.u8string());
        require(loaded->input().get_shape()==ov::Shape({1,1,4}) && loaded->output().get_shape()==ov::Shape({1,2,2}),"saved IR shape changed");
        require(loaded->input().get_element_type()==ov::element::f32 && loaded->output().get_element_type()==ov::element::f32,"saved IR boundary precision changed");
        require(loaded->input().get_names().count("mel") && loaded->output().get_names().count("output_features"),"saved IR port names changed");
        near(inferModel(loaded,{1,2,3,4}),{.99999012f,-.99999012f,.9973984f,-.9973984f},8e-5f);
        std::error_code error;std::filesystem::remove(xml,error);auto bin=xml;bin.replace_extension(".bin");std::filesystem::remove(bin,error);
    });
    run("duplicate named weights are rejected",[]{
        TensorSet weights; weights.add("encoder.ln_post.weight",{2},{1,1});
        rejects([&]{weights.add("encoder.ln_post.weight",{2},{2,2});},"duplicate");
    });
    run("missing named weights are rejected",[]{
        TensorSet weights; rejects([&]{weights.read("encoder.conv1.weight",{2,1,3});},"missing");
    });
    run("wrong weight shapes are rejected",[]{
        TensorSet weights; weights.add("encoder.conv1.weight",{1,2,3},std::vector<float>(6));
        rejects([&]{weights.read("encoder.conv1.weight",{2,1,3});},"shape");
    });
    run("truncated weight storage is rejected",[]{
        TensorSet weights; rejects([&]{weights.add("encoder.conv1.weight",{2,1,3},{1,2});},"size");
    });
    run("full graph rejects a missing post normalization",[]{
        auto weights=stemWeights();
        rejects([&]{buildGraph(weights,{1,2,2,1,0});},"missing");
    });
    return failures?1:0;
}
