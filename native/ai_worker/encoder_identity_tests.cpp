#include "encoder_identity.h"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using namespace luma::encoder;
const detail::VerifiedSourceIdentity FixtureSource{
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 3};
const char *ValidManifest=R"({
  "format":"luma-whisper-encoder-v1",
  "sourceModel":"kotoba-whisper-v2.0-q5_0",
  "sourceModelSha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
  "sourceModelBytes":3,
  "encoderXmlSha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
  "encoderXmlBytes":3,
  "encoderBinSha256":"cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34",
  "encoderBinBytes":3,
  "weightsPrecision":"FP16",
  "sourceQuantization":"Q5_0",
  "inputName":"mel",
  "inputShape":[1,128,3000],
  "outputName":"output_features",
  "outputShape":[1,1500,1280],
  "encoderLayers":32,
  "decoderLayers":2,
  "heads":20,
  "mels":128,
  "state":1280,
  "audioContext":1500,
  "sourceTensorCount":487,
  "geluApproximation":"TANH",
  "whisperSource":"b5454",
  "openvinoBuildNumber":"2024.1.0-fixture"
})";

void put(const std::filesystem::path &path,const std::string &value){
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    output.write(value.data(),static_cast<std::streamsize>(value.size()));
    if(!output)throw std::runtime_error("fixture write failed");
}
std::string replaceOne(std::string text,const std::string &from,const std::string &to){
    const auto at=text.find(from);
    if(at==std::string::npos)throw std::runtime_error("fixture mutation target missing");
    text.replace(at,from.size(),to);return text;
}
struct Fixture {
    std::filesystem::path root,xml,bin,manifest,model;
    Fixture(){
        const auto base=std::filesystem::temp_directory_path();
        std::random_device random;
        for(int attempt=0;attempt<16;++attempt){
            std::ostringstream suffix;
            suffix<<"luma-encoder-identity-"<<std::hex
                  <<std::chrono::steady_clock::now().time_since_epoch().count()<<'-'<<random();
            auto candidate=base/suffix.str();
            if(std::filesystem::create_directory(candidate)){root=std::move(candidate);break;}
        }
        if(root.empty())throw std::runtime_error("cannot create private fixture directory");
        xml=root/"encoder.xml";bin=root/"encoder.bin";manifest=root/"encoder.json";model=root/"model.bin";
        put(xml,"abc");put(bin,"def");put(model,"abc");put(manifest,ValidManifest);
    }
    ~Fixture(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}
    void verify()const{detail::verifyEncoderArtifact(xml,FixtureSource);}
};
void rejects(const std::function<void()> &action){
    try{action();}catch(const std::runtime_error &){return;}
    throw std::runtime_error("invalid artifact accepted");
}
void rejectsManifest(const std::string &manifest){
    Fixture f;put(f.manifest,manifest);rejects([&]{f.verify();});
}
} // namespace

int main(){
    int failures=0;
    const auto run=[&](const char *name,auto test){
        try{test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception &e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    };
    // Catches refusing correct artifacts, or accepting a model merely because
    // its self-described manifest agrees with itself instead of the pin.
    run("matching small XML BIN and independently supplied source identity pass",[]{Fixture f;f.verify();});
    run("FP32 reference artifacts remain valid",[]{Fixture f;put(f.manifest,replaceOne(ValidManifest,"\"FP16\"","\"FP32\""));f.verify();});
    run("the public entry point still rejects a nonpinned model",[]{Fixture f;rejects([&]{verifyEncoderIdentity(f.model,f.xml);});});
    run("missing same-stem manifest is rejected",[]{Fixture f;std::filesystem::remove(f.manifest);rejects([&]{f.verify();});});
    run("missing same-stem BIN cannot be replaced by another file",[]{Fixture f;std::filesystem::rename(f.bin,f.root/"other.bin");rejects([&]{f.verify();});});
    run("same-length XML corruption is rejected by content hash",[]{Fixture f;put(f.xml,"abd");rejects([&]{f.verify();});});
    run("same-length BIN corruption is rejected by content hash",[]{Fixture f;put(f.bin,"deg");rejects([&]{f.verify();});});
    run("XML size mismatch is rejected",[]{Fixture f;put(f.xml,"abcd");rejects([&]{f.verify();});});
    run("BIN size mismatch is rejected",[]{Fixture f;put(f.bin,"defg");rejects([&]{f.verify();});});
    run("XML byte metadata must match even when content hash is correct",[]{rejectsManifest(replaceOne(ValidManifest,"\"encoderXmlBytes\":3","\"encoderXmlBytes\":4"));});
    run("BIN byte metadata must match even when content hash is correct",[]{rejectsManifest(replaceOne(ValidManifest,"\"encoderBinBytes\":3","\"encoderBinBytes\":4"));});
    run("source digest must match independently authenticated source",[]{
        rejectsManifest(replaceOne(ValidManifest,
            "\"sourceModelSha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"",
            "\"sourceModelSha256\":\"cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34\""));
    });
    run("source byte count must match independently authenticated source",[]{rejectsManifest(replaceOne(ValidManifest,"\"sourceModelBytes\":3","\"sourceModelBytes\":4"));});
    run("missing required field is rejected",[]{rejectsManifest(replaceOne(ValidManifest,"  \"whisperSource\":\"b5454\",\n",""));});
    run("duplicate field cannot override identity",[]{rejectsManifest(replaceOne(ValidManifest,"\"format\":","\"format\":\"luma-whisper-encoder-v1\",\"format\":"));});
    run("escaped duplicate key is also rejected",[]{rejectsManifest(replaceOne(ValidManifest,"\"format\":","\"fo\\u0072mat\":\"luma-whisper-encoder-v1\",\"format\":"));});
    run("unknown filename metadata cannot redirect files",[]{rejectsManifest(replaceOne(ValidManifest,"\"format\":","\"encoderBinFile\":\"../foreign.bin\",\"format\":"));});
    run("unexpected scalar types and numeric encodings are rejected",[]{
        for(const auto &bad:std::vector<std::string>{"\"32\"","true","null","{}","-32","32.0","3.2e1","032","+32","18446744073709551616"})
            rejectsManifest(replaceOne(ValidManifest,"\"encoderLayers\":32","\"encoderLayers\":"+bad));
    });
    run("shape arrays contain exactly the required integer dimensions",[]{
        for(const auto &bad:std::vector<std::string>{"[1,128,1500]","[1,128,3000,1]","[1,128]","[1,128,3000.0]","[1,128,\"3000\"]","[[1],128,3000]","[]","\"1,128,3000\""})
            rejectsManifest(replaceOne(ValidManifest,"[1,128,3000]",bad));
        rejectsManifest(replaceOne(ValidManifest,"[1,1500,1280]","[1,1280,1500]"));
    });
    run("each architecture field is enforced",[]{
        const std::vector<std::pair<std::string,std::string>> changes={
            {"\"encoderLayers\":32","\"encoderLayers\":31"},
            {"\"decoderLayers\":2","\"decoderLayers\":4"},
            {"\"heads\":20","\"heads\":16"},
            {"\"mels\":128","\"mels\":80"},
            {"\"state\":1280","\"state\":1024"},
            {"\"audioContext\":1500","\"audioContext\":750"},
            {"\"sourceTensorCount\":487","\"sourceTensorCount\":486"}};
        for(const auto &change:changes)rejectsManifest(replaceOne(ValidManifest,change.first,change.second));
    });
    run("format model precision names and numerical semantics are enforced",[]{
        const std::vector<std::pair<std::string,std::string>> changes={
            {"luma-whisper-encoder-v1","luma-whisper-encoder-v2"},
            {"kotoba-whisper-v2.0-q5_0","whisper-large-v3"},
            {"\"FP16\"","\"INT8\""},{"\"Q5_0\"","\"Q4_0\""},
            {"\"mel\"","\"input_features\""},{"\"output_features\"","\"output\""},
            {"\"TANH\"","\"ERF\""},{"\"b5454\"","\"main\""}};
        for(const auto &change:changes)rejectsManifest(replaceOne(ValidManifest,change.first,change.second));
    });
    run("all digests require a complete hexadecimal SHA256",[]{
        for(const auto &key:std::vector<std::string>{"sourceModelSha256","encoderXmlSha256","encoderBinSha256"}){
            auto value=std::string(ValidManifest);const auto start=value.find('"',value.find(':',value.find('"'+key+'"')))+1;
            value.replace(start,64,std::string(64,'g'));rejectsManifest(value);
        }
    });
    run("OpenVINO build metadata is required and nonempty",[]{
        for(const auto &bad:std::vector<std::string>{"\"\"","\"   \"","\"\\u0000\"","17","[]"})
            rejectsManifest(replaceOne(ValidManifest,"\"2024.1.0-fixture\"",bad));
    });
    run("another nonempty OpenVINO version remains valid",[]{Fixture f;put(f.manifest,replaceOne(ValidManifest,"2024.1.0-fixture","future-build-123"));f.verify();});
    run("valid escaped Unicode and surrogate pairs in build metadata parse",[]{
        Fixture f;put(f.manifest,replaceOne(ValidManifest,"2024.1.0-fixture","build-\\uD83D\\uDE00-\\uAC00"));f.verify();
    });
    run("invalid Unicode escapes and raw controls are rejected",[]{
        for(const auto &bad:std::vector<std::string>{"\\uD800","\\uDC00","\\uD800\\u0041","\\x41",std::string(1,'\x01'),std::string(1,'\xFF')})
            rejectsManifest(replaceOne(ValidManifest,"2024.1.0-fixture",bad));
    });
    run("malformed separators trailing input and nested objects are rejected",[]{
        rejectsManifest(std::string(ValidManifest)+"{}");
        rejectsManifest(replaceOne(ValidManifest,"\"heads\":20,","\"heads\":20"));
        rejectsManifest(replaceOne(ValidManifest,"\"heads\":20","\"heads\" 20"));
        rejectsManifest(replaceOne(ValidManifest,"\"openvinoBuildNumber\":\"2024.1.0-fixture\"","\"openvinoBuildNumber\":\"2024.1.0-fixture\","));
        rejectsManifest("[]");
    });
    run("valid manifests fit at exactly 64 KiB",[]{Fixture f;std::string text=ValidManifest;text.resize(65536,' ');put(f.manifest,text);f.verify();});
    run("otherwise valid manifests above 64 KiB are rejected",[]{std::string text=ValidManifest;text.resize(65537,' ');rejectsManifest(text);});
    run("parent traversal in the supplied encoder path is rejected",[]{
        Fixture f;std::filesystem::create_directory(f.root/"child");
        rejects([&]{detail::verifyEncoderArtifact(f.root/"child"/".."/"encoder.xml",FixtureSource);});
    });
    run("encoder extension must identify XML",[]{
        Fixture f;std::filesystem::rename(f.xml,f.root/"encoder.txt");
        rejects([&]{detail::verifyEncoderArtifact(f.root/"encoder.txt",FixtureSource);});
    });
    return failures?1:0;
}
