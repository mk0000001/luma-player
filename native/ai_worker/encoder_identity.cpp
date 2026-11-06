#include "encoder_identity.h"
#include "encoder_utils.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

namespace luma::encoder {
namespace {
constexpr size_t MaximumManifestBytes=64*1024;
using Dimensions=std::vector<std::uint64_t>;
using Value=std::variant<std::string,std::uint64_t,Dimensions>;
using Manifest=std::map<std::string,Value>;

[[noreturn]] void fail(const std::string &message){
    throw std::runtime_error("Encoder identity: "+message);
}

// Deliberately accepts only the flat manifest's strings, unsigned integers and
// integer arrays. Full JSON's floating point, nested objects and other types
// are outside this versioned contract and cannot silently coerce to a field.
class Parser {
    const std::string &text_;
    size_t at_=0;
    void whitespace(){
        while(at_<text_.size() && (text_[at_]==' ' || text_[at_]=='\t' || text_[at_]=='\n' || text_[at_]=='\r'))++at_;
    }
    bool take(char character){
        whitespace();
        if(at_<text_.size() && text_[at_]==character){++at_;return true;}
        return false;
    }
    void require(char character){if(!take(character))fail("malformed manifest JSON");}
    std::uint32_t hexQuad(){
        if(text_.size()-at_<4)fail("truncated Unicode escape");
        std::uint32_t result=0;
        for(int i=0;i<4;++i){
            const auto c=static_cast<unsigned char>(text_[at_++]);
            unsigned digit=0;
            if(c>='0' && c<='9')digit=c-'0';
            else if(c>='a' && c<='f')digit=c-'a'+10;
            else if(c>='A' && c<='F')digit=c-'A'+10;
            else fail("invalid Unicode escape");
            result=(result<<4)|digit;
        }
        return result;
    }
    static void appendUtf8(std::string &output,std::uint32_t code){
        if(code<=0x7f)output.push_back(static_cast<char>(code));
        else if(code<=0x7ff){
            output.push_back(static_cast<char>(0xc0|(code>>6)));
            output.push_back(static_cast<char>(0x80|(code&0x3f)));
        }else if(code<=0xffff){
            output.push_back(static_cast<char>(0xe0|(code>>12)));
            output.push_back(static_cast<char>(0x80|((code>>6)&0x3f)));
            output.push_back(static_cast<char>(0x80|(code&0x3f)));
        }else{
            output.push_back(static_cast<char>(0xf0|(code>>18)));
            output.push_back(static_cast<char>(0x80|((code>>12)&0x3f)));
            output.push_back(static_cast<char>(0x80|((code>>6)&0x3f)));
            output.push_back(static_cast<char>(0x80|(code&0x3f)));
        }
    }
    void rawUtf8(std::string &output,unsigned char first){
        unsigned following=0;
        std::uint32_t code=0,minimum=0;
        if(first>=0xc2 && first<=0xdf){following=1;code=first&0x1f;minimum=0x80;}
        else if(first>=0xe0 && first<=0xef){following=2;code=first&0x0f;minimum=0x800;}
        else if(first>=0xf0 && first<=0xf4){following=3;code=first&0x07;minimum=0x10000;}
        else fail("invalid UTF-8 in manifest");
        if(text_.size()-at_<following)fail("truncated UTF-8 in manifest");
        for(unsigned i=0;i<following;++i){
            const auto next=static_cast<unsigned char>(text_[at_++]);
            if((next&0xc0)!=0x80)fail("invalid UTF-8 in manifest");
            code=(code<<6)|(next&0x3f);
        }
        if(code<minimum || code>0x10ffff || (code>=0xd800 && code<=0xdfff))fail("invalid UTF-8 code point");
        appendUtf8(output,code);
    }
    std::string string(){
        require('"');
        std::string output;
        while(at_<text_.size()){
            const auto c=static_cast<unsigned char>(text_[at_++]);
            if(c=='"')return output;
            if(c<0x20)fail("unescaped control in manifest string");
            if(c>=0x80){rawUtf8(output,c);continue;}
            if(c!='\\'){output.push_back(static_cast<char>(c));continue;}
            if(at_==text_.size())fail("truncated manifest escape");
            switch(text_[at_++]){
            case '"':output.push_back('"');break;
            case '\\':output.push_back('\\');break;
            case '/':output.push_back('/');break;
            case 'b':output.push_back('\b');break;
            case 'f':output.push_back('\f');break;
            case 'n':output.push_back('\n');break;
            case 'r':output.push_back('\r');break;
            case 't':output.push_back('\t');break;
            case 'u':{
                auto code=hexQuad();
                if(code>=0xd800 && code<=0xdbff){
                    if(text_.size()-at_<2 || text_[at_]!='\\' || text_[at_+1]!='u')fail("missing low Unicode surrogate");
                    at_+=2;const auto low=hexQuad();
                    if(low<0xdc00 || low>0xdfff)fail("invalid low Unicode surrogate");
                    code=0x10000+((code-0xd800)<<10)+(low-0xdc00);
                }else if(code>=0xdc00 && code<=0xdfff)fail("unpaired low Unicode surrogate");
                appendUtf8(output,code);break;
            }
            default:fail("invalid manifest string escape");
            }
        }
        fail("unterminated manifest string");
    }
    std::uint64_t integer(){
        whitespace();
        if(at_==text_.size() || text_[at_]<'0' || text_[at_]>'9')fail("manifest number must be an unsigned integer");
        if(text_[at_]=='0' && at_+1<text_.size() && text_[at_+1]>='0' && text_[at_+1]<='9')fail("leading zero in manifest number");
        std::uint64_t number=0;
        while(at_<text_.size() && text_[at_]>='0' && text_[at_]<='9'){
            const auto digit=static_cast<unsigned>(text_[at_++]-'0');
            if(number>(std::numeric_limits<std::uint64_t>::max()-digit)/10)fail("manifest integer overflow");
            number=number*10+digit;
        }
        return number;
    }
    Value value(){
        whitespace();
        if(at_==text_.size())fail("missing manifest value");
        if(text_[at_]=='"')return string();
        if(take('[')){
            Dimensions dimensions;
            if(take(']'))return dimensions;
            do{
                if(dimensions.size()==8)fail("too many manifest dimensions");
                dimensions.push_back(integer());
                if(take(']'))return dimensions;
            }while(take(','));
            fail("malformed manifest dimension array");
        }
        return integer();
    }
public:
    explicit Parser(const std::string &text):text_(text){}
    Manifest parse(){
        Manifest fields;require('{');
        if(!take('}')){
            do{
                const auto key=string();
                if(fields.find(key)!=fields.end())fail("duplicate manifest key");
                if(fields.size()==24)fail("too many manifest keys");
                require(':');fields.emplace(key,value());
                if(take('}'))break;
                require(',');
            }while(true);
        }
        whitespace();if(at_!=text_.size())fail("trailing input after manifest");
        return fields;
    }
};

bool digest(const std::string &value){
    return value.size()==64 && std::all_of(value.begin(),value.end(),[](unsigned char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});
}
const Value &field(const Manifest &manifest,const char *name){
    const auto found=manifest.find(name);
    if(found==manifest.end())fail(std::string("missing manifest field: ")+name);
    return found->second;
}
const std::string &textField(const Manifest &manifest,const char *name){
    if(const auto value=std::get_if<std::string>(&field(manifest,name)))return *value;
    fail(std::string("manifest field must be a string: ")+name);
}
std::uint64_t numberField(const Manifest &manifest,const char *name){
    if(const auto value=std::get_if<std::uint64_t>(&field(manifest,name)))return *value;
    fail(std::string("manifest field must be an integer: ")+name);
}
void expectedText(const Manifest &manifest,const char *name,std::string_view expected){
    if(textField(manifest,name)!=expected)fail(std::string("unexpected manifest field: ")+name);
}
void expectedShape(const Manifest &manifest,const char *name,const Dimensions &expected){
    const auto shape=std::get_if<Dimensions>(&field(manifest,name));
    if(!shape || *shape!=expected)fail(std::string("unexpected encoder shape: ")+name);
}
void regularContainedFile(const std::filesystem::path &path,const std::filesystem::path &parent){
    std::error_code error;
    const auto status=std::filesystem::symlink_status(path,error);
    if(error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))fail("missing or nonregular encoder artifact");
    const auto resolved=std::filesystem::canonical(path,error);
    if(error || resolved.parent_path()!=parent)fail("encoder artifact escapes its directory");
}
std::string readManifest(const std::filesystem::path &path){
    std::error_code error;
    if(std::filesystem::file_size(path,error)>MaximumManifestBytes || error)fail("manifest exceeds 64 KiB or cannot be sized");
    std::ifstream input(path,std::ios::binary);
    if(!input)fail("cannot read encoder manifest");
    std::string content(MaximumManifestBytes+1,'\0');
    input.read(content.data(),static_cast<std::streamsize>(content.size()));
    const auto read=static_cast<size_t>(input.gcount());
    if(input.bad() || (!input.eof() && input.fail()))fail("encoder manifest read failed");
    if(read>MaximumManifestBytes)fail("manifest exceeds 64 KiB");
    content.resize(read);return content;
}
void validateManifest(const Manifest &manifest,const detail::VerifiedSourceIdentity &source){
    static constexpr std::array<std::string_view,24> names={
        "format","sourceModel","sourceModelSha256","sourceModelBytes",
        "encoderXmlSha256","encoderXmlBytes","encoderBinSha256","encoderBinBytes",
        "weightsPrecision","sourceQuantization","inputName","inputShape","outputName","outputShape",
        "encoderLayers","decoderLayers","heads","mels","state","audioContext","sourceTensorCount",
        "geluApproximation","whisperSource","openvinoBuildNumber"};
    for(const auto &entry:manifest)
        if(std::find(names.begin(),names.end(),entry.first)==names.end())fail("unknown manifest field");
    for(const auto name:names)field(manifest,std::string(name).c_str());
    expectedText(manifest,"format","luma-whisper-encoder-v1");
    expectedText(manifest,"sourceModel","kotoba-whisper-v2.0-q5_0");
    for(const auto name:{"sourceModelSha256","encoderXmlSha256","encoderBinSha256"})
        if(!digest(textField(manifest,name)))fail(std::string("invalid SHA-256 field: ")+name);
    if(!digest(source.sha256) || !source.bytes)fail("invalid authenticated source identity");
    expectedText(manifest,"sourceModelSha256",source.sha256);
    if(numberField(manifest,"sourceModelBytes")!=source.bytes)fail("source model byte count mismatch");
    const auto &precision=textField(manifest,"weightsPrecision");
    if(precision!="FP16" && precision!="FP32")fail("unsupported encoder weights precision");
    expectedText(manifest,"sourceQuantization","Q5_0");
    expectedText(manifest,"inputName","mel");expectedShape(manifest,"inputShape",{1,128,3000});
    expectedText(manifest,"outputName","output_features");expectedShape(manifest,"outputShape",{1,1500,1280});
    static constexpr std::array<std::pair<const char *,std::uint64_t>,7> architecture={{{"encoderLayers",32},{"decoderLayers",2},{"heads",20},{"mels",128},{"state",1280},{"audioContext",1500},{"sourceTensorCount",487}}};
    for(const auto &entry:architecture)
        if(numberField(manifest,entry.first)!=entry.second)fail(std::string("unexpected encoder architecture: ")+entry.first);
    expectedText(manifest,"geluApproximation","TANH");expectedText(manifest,"whisperSource","b5454");
    const auto &build=textField(manifest,"openvinoBuildNumber");
    if(build.empty() || std::all_of(build.begin(),build.end(),[](unsigned char c){return c==' ';}) ||
       std::any_of(build.begin(),build.end(),[](unsigned char c){return c<0x20 || c==0x7f;}))fail("missing or invalid OpenVINO build metadata");
}
void verifyFile(const std::filesystem::path &path,const Manifest &manifest,const char *sizeKey,const char *hashKey){
    const auto bytes=numberField(manifest,sizeKey);
    std::error_code error;
    const auto actual=std::filesystem::file_size(path,error);
    if(!bytes || error || actual!=bytes)fail(std::string("encoder artifact byte count mismatch: ")+sizeKey);
    if(sha256File(path)!=textField(manifest,hashKey))fail(std::string("encoder artifact SHA-256 mismatch: ")+hashKey);
}
} // namespace

void verifyEncoderIdentity(const std::filesystem::path &model,const std::filesystem::path &encoderXml){
    verifyPinnedModel(model);
    detail::verifyEncoderArtifact(encoderXml,{PinnedModelSha256,PinnedModelBytes});
}
namespace detail {
void verifyEncoderArtifact(const std::filesystem::path &encoderXml,const VerifiedSourceIdentity &source){
    if(encoderXml.empty())fail("missing encoder XML path");
    for(const auto &part:encoderXml)if(part=="..")fail("parent traversal in encoder path");
    if(encoderXml.has_root_name() && !encoderXml.has_root_directory())fail("drive-relative encoder path");
    const auto filename=encoderXml.filename().u8string();
    auto extension=encoderXml.extension().u8string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return c>='A' && c<='Z'?static_cast<char>(c-'A'+'a'):static_cast<char>(c);});
    if(filename.empty() || filename.find(':')!=std::string::npos || extension!=".xml")fail("encoder path must name an XML file");
    auto manifestPath=encoderXml;manifestPath.replace_extension(".json");
    auto bin=encoderXml;bin.replace_extension(".bin");
    const auto directory=encoderXml.has_parent_path()?encoderXml.parent_path():std::filesystem::path(".");
    std::error_code error;const auto parent=std::filesystem::canonical(directory,error);
    if(error)fail("encoder directory does not exist");
    regularContainedFile(encoderXml,parent);regularContainedFile(manifestPath,parent);regularContainedFile(bin,parent);
    const auto contents=readManifest(manifestPath);
    const auto manifest=Parser(contents).parse();validateManifest(manifest,source);
    verifyFile(encoderXml,manifest,"encoderXmlBytes","encoderXmlSha256");
    verifyFile(bin,manifest,"encoderBinBytes","encoderBinSha256");
}
} // namespace detail
} // namespace luma::encoder
