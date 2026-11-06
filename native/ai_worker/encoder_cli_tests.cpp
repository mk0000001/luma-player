#include "encoder_cli.h"
#include <iostream>
#include <stdexcept>
using namespace luma::encoder;
int main(){
    int failures=0;
    const auto run=[&](const char *name,auto test){try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception &e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    const auto rejects=[](const std::vector<std::string> &args,const std::string &reason){try{parseOptions(args);}catch(const std::runtime_error &e){if(std::string(e.what()).find(reason)!=std::string::npos)return;throw;}throw std::runtime_error("invalid CLI accepted");};
    run("export command preserves requested output precision",[]{auto o=parseOptions({"--model","q5.bin","--output","encoder.xml","--fp32"});if(o.model!="q5.bin"||o.output!="encoder.xml"||!o.fp32)throw std::runtime_error("wrong export options");});
    run("parity defaults to explicit CPU and nonflash reference",[]{auto o=parseOptions({"--model","q5.bin","--encoder","encoder.xml","--compare-mel","mel.f32"});if(o.device!="CPU"||o.flashAttention||o.threads!=1)throw std::runtime_error("wrong comparison defaults");});
    run("explicit NPU and production flash mode are preserved",[]{auto o=parseOptions({"--model","q5.bin","--encoder","encoder.xml","--compare-mel","mel.f32","--device","NPU.0","--flash-attn","on","--threads","4"});if(o.device!="NPU.0"||!o.flashAttention||o.threads!=4)throw std::runtime_error("wrong device/reference options");});
    run("automatic and heterogeneous inference are rejected",[&]{rejects({"--model","q5.bin","--encoder","e.xml","--compare-mel","m","--device","HETERO:NPU,CPU"},"device");rejects({"--model","q5.bin","--encoder","e.xml","--compare-mel","m","--device","AUTO"},"device");});
    run("duplicate CLI options cannot silently override identity",[&]{rejects({"--model","q5.bin","--model","other.bin","--output","e.xml"},"duplicate");});
    run("export and comparison modes cannot be combined",[&]{rejects({"--model","q5.bin","--output","e.xml","--encoder","e.xml","--compare-mel","m"},"mode");});
    run("unknown options cannot be silently ignored",[&]{rejects({"--model","q5.bin","--output","e.xml","--unknown","x"},"unknown");});
    run("verification accepts source and encoder without loading comparison input",[]{
        const auto o=parseOptions({"--verify","--model","q5.bin","--encoder","encoder.xml"});
        if(!o.verify || o.model!="q5.bin" || o.encoder!="encoder.xml" || !o.output.empty() || !o.mel.empty())throw std::runtime_error("wrong verification options");
    });
    run("verification cannot be combined with export or inference controls",[&]{
        for(const auto &extra:std::vector<std::vector<std::string>>{{"--output","out.xml"},{"--compare-mel","mel.f32"},{"--device","NPU"},{"--fp32"},{"--cache","cache"},{"--threads","4"},{"--flash-attn","on"},{"--dump-native","out.f32"}}){
            std::vector<std::string> args={"--verify","--model","q5.bin","--encoder","encoder.xml"};args.insert(args.end(),extra.begin(),extra.end());rejects(args,"mode");
        }
    });
    run("verification requires an encoder identity target",[&]{rejects({"--verify","--model","q5.bin"},"mode");});
    return failures?1:0;
}
