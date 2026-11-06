#include "encoder_bridge.h"
#include "encoder_whisper.h"
#include <whisper.h>
#include <ggml.h>
#include <ggml-backend.h>
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
void word(std::ofstream &out,uint32_t value){for(unsigned i=0;i<4;++i)out.put(static_cast<char>(value>>(i*8)));}
void tensor(std::ofstream &out,const std::string &name,const std::vector<int> &shape,float value=0){
    word(out,static_cast<uint32_t>(shape.size()));word(out,static_cast<uint32_t>(name.size()));word(out,0);
    size_t count=1;for(auto extent:shape){word(out,extent);count*=extent;}out.write(name.data(),name.size());
    uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));for(size_t i=0;i<count;++i)word(out,bits);
}
void norm(std::ofstream &out,const std::string &name){tensor(out,name+".weight",{2},1);tensor(out,name+".bias",{2});}
void attention(std::ofstream &out,const std::string &name){
    tensor(out,name+".query.weight",{2,2});tensor(out,name+".query.bias",{2});
    tensor(out,name+".key.weight",{2,2});tensor(out,name+".value.weight",{2,2});tensor(out,name+".value.bias",{2});
    tensor(out,name+".out.weight",{2,2});tensor(out,name+".out.bias",{2});
}
void feedForward(std::ofstream &out,const std::string &name){
    norm(out,name+"mlp_ln");tensor(out,name+"mlp.0.weight",{2,8});tensor(out,name+"mlp.0.bias",{8});
    tensor(out,name+"mlp.2.weight",{8,2});tensor(out,name+"mlp.2.bias",{2});
}
void tinyWhisper(const std::filesystem::path &path){
    std::ofstream out(path,std::ios::binary);
    word(out,0x67676d6c);
    // A valid tiny architecture with 4 encoder blocks, 2 hidden channels and
    // 4 mel frames. The large vocabulary costs <0.5 MiB and keeps token metadata valid.
    for(auto value:{51865,2,2,1,4,4,2,1,1,2,0})word(out,value);
    word(out,2);word(out,201);for(int i=0;i<402;++i)word(out,0);
    word(out,1);word(out,1);out.put('?');
    tensor(out,"encoder.positional_embedding",{2,2});
    tensor(out,"encoder.conv1.weight",{3,2,2});tensor(out,"encoder.conv1.bias",{1,2});
    tensor(out,"encoder.conv2.weight",{3,2,2});tensor(out,"encoder.conv2.bias",{1,2});norm(out,"encoder.ln_post");
    for(int layer=0;layer<4;++layer){const auto p="encoder.blocks."+std::to_string(layer)+".";norm(out,p+"attn_ln");attention(out,p+"attn");feedForward(out,p);}
    tensor(out,"decoder.positional_embedding",{2,4});tensor(out,"decoder.token_embedding.weight",{2,51865});norm(out,"decoder.ln");
    norm(out,"decoder.blocks.0.attn_ln");attention(out,"decoder.blocks.0.attn");
    norm(out,"decoder.blocks.0.cross_attn_ln");attention(out,"decoder.blocks.0.cross_attn");feedForward(out,"decoder.blocks.0.");
    require(static_cast<bool>(out),"cannot write synthetic GGML fixture");
}
using Context=std::unique_ptr<whisper_context,decltype(&whisper_free)>;
Context load(const std::filesystem::path &path){
    auto params=whisper_context_default_params();params.use_gpu=false;params.flash_attn=false;
    auto result=Context(whisper_init_from_file_with_params(path.u8string().c_str(),params),whisper_free);
    require(result!=nullptr,"cannot initialize synthetic Whisper fixture");return result;
}
}
int main(){
    int failures=0;
    const auto run=[&](const char *name,const std::function<void()> &test){try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception &e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    ggml_backend_load_all();
    const auto directory=std::filesystem::temp_directory_path()/("luma-encoder-boundary-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto modelPath=directory/"synthetic.bin";tinyWhisper(modelPath);
    run("bounded accessor exposes encoder tensors only",[&]{
        auto ctx=load(modelPath);
        require(luma_whisper_encoder_tensor_count(ctx.get())==67,"missing encoder tensor catalog");
        auto weight=luma_whisper_encoder_tensor(ctx.get(),"encoder.ln_post.weight");
        require(weight && weight->ne[0]==2,"encoder weight unavailable");
        float values[2]{};ggml_backend_tensor_get(weight,values,0,sizeof(values));
        require(values[0]==1 && values[1]==1,"wrong encoder weight bytes");
        require(!luma_whisper_encoder_tensor(ctx.get(),"decoder.ln.weight"),"decoder tensor crossed encoder boundary");
        require(!luma_whisper_encoder_tensor(ctx.get(),"encoder.absent"),"missing tensor was accepted");
        require(!luma_whisper_encoder_tensor(nullptr,"encoder.ln_post.weight"),"null context was accepted");
    });
    run("native encoded features have an exact bounded copy",[&]{
        auto ctx=load(modelPath);const float mel[8]={1,2,3,4,5,6,7,8};
        require(whisper_set_mel(ctx.get(),mel,4,2)==0 && whisper_encode(ctx.get(),0,1)==0,"native encode failed");
        float output[4]={9,9,9,9};
        require(!luma_whisper_copy_encoder_features(ctx.get(),output,3),"short destination accepted");
        require(luma_whisper_copy_encoder_features(ctx.get(),output,4),"encoder features unavailable");
        for(auto value:output)require(value==0,"wrong native features");
    });
    // A printed timing line clamps zero encoder calls to one. The execution
    // acknowledgement must instead consume the real counter, including reset.
    run("raw encoder statistics preserve zero calls before inference and after reset",[&]{
        auto ctx=load(modelPath);double totalMs=-1;int32_t calls=-1;
        require(luma_whisper_encoder_statistics(ctx.get(),&totalMs,&calls),"encoder statistics unavailable");
        require(calls==0 && totalMs==0,"unexecuted encoder reported work");
        whisper_print_timings(ctx.get());
        require(luma_whisper_encoder_statistics(ctx.get(),&totalMs,&calls) && calls==0 && totalMs==0,"printed timing clamp leaked into raw statistics");
        const float mel[8]={1,2,3,4,5,6,7,8};
        require(whisper_set_mel(ctx.get(),mel,4,2)==0 && whisper_encode(ctx.get(),0,1)==0,"native encode failed");
        require(luma_whisper_encoder_statistics(ctx.get(),&totalMs,&calls) && calls==1 && totalMs>=0,"executed encoder counter is wrong");
        whisper_reset_timings(ctx.get());
        require(luma_whisper_encoder_statistics(ctx.get(),&totalMs,&calls) && calls==0 && totalMs==0,"reset encoder statistics retained previous work");
        require(!luma_whisper_encoder_statistics(nullptr,&totalMs,&calls),"null context statistics accepted");
        require(!luma_whisper_encoder_statistics(ctx.get(),nullptr,&calls),"null elapsed destination accepted");
        require(!luma_whisper_encoder_statistics(ctx.get(),&totalMs,nullptr),"null call destination accepted");
    });
    run("Whisper source reads canonical shapes and native features",[&]{
        luma::encoder::WhisperSource source(modelPath,true,false);
        auto dims=source.dimensions();require(dims.mels==2 && dims.context==2 && dims.state==2 && dims.layers==4,"wrong source dimensions");
        auto bias=source.read("encoder.conv1.bias",{2});require(bias.values==std::vector<float>({0,0}),"wrong convolution bias shape");
        auto weight=source.read("encoder.ln_post.weight",{2});require(weight.values==std::vector<float>({1,1}),"wrong source data");
        auto features=source.encode({1,2,3,4,5,6,7,8},1);require(features==std::vector<float>({0,0,0,0}),"wrong source reference features");
    });
    // This must fail on the unpatched upstream path: OpenVINO rejects [1,2,4]
    // against an IR compiled for [1,2,5], but whisper_encode formerly returned 0.
    run("failed IR inference propagates through public encode",[&]{
        auto parameter=std::make_shared<ov::opset13::Parameter>(ov::element::f32,ov::Shape{1,2,5});
        auto graph=std::make_shared<ov::Model>(ov::OutputVector{parameter},ov::ParameterVector{parameter});
        const auto irPath=directory/"incompatible.xml";ov::serialize(graph,irPath.u8string());
        auto ctx=load(modelPath);
        require(whisper_ctx_init_openvino_encoder(ctx.get(),irPath.u8string().c_str(),"CPU","")==0,"cannot compile tiny CPU IR");
        const float mel[8]={1,2,3,4,5,6,7,8};require(whisper_set_mel(ctx.get(),mel,4,2)==0,"cannot set mel");
        require(whisper_encode(ctx.get(),0,1)!=0,"failed OpenVINO inference reported success");
    });
    std::error_code error;
    for(const auto &name:{"synthetic.bin","incompatible.xml","incompatible.bin"})std::filesystem::remove(directory/name,error);
    std::filesystem::remove(directory,error);
    return failures?1:0;
}
