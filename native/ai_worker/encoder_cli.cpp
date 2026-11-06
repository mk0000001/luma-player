#include "encoder_cli.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
namespace luma::encoder {
Options parseOptions(const std::vector<std::string> &arguments){
    Options result;
    if(arguments==std::vector<std::string>{"--help"}){result.help=true;return result;}
    const std::set<std::string> known={"--model","--output","--fp32","--encoder","--compare-mel","--device","--flash-attn","--threads","--cache","--dump-native","--verify"};
    std::map<std::string,std::string> values;
    for(size_t i=0;i<arguments.size();++i){
        const auto &key=arguments[i];
        if(!known.count(key))throw std::runtime_error("unknown option: "+key);
        if(values.count(key))throw std::runtime_error("duplicate option: "+key);
        if(key=="--fp32"){values[key]="true";result.fp32=true;continue;}
        if(key=="--verify"){values[key]="true";result.verify=true;continue;}
        if(i+1==arguments.size() || arguments[i+1].empty() || arguments[i+1].rfind("--",0)==0)
            throw std::runtime_error("missing option value: "+key);
        values[key]=arguments[++i];
    }
    const auto value=[&](const char *key){const auto found=values.find(key);return found==values.end()?std::string():found->second;};
    result.model=value("--model");result.output=value("--output");result.encoder=value("--encoder");result.mel=value("--compare-mel");
    result.cache=value("--cache");result.dumpNative=value("--dump-native");
    if(result.model.empty())throw std::runtime_error("missing model path");
    if(result.verify){
        if(result.encoder.empty() || values.size()!=3)throw std::runtime_error("verification mode requires only --verify, --model and --encoder");
        return result;
    }
    const bool exporting=!result.output.empty(),comparing=!result.encoder.empty() && !result.mel.empty();
    if(exporting==comparing || (!exporting && (!comparing || result.fp32)) ||
       (exporting && (!result.encoder.empty() || !result.mel.empty() || values.count("--device") ||
                       values.count("--flash-attn") || values.count("--threads") || !result.cache.empty() || !result.dumpNative.empty())))
        throw std::runtime_error("choose one export or comparison mode");
    if(values.count("--device"))result.device=value("--device");
    const auto explicitDevice=[](const std::string &device){
        if(device=="CPU" || device=="NPU" || device=="GPU")return true;
        const bool indexed=device.rfind("NPU.",0)==0 || device.rfind("GPU.",0)==0;
        return indexed && device.size()>4 && std::all_of(device.begin()+4,device.end(),[](unsigned char c){return std::isdigit(c)!=0;});
    };
    if(!explicitDevice(result.device))throw std::runtime_error("comparison requires an explicit CPU, GPU, or NPU device");
    const auto flash=value("--flash-attn");
    if(!flash.empty() && flash!="on" && flash!="off")throw std::runtime_error("flash-attn must be on or off");
    result.flashAttention=flash=="on";
    if(values.count("--threads")){
        const auto text=value("--threads");
        if(text.empty() || !std::all_of(text.begin(),text.end(),[](unsigned char c){return std::isdigit(c)!=0;}))throw std::runtime_error("invalid threads");
        try{result.threads=std::stoi(text);}catch(...){throw std::runtime_error("invalid threads");}
        if(result.threads<1 || result.threads>16)throw std::runtime_error("threads must be between 1 and 16");
    }
    return result;
}
}
