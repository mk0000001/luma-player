// Native, isolated local-ASR worker. Accelerated success is acknowledged only
// after explicit-device encoder inference and a complete subtitle file.
#include <whisper.h>
#include <ggml-backend.h>
#include <openvino/openvino.hpp>
#include "encoder_identity.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif
extern "C" bool luma_whisper_has_gpu_backend(struct whisper_context *context);
extern "C" const char *luma_whisper_gpu_device(struct whisper_context *context);
extern "C" const char *luma_whisper_gpu_backend(struct whisper_context *context);
extern "C" bool luma_whisper_encoder_statistics(const struct whisper_context *context,double *totalMs,int32_t *rawCalls);

namespace {
uint32_t le32(const unsigned char *p){return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;}
uint16_t le16(const unsigned char *p){return uint16_t(p[0]) | uint16_t(p[1])<<8;}
std::vector<float> readPcm(const std::string &path){
    std::ifstream input(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
    if(!input || input.tellg()<44 || input.tellg()>4*1024*1024)throw std::runtime_error("Expected a bounded mono 16-kHz PCM WAV");
    std::vector<unsigned char> bytes(static_cast<size_t>(input.tellg()));input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    if(!input || std::string(reinterpret_cast<char*>(bytes.data()),4)!="RIFF" || std::string(reinterpret_cast<char*>(bytes.data()+8),4)!="WAVE")throw std::runtime_error("Invalid WAV header");
    bool format=false;size_t start=0,size=0;
    for(size_t pos=12;pos+8<=bytes.size();){
        const auto chunkSize=le32(bytes.data()+pos+4);const size_t data=pos+8;
        if(chunkSize>bytes.size()-data)throw std::runtime_error("Truncated WAV chunk");
        const auto id=std::string(reinterpret_cast<char*>(bytes.data()+pos),4);
        if(id=="fmt "){format=chunkSize>=16 && le16(bytes.data()+data)==1 && le16(bytes.data()+data+2)==1 && le32(bytes.data()+data+4)==16000 && le16(bytes.data()+data+14)==16;}
        if(id=="data"){start=data;size=chunkSize;}
        pos=data+chunkSize+(chunkSize&1);
    }
    if(!format || !size || size%2 || size>16000*2*60)throw std::runtime_error("Unsupported PCM format or duration");
    std::vector<float> samples(size/2);for(size_t i=0;i<samples.size();++i)samples[i]=static_cast<int16_t>(le16(bytes.data()+start+2*i))/32768.0f;
    return samples;
}
std::string timestamp(int64_t ticks){
    const auto ms=std::max<int64_t>(0,ticks*10);std::ostringstream out;
    out<<std::setfill('0')<<std::setw(2)<<ms/3600000<<':'<<std::setw(2)<<ms/60000%60<<':'<<std::setw(2)<<ms/1000%60<<','<<std::setw(3)<<ms%1000;return out.str();
}
std::string jsonString(const std::string &text){
    std::ostringstream out;out<<'"';for(unsigned char c:text){if(c=='"' || c=='\\')out<<'\\'<<c;else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c);else out<<c;}out<<'"';return out.str();
}
int listDevices(){
    ggml_backend_load_all();bool comma=false;std::cout<<"{\"devices\":[";
    const auto add=[&](const std::string &id,const std::string &name,const std::string &backend,const std::string &type){if(comma)std::cout<<',';comma=true;std::cout<<"{\"id\":"<<jsonString(id)<<",\"name\":"<<jsonString(name)<<",\"backend\":"<<jsonString(backend)<<",\"type\":"<<jsonString(type)<<'}';};
    for(size_t i=0;i<ggml_backend_dev_count();++i){auto dev=ggml_backend_dev_get(i);const auto type=ggml_backend_dev_type(dev);if(type==GGML_BACKEND_DEVICE_TYPE_GPU || type==GGML_BACKEND_DEVICE_TYPE_IGPU)add(ggml_backend_dev_name(dev),ggml_backend_dev_description(dev),ggml_backend_reg_name(ggml_backend_dev_backend_reg(dev)),"gpu");}
    try{ov::Core core;for(const auto &dev:core.get_available_devices())if(dev.rfind("GPU",0)==0 || dev.rfind("NPU",0)==0){std::string name=dev;try{name=core.get_property(dev,ov::device::full_name);}catch(...){}add("OpenVINO:"+dev,name,"OpenVINO",dev.rfind("NPU",0)==0?"npu":"gpu");}}catch(...){}
    std::cout<<"]}"<<std::endl;return 0;
}
int run(const std::vector<std::string> &args){
    try {
        if(args.size()==2 && args[1]=="--list-devices")return listDevices();
        bool server=false,warmup=false;
        std::map<std::string,std::string> options;
        for(size_t i=1;i<args.size();++i){
            const auto &arg=args[i];if(arg=="-osrt" || arg=="-np")continue;if(arg=="--server"){server=true;continue;}if(arg=="--warmup"){warmup=true;continue;}
            if(i+1>=args.size())throw std::runtime_error("Missing option value");options[arg]=args[++i];
        }
        const auto requested=options.at("--device");
        const bool explicitOpenVino=requested.rfind("OpenVINO:",0)==0;
        const auto device=explicitOpenVino?requested.substr(9):requested;
        const bool npu=device.rfind("NPU",0)==0;
        const bool explicitNative=device.rfind("Vulkan",0)==0 || device.rfind("CUDA",0)==0;
        if(!npu && device!="GPU" && device.rfind("GPU.",0)!=0 && !explicitNative)throw std::runtime_error("Explicit NPU or GPU device required");
        ov::Core core;const auto devices=core.get_available_devices();bool openVinoAvailable=false;
        for(const auto &candidate:devices)if(candidate==device || candidate.rfind(device+".",0)==0)openVinoAvailable=true;
        if(npu && !openVinoAvailable){std::cerr<<"NPU is not available to OpenVINO\n";return 20;}
        ggml_backend_load_all();
        auto contextParams=whisper_context_default_params();contextParams.use_gpu=!npu && !explicitOpenVino;
        if(explicitNative || (device=="GPU" && !explicitOpenVino)){
            bool found=false;int gpuIndex=0;int fallback=-1;
            for(size_t i=0;i<ggml_backend_dev_count();++i){
                auto dev=ggml_backend_dev_get(i);const auto type=ggml_backend_dev_type(dev);if(type!=GGML_BACKEND_DEVICE_TYPE_GPU && type!=GGML_BACKEND_DEVICE_TYPE_IGPU)continue;
                if(fallback<0)fallback=gpuIndex;
                const auto backend=ggml_backend_reg_name(ggml_backend_dev_backend_reg(dev));
                if((explicitNative && device==ggml_backend_dev_name(dev)) || (!explicitNative && std::string(backend)=="CUDA")){contextParams.gpu_device=gpuIndex;found=true;break;}
                ++gpuIndex;
            }
            if(!found && !explicitNative && fallback>=0){contextParams.gpu_device=fallback;found=true;}
            if(!found && explicitNative){std::cerr<<"Selected GPU is unavailable\n";return 20;}
        }
        using Context=std::unique_ptr<whisper_context,decltype(&whisper_free)>;
        Context context(whisper_init_from_file_with_params(options.at("-m").c_str(),contextParams),whisper_free);
        if(!context)throw std::runtime_error("Cannot load Whisper model");
        const bool nativeGpu=contextParams.use_gpu && luma_whisper_has_gpu_backend(context.get());
        const auto gpuDevice=luma_whisper_gpu_device(context.get()),gpuBackend=luma_whisper_gpu_backend(context.get());
        const std::string actualDevice=gpuDevice?gpuDevice:"",actualBackend=gpuBackend?gpuBackend:"";
        if(explicitNative && (!nativeGpu || actualDevice!=device)){std::cerr<<"Selected GPU initialization failed\n";return 20;}
        if(!nativeGpu){
            if(!openVinoAvailable || options.find("--encoder")==options.end()){std::cerr<<"No usable explicit GPU/NPU backend\n";return 20;}
            if(contextParams.use_gpu){context.reset();contextParams.use_gpu=false;context.reset(whisper_init_from_file_with_params(options.at("-m").c_str(),contextParams));if(!context)throw std::runtime_error("Cannot initialize decoder");}
            const auto encoder=options.at("--encoder"),cache=options.at("--cache");
            const auto encoderPath=std::filesystem::u8path(encoder);
            auto manifest=encoderPath;manifest.replace_extension(".json");
            // Legacy bundled base IR is retained. New/128-mel encoders require
            // the exact source and artifact hashes before entering OpenVINO.
            const bool legacyBase=whisper_model_n_mels(context.get())==80 && whisper_model_n_audio_layer(context.get())==6 && whisper_model_n_audio_state(context.get())==512;
            if(!legacyBase || std::filesystem::exists(manifest))luma::encoder::verifyEncoderIdentity(std::filesystem::u8path(options.at("-m")),encoderPath);
            std::filesystem::create_directories(std::filesystem::u8path(cache));
            if(whisper_ctx_init_openvino_encoder(context.get(),encoder.c_str(),device.c_str(),cache.c_str())!=0){std::cerr<<"Explicit "<<device<<" encoder initialization failed\n";return 21;}
        }
        const auto nextRequest=[&](){
            std::string line;if(!std::getline(std::cin,line))return false;if(line.size()>16384)throw std::runtime_error("Request too long");
            std::istringstream request(line);std::string value;const char *keys[]={"-f","-of","-l","-t"};for(const auto key:keys){if(!std::getline(request,value,'\t') || value.empty())throw std::runtime_error("Invalid request");options[key]=value;}return true;
        };
        if(warmup){std::cout<<"LUMA_READY"<<std::endl;if(!nextRequest())return 0;}
        do{
        const auto cancelPath=std::filesystem::u8path(options.at("-of")+".cancel");
        const auto canceled=[&](){std::error_code error;return std::filesystem::exists(cancelPath,error);};
        if(canceled()){std::cout<<"LUMA_ABORTED="<<options.at("-of")<<std::endl;if(!server || !nextRequest())break;continue;}
        const auto samples=readPcm(options.at("-f"));
        auto params=whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        params.n_threads=std::clamp(std::stoi(options.at("-t")),1,16);params.language=options.at("-l").c_str();
        params.translate=false;params.print_realtime=false;params.print_progress=false;params.print_timestamps=false;params.print_special=false;
        params.max_len=48;params.token_timestamps=true;params.greedy.best_of=1;
        params.abort_callback=[](void *data){std::error_code error;return std::filesystem::exists(*static_cast<const std::filesystem::path*>(data),error);};params.abort_callback_user_data=const_cast<std::filesystem::path*>(&cancelPath);
        std::cout<<"LUMA_WORKING="<<options.at("-of")<<std::endl;
        whisper_reset_timings(context.get());
        const auto inferenceStart=std::chrono::steady_clock::now();
        const auto result=whisper_full(context.get(),params,samples.data(),static_cast<int>(samples.size()));
        const auto inferenceMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-inferenceStart).count();
        if(canceled()){std::cout<<"LUMA_ABORTED="<<options.at("-of")<<std::endl;if(!server || !nextRequest())break;continue;}
        if(result!=0){std::cerr<<"Encoder/decoder inference failed\n";return 22;}
        double encoderMs=0;int32_t encoderCalls=0;
        if(!luma_whisper_encoder_statistics(context.get(),&encoderMs,&encoderCalls) || encoderCalls<=0 || encoderMs<=0){std::cerr<<"No encoder inference completed\n";return 22;}
        // Includes actual encode-call counts; reset above prevents resident totals
        // from being mistaken for a single request or a warmed first inference.
        whisper_print_timings(context.get());
        std::ofstream output(std::filesystem::u8path(options.at("-of")+".srt"),std::ios::binary);
        if(!output)throw std::runtime_error("Cannot create subtitle output");
        int number=0;for(int i=0;i<whisper_full_n_segments(context.get());++i){
            std::string text=whisper_full_get_segment_text(context.get(),i);const auto first=text.find_first_not_of(" \r\n\t");if(first==std::string::npos)continue;text=text.substr(first,text.find_last_not_of(" \r\n\t")-first+1);
            output<<++number<<'\n'<<timestamp(whisper_full_get_segment_t0(context.get(),i))<<" --> "<<timestamp(whisper_full_get_segment_t1(context.get(),i))<<'\n'<<text<<"\n\n";
        }
        output.close();if(!output)throw std::runtime_error("Subtitle output write failed");
        std::cout<<std::fixed<<std::setprecision(3)<<"LUMA_AUDIO_MS="<<samples.size()/16.0<<'\n'<<"LUMA_INFERENCE_MS="<<inferenceMs<<'\n';
        std::cout<<"LUMA_ENCODER_MS="<<encoderMs<<'\n'<<"LUMA_ENCODER_CALLS="<<encoderCalls<<'\n';
        std::cout<<"LUMA_ACCELERATOR="<<(nativeGpu?actualBackend+" GPU":"OpenVINO encoder")<<'\n'<<"LUMA_BACKEND="<<(npu?"NPU":"GPU")<<'\n'<<"LUMA_DEVICE="<<(nativeGpu?actualDevice:requested)<<'\n'<<"LUMA_DONE="<<options.at("-of")<<std::endl;
        if(!server)break;
        if(!nextRequest())break;
        }while(server);
        return 0;
    }catch(const std::exception &error){std::cerr<<"Local ASR worker: "<<error.what()<<'\n';return 23;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv){
    std::vector<std::string> args;for(int i=0;i<argc;++i){const int bytes=WideCharToMultiByte(CP_UTF8,0,argv[i],-1,nullptr,0,nullptr,nullptr);std::string arg(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,argv[i],-1,arg.data(),bytes,nullptr,nullptr);arg.resize(bytes-1);args.push_back(std::move(arg));}return run(args);
}
#else
int main(int argc,char **argv){return run(std::vector<std::string>(argv,argv+argc));}
#endif
