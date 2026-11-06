// Deterministic child process for testing process cancellation and deadlines.
// Actual recognition accuracy is tested separately against the JFK speech sample.
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <filesystem>
int main(int argc,char **argv){
    std::string source,prefix,wav,device;bool server=false,warmup=false;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--server")server=true;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--warmup")warmup=true;
    for(int i=1;i<argc-1;++i){const std::string arg=argv[i];if(arg=="-i")source=argv[i+1];if(arg=="-f")wav=argv[i+1];if(arg=="-of")prefix=argv[i+1];if(arg=="--device")device=argv[i+1];}
    if(source=="stall.wav")std::this_thread::sleep_for(std::chrono::seconds(5));
    if(!prefix.empty()){
        if(!device.empty())if(const auto log=std::getenv("LUMA_FIXTURE_ASR_STARTS")){std::ofstream starts(log,std::ios::app);starts<<"start\n";}
        const auto nextRequest=[&](){std::string line;if(!std::getline(std::cin,line))return false;const auto tab=line.find('\t');if(tab==std::string::npos)return false;wav=line.substr(0,tab);const auto end=line.find('\t',tab+1);prefix=line.substr(tab+1,end-tab-1);return true;};
        if(warmup){
            if(std::getenv("LUMA_FIXTURE_WARMUP_FAIL"))return 20;
            if(const auto gate=std::getenv("LUMA_FIXTURE_WARMUP_GATE")){
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                while(!std::filesystem::exists(gate)){
                    if(std::chrono::steady_clock::now()>=deadline)return 24;
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }
            if(const auto delay=std::getenv("LUMA_FIXTURE_WARMUP_DELAY"))std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(delay)));
            std::cout<<"LUMA_READY"<<std::endl;if(!nextRequest())return 0;
        }
        int request=0;do{
        std::ifstream input(wav,std::ios::binary);std::string payload(128,'\0');input.read(payload.data(),128);input.close();
        const auto requestNumber=++request;
        std::cout<<"LUMA_WORKING="<<prefix<<std::endl;
        if(device=="NPU" && payload.find("stall-npu")!=std::string::npos)std::this_thread::sleep_for(std::chrono::seconds(5));
        bool canceled=false;if(payload.find("cancel-running")!=std::string::npos){
            if(const auto log=std::getenv("LUMA_FIXTURE_ASR_WORKING")){std::ofstream working(log);working<<prefix<<'\n';}
            // Native workers see a cancellation marker; the CPU CLI is killed by its owner.
            while(!std::filesystem::exists(prefix+".cancel")){
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            canceled=true;
        }
        if(canceled){std::cout<<"LUMA_ABORTED="<<prefix<<std::endl;if(!server || !nextRequest())return 0;continue;}
        if(device=="NPU" && payload.find("npu-fails")!=std::string::npos)return 20;
        std::ofstream out(prefix+".srt");out<<"1\n00:00:00,000 --> 00:00:01,000\nFixture transcription request "<<requestNumber<<"\n\n";out.close();
        if(!device.empty() && payload.find("silent-cpu")==std::string::npos)std::cout<<"LUMA_BACKEND="<<device<<std::endl;
        if(!out)return 1;
        if(!server)return 0;
        std::cout<<"LUMA_DONE="<<prefix<<std::endl;
        if(!nextRequest())return 0;
        }while(server);return 0;
    }
    if(argc<2)return 1;const std::string output=argv[argc-1];
    if(output.size()>4 && output.substr(output.size()-4)==".pcm"){
        if(const auto log=std::getenv("LUMA_FIXTURE_PCM_STARTS")){std::ofstream starts(log,std::ios::app);starts<<"start\n";}
        if(source=="stall-pcm.wav")std::this_thread::sleep_for(std::chrono::seconds(5));
        const int seconds=source=="buffered-pcm.wav"?16:source=="empty-pcm.wav"?0:1;
        std::ofstream out(output,std::ios::binary);out<<std::string(seconds*32000,'p');out.close();return source=="failed-pcm.wav"?9:out?0:1;
    }
    std::ofstream out(output,std::ios::binary);source.resize(128,'\0');out<<source;return out?0:1;
}
