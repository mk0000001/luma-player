#include "encoder_cli.h"
#include "encoder_utils.h"
#include "encoder_identity.h"
#include "encoder_whisper.h"
#include <openvino/core/graph_util.hpp>
#include <openvino/core/version.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace luma::encoder;
namespace {
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
std::string json(const std::string &value){
    std::ostringstream out;out<<'"';
    for(unsigned char c:value){if(c=='"'||c=='\\')out<<'\\'<<c;else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c);else out<<c;}
    return out.str()+'"';
}
void number(std::ostream &out,double value){if(std::isfinite(value))out<<std::setprecision(12)<<value;else out<<"null";}
void requireKotoba(const WhisperSource &source){
    const auto d=source.dimensions();
    if(d.mels!=128 || d.context!=1500 || d.state!=1280 || d.heads!=20 || d.layers!=32 || source.tensorCount()!=487 ||
       whisper_model_n_text_layer(source.context())!=2 || whisper_model_n_text_state(source.context())!=1280 ||
       whisper_model_n_text_head(source.context())!=20)
        throw std::runtime_error("model architecture is not the pinned Kotoba v2.0 encoder/decoder");
}
std::filesystem::path sibling(std::filesystem::path xml,const char *extension){xml.replace_extension(extension);return xml;}
void exportEncoder(const Options &options){
    const auto output=std::filesystem::u8path(options.output);
    if(output.extension()!=".xml")throw std::runtime_error("encoder output must end in .xml");
    const auto binary=sibling(output,".bin"),manifest=sibling(output,".json");
    for(const auto &path:{output,binary,manifest})if(std::filesystem::exists(path))throw std::runtime_error("refusing to overwrite existing encoder artifact: "+path.u8string());
    std::cerr<<"Verifying the pinned Kotoba Q5 model...\n";
    verifyPinnedModel(std::filesystem::u8path(options.model));
    auto source=std::make_unique<WhisperSource>(std::filesystem::u8path(options.model));
    requireKotoba(*source);
    std::cerr<<"Constructing the static encoder from 487 pinned tensors...\n";
    auto graph=buildGraph(*source,source->dimensions());
    source.reset(); // Constants own copies: free GGML weights before FP16 compression.
    if(!output.parent_path().empty())std::filesystem::create_directories(output.parent_path());
    try {
        std::cerr<<"Saving "<<(options.fp32?"FP32":"Q5-derived FP16")<<" encoder IR...\n";
        ov::save_model(graph,output.u8string(),!options.fp32);
        graph.reset(); // Do not retain a second graph while hashing large artifacts.
        const auto xmlHash=sha256File(output),binHash=sha256File(binary);
        std::ofstream out(manifest,std::ios::binary);
        if(!out)throw std::runtime_error("cannot create encoder identity manifest");
        out<<"{\n"
            <<"  \"format\": \"luma-whisper-encoder-v1\",\n"
            <<"  \"sourceModel\": \"kotoba-whisper-v2.0-q5_0\",\n"
            <<"  \"sourceModelSha256\": "<<json(PinnedModelSha256)<<",\n"
            <<"  \"sourceModelBytes\": "<<PinnedModelBytes<<",\n"
            <<"  \"encoderXmlSha256\": "<<json(xmlHash)<<",\n"
            <<"  \"encoderXmlBytes\": "<<std::filesystem::file_size(output)<<",\n"
            <<"  \"encoderBinSha256\": "<<json(binHash)<<",\n"
            <<"  \"encoderBinBytes\": "<<std::filesystem::file_size(binary)<<",\n"
            <<"  \"weightsPrecision\": "<<json(options.fp32?"FP32":"FP16")<<",\n"
            <<"  \"sourceQuantization\": \"Q5_0\",\n"
            <<"  \"inputName\": \"mel\",\n  \"inputShape\": [1,128,3000],\n"
            <<"  \"outputName\": \"output_features\",\n  \"outputShape\": [1,1500,1280],\n"
            <<"  \"encoderLayers\": 32,\n  \"decoderLayers\": 2,\n  \"heads\": 20,\n"
            <<"  \"mels\": 128,\n  \"state\": 1280,\n  \"audioContext\": 1500,\n  \"sourceTensorCount\": 487,\n"
            <<"  \"geluApproximation\": \"TANH\",\n  \"whisperSource\": \"b5454\",\n"
            <<"  \"openvinoBuildNumber\": "<<json(ov::get_openvino_version().buildNumber)<<"\n}\n";
        out.close();if(!out)throw std::runtime_error("encoder identity manifest write failed");
        std::cout<<"LUMA_ENCODER_EXPORTED="<<output.u8string()<<'\n'
                 <<"LUMA_ENCODER_MANIFEST="<<manifest.u8string()<<'\n';
    } catch(...) {
        std::error_code error;
        for(const auto &path:{output,binary,manifest})std::filesystem::remove(path,error);
        throw;
    }
}
std::vector<float> readMel(const std::filesystem::path &path){
    constexpr size_t count=128*3000;
    if(std::filesystem::file_size(path)!=count*sizeof(float))throw std::runtime_error("expected exactly 128x3000 little-endian float32 mel values");
    std::vector<float> mel(count);std::ifstream input(path,std::ios::binary);
    input.read(reinterpret_cast<char*>(mel.data()),static_cast<std::streamsize>(count*sizeof(float)));
    if(!input)throw std::runtime_error("cannot read complete mel input");
    for(auto value:mel)if(!std::isfinite(value))throw std::runtime_error("nonfinite mel input");
    return mel;
}
void compareEncoder(const Options &options){
    std::cerr<<"Verifying the pinned Kotoba Q5 model...\n";verifyPinnedModel(std::filesystem::u8path(options.model));
    auto mel=readMel(std::filesystem::u8path(options.mel));
    std::vector<float> reference;
    const auto nativeStart=Clock::now();
    {
        WhisperSource source(std::filesystem::u8path(options.model),true,options.flashAttention);requireKotoba(source);
        reference=source.encode(mel,options.threads);
    } // Release the native encoder before compiling the OpenVINO graph.
    const auto nativeMs=milliseconds(nativeStart);
    if(!options.dumpNative.empty()){
        const auto path=std::filesystem::u8path(options.dumpNative);
        if(std::filesystem::exists(path))throw std::runtime_error("refusing to overwrite native features");
        std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(reference.data()),static_cast<std::streamsize>(reference.size()*sizeof(float)));
        if(!output)throw std::runtime_error("cannot write native features");
    }
    ov::Core core;
    if(!options.cache.empty()){
        std::filesystem::create_directories(std::filesystem::u8path(options.cache));core.set_property(ov::cache_dir(options.cache));
    }
    auto graph=core.read_model(options.encoder);
    if(graph->inputs().size()!=1 || graph->outputs().size()!=1 ||
       graph->input().get_shape()!=ov::Shape({1,128,3000}) || graph->output().get_shape()!=ov::Shape({1,1500,1280}) ||
       graph->input().get_element_type()!=ov::element::f32 || graph->output().get_element_type()!=ov::element::f32)
        throw std::runtime_error("encoder IR does not match the static Whisper float32 input/output ABI");
    const auto compileStart=Clock::now();
    auto compiled=options.device=="CPU"?
        core.compile_model(graph,options.device,ov::hint::inference_precision(ov::element::f32)):
        core.compile_model(graph,options.device);
    const auto compileMs=milliseconds(compileStart);graph.reset();
    auto request=compiled.create_infer_request();
    request.set_input_tensor(ov::Tensor(ov::element::f32,{1,128,3000},mel.data()));
    std::vector<float> actual(1500*1280);
    request.set_output_tensor(ov::Tensor(ov::element::f32,{1,1500,1280},actual.data()));
    const auto inferStart=Clock::now();request.infer();const auto inferMs=milliseconds(inferStart);
    const auto metrics=compare(reference,actual);
    std::string fullName;
    try{fullName=core.get_property(options.device,ov::device::full_name);}catch(...){fullName="unavailable";}
    std::vector<std::string> devices;
    try{devices=compiled.get_property(ov::execution_devices);}catch(...){}
    double maxFrameRmse=0;
    for(size_t frame=0;frame<1500;++frame){double squared=0;for(size_t i=0;i<1280;++i){const double error=double(reference[frame*1280+i])-actual[frame*1280+i];squared+=error*error;}maxFrameRmse=std::max(maxFrameRmse,std::sqrt(squared/1280));}
    std::cout<<"{\"requestedDevice\":"<<json(options.device)<<",\"deviceFullName\":"<<json(fullName)<<",\"executionDevices\":[";
    for(size_t i=0;i<devices.size();++i){if(i)std::cout<<',';std::cout<<json(devices[i]);}
    std::cout<<"],\"nativeBackend\":\"GGML CPU Q5\",\"nativeFlashAttention\":"<<(options.flashAttention?"true":"false")<<",\"thresholdApplied\":false,\"count\":"<<metrics.count;
    const auto metric=[&](const char *name,double value){std::cout<<",\""<<name<<"\":";number(std::cout,value);};
    metric("mae",metrics.mae);metric("rmse",metrics.rmse);metric("maxAbsolute",metrics.maxAbsolute);
    metric("referenceRms",metrics.referenceRms);metric("normalizedRmse",metrics.normalizedRmse);metric("cosine",metrics.cosine);
    metric("maxFrameRmse",maxFrameRmse);metric("nativeLoadEncodeFreeMs",nativeMs);metric("compileMs",compileMs);metric("inferenceMs",inferMs);
    std::cout<<"}\nLUMA_ENCODER_INFERENCE_OK="<<options.device<<'\n';
}
int run(const std::vector<std::string> &arguments){
    try{
        const auto options=parseOptions(arguments);
        if(options.help){
            std::cout<<"Native Kotoba Q5-derived OpenVINO encoder (developer tool)\n"
                <<"  --model <pinned-q5.bin> --output <new.xml> [--fp32]\n"
                <<"  --verify --model <pinned-q5.bin> --encoder <encoder.xml>\n"
                <<"  --model <pinned-q5.bin> --encoder <encoder.xml> --compare-mel <128x3000.f32>\n"
                <<"    [--device CPU|NPU[.id]|GPU[.id]] [--flash-attn on|off] [--threads 1..16]\n"
                <<"    [--cache <directory>] [--dump-native <new.f32>]\n"
                <<"Export writes XML/BIN plus a same-stem JSON identity manifest. Existing files are refused.\n"
                <<"Parity defaults to CPU / flash-attn off. Metrics do not impose an accuracy threshold.\n";
            return 0;
        }
        if(options.verify){
            verifyEncoderIdentity(std::filesystem::u8path(options.model),std::filesystem::u8path(options.encoder));
            std::cout<<"LUMA_ENCODER_VERIFIED="<<options.encoder<<'\n';
            return 0;
        }
        if(!options.output.empty())exportEncoder(options);else compareEncoder(options);
        return 0;
    }catch(const std::exception &error){std::cerr<<"Kotoba encoder: "<<error.what()<<'\n';return 1;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv){
    std::vector<std::string> arguments;
    for(int i=1;i<argc;++i){const auto size=WideCharToMultiByte(CP_UTF8,0,argv[i],-1,nullptr,0,nullptr,nullptr);std::string value(size,'\0');WideCharToMultiByte(CP_UTF8,0,argv[i],-1,value.data(),size,nullptr,nullptr);value.resize(size-1);arguments.push_back(std::move(value));}
    return run(arguments);
}
#else
int main(int argc,char **argv){return run(std::vector<std::string>(argv+1,argv+argc));}
#endif
