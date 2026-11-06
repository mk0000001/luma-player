#pragma once
#include <map>
#include <string>
#include <vector>
namespace luma::encoder {
struct Options {
    bool help=false,fp32=false,flashAttention=false,verify=false;
    int threads=1;
    std::string model,output,encoder,mel,device="CPU",cache,dumpNative;
};
Options parseOptions(const std::vector<std::string> &arguments);
}
