#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <omp.h>
#include <chrono>
#include "Task.hpp"
#include "Video.hpp"
#include "VideoFrameReader.hpp"

using namespace std;
uint32_t cpu_num=1;

int _argc,_argn=1;
char **_argv=nullptr;
char* nextArg(bool need=false){
    if(_argn>=_argc){
        if(need) throw string("缺少参数");
        else return nullptr;
    }
    else return _argv[_argn++];
}

int main(int argc, char *argv[]){
    _argc=argc, _argv=argv;
    cpu_num=thread::hardware_concurrency();
    omp_set_num_threads(cpu_num);
    try{
        vector<Task> tasks;
        const char* arg;
        while (arg=nextArg()){
            if(arg[0]=='-'){
                ;
            }else{
                string file=arg;
                Task& t=tasks.emplace_back();
                Video& vd=t.addInput(file);
                av_log(NULL, AV_LOG_INFO,"添加[%s]\n",arg);
                vd.Print();

                clock_t ts=clock();
                auto score=t.CalcScores(ScoreType::MSE)[0];
                ts=clock()-ts;
                size_t n=score.getScores().size();
                av_log(NULL, AV_LOG_INFO,"数据量:%d,耗时:%d(%lf)",n,ts,1.0*n/ts);

                char data[score.Dump()];
                score.Dump((uint8_t*)data);
                string outpath=file.substr(0,file.rfind('.'));
                ofstream of(withsuffix(file,"scob"), ios::binary);
                of.write(data, sizeof(data));
                of.close();
            }
        }
    }catch(string errMsg){
        av_log(NULL, AV_LOG_ERROR, "%s\n", errMsg.c_str());
    }
    return 0;
}