#include "encoder_utils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace luma::encoder {
namespace {
uint32_t rotate(uint32_t value,unsigned bits) { return (value>>bits)|(value<<(32-bits)); }
class Sha256 {
    std::array<uint32_t,8> state_{{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    std::array<unsigned char,64> block_{};
    size_t used_=0;
    uint64_t bytes_=0;
    void transform() {
        static constexpr uint32_t k[64]={
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for(size_t i=0;i<16;++i) w[i]=uint32_t(block_[4*i])<<24|uint32_t(block_[4*i+1])<<16|uint32_t(block_[4*i+2])<<8|block_[4*i+3];
        for(size_t i=16;i<64;++i) {
            const auto a=w[i-15],b=w[i-2];
            w[i]=w[i-16]+(rotate(a,7)^rotate(a,18)^(a>>3))+w[i-7]+(rotate(b,17)^rotate(b,19)^(b>>10));
        }
        auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for(size_t i=0;i<64;++i) {
            const uint32_t t1=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            const uint32_t t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;
        state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
public:
    void update(const char *data,size_t size) {
        bytes_+=size;
        while(size) {
            const auto take=std::min(size,block_.size()-used_);
            std::copy_n(reinterpret_cast<const unsigned char*>(data),take,block_.begin()+used_);
            data+=take;size-=take;used_+=take;
            if(used_==block_.size()){transform();used_=0;}
        }
    }
    std::string finish() {
        const uint64_t bits=bytes_*8;
        block_[used_++]=0x80;
        if(used_>56){std::fill(block_.begin()+used_,block_.end(),0);transform();used_=0;}
        std::fill(block_.begin()+used_,block_.begin()+56,0);
        for(size_t i=0;i<8;++i)block_[56+i]=static_cast<unsigned char>(bits>>(56-8*i));
        transform();
        std::ostringstream result;result<<std::hex<<std::setfill('0');
        for(auto word:state_)result<<std::setw(8)<<word;
        return result.str();
    }
};
}
std::string sha256File(const std::filesystem::path &path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("cannot read model/artifact for SHA-256");
    std::array<char,65536> buffer;
    Sha256 hash;
    while(input){input.read(buffer.data(),buffer.size());hash.update(buffer.data(),static_cast<size_t>(input.gcount()));}
    if(!input.eof())throw std::runtime_error("model/artifact read failed during SHA-256");
    return hash.finish();
}
void verifyPinnedModel(const std::filesystem::path &path) {
    if(std::filesystem::file_size(path)!=PinnedModelBytes)throw std::runtime_error("Kotoba model size does not match pinned Q5 model");
    if(sha256File(path)!=PinnedModelSha256)throw std::runtime_error("Kotoba model SHA-256 does not match pinned Q5 model");
}
Metrics compare(const std::vector<float> &reference,const std::vector<float> &actual) {
    if(reference.empty() || reference.size()!=actual.size())throw std::runtime_error("embedding size mismatch");
    Metrics result;result.count=reference.size();
    double squaredError=0,refSquared=0,actualSquared=0,dot=0;
    for(size_t i=0;i<reference.size();++i){
        const double a=reference[i],b=actual[i];
        if(!std::isfinite(a) || !std::isfinite(b))throw std::runtime_error("nonfinite embedding value");
        const auto error=std::abs(a-b);result.mae+=error;result.maxAbsolute=std::max(result.maxAbsolute,error);
        squaredError+=error*error;refSquared+=a*a;actualSquared+=b*b;dot+=a*b;
    }
    result.mae/=result.count;result.rmse=std::sqrt(squaredError/result.count);result.referenceRms=std::sqrt(refSquared/result.count);
    result.normalizedRmse=refSquared?std::sqrt(squaredError/refSquared):(squaredError?std::numeric_limits<double>::infinity():0);
    result.cosine=(refSquared && actualSquared)?std::clamp(dot/std::sqrt(refSquared*actualSquared),-1.0,1.0):(!refSquared && !actualSquared?1:0);
    return result;
}
}
