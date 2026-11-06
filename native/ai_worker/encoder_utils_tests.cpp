#include "encoder_utils.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace luma::encoder;
int main() {
    int failures=0;
    const auto run=[&](const char *name,auto test){try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception &e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    const auto near=[](double got,double want){if(std::abs(got-want)>1e-9)throw std::runtime_error("wrong numerical metric");};
    const auto path=std::filesystem::temp_directory_path()/"luma-encoder-sha256-test.bin";
    // Catches reading a file as text, hashing the path, and wrong padding/byte order.
    run("source identity hashes file bytes",[&]{
        {std::ofstream out(path,std::ios::binary);out<<"abc";}
        if(sha256File(path)!="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")throw std::runtime_error("wrong SHA-256");
        {std::ofstream out(path,std::ios::binary|std::ios::trunc);}
        if(sha256File(path)!="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")throw std::runtime_error("wrong empty SHA-256");
    });
    run("another model is rejected before loading",[&]{
        try{verifyPinnedModel(path);}catch(const std::runtime_error &e){if(std::string(e.what()).find("size")!=std::string::npos)return;throw;}
        throw std::runtime_error("wrong model accepted");
    });
    // Expected metrics come from error [0,0,1], squared norm reference=14,
    // squared norm actual=21, dot product=17.
    run("embedding comparison reports scale and direction errors",[&]{
        auto m=compare({1,2,3},{1,2,4});
        if(m.count!=3)throw std::runtime_error("wrong count");
        near(m.mae,1.0/3);near(m.rmse,std::sqrt(1.0/3));near(m.maxAbsolute,1);
        near(m.referenceRms,std::sqrt(14.0/3));near(m.normalizedRmse,std::sqrt(1.0/14));
        near(m.cosine,17/std::sqrt(294.0));
    });
    run("nonfinite embeddings cannot receive similarity metrics",[]{
        try{compare({1},{std::numeric_limits<float>::quiet_NaN()});}catch(const std::runtime_error &e){if(std::string(e.what()).find("nonfinite")!=std::string::npos)return;throw;}
        throw std::runtime_error("nonfinite result accepted");
    });
    std::error_code error;std::filesystem::remove(path,error);
    return failures?1:0;
}
