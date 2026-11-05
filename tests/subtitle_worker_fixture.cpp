// Deterministic child process for testing process cancellation and deadlines.
// Actual recognition accuracy is tested separately against the JFK speech sample.
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
int main(int argc,char **argv){
    std::string source,prefix;
    for(int i=1;i<argc-1;++i){if(std::string(argv[i])=="-i")source=argv[i+1];if(std::string(argv[i])=="-of")prefix=argv[i+1];}
    if(source.find("stall")!=std::string::npos)std::this_thread::sleep_for(std::chrono::seconds(5));
    if(!prefix.empty()){std::ofstream out(prefix+".srt");out<<"1\n00:00:00,000 --> 00:00:01,000\nFixture transcription\n\n";return out?0:1;}
    if(argc<2)return 1;std::ofstream out(argv[argc-1],std::ios::binary);out<<std::string(128,'\0');return out?0:1;
}
