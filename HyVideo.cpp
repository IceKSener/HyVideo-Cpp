#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <omp.h>
#include <chrono>
#include "Task.hpp"

using namespace std;
uint32_t cpu_num=1;

int _argc,_argn=1;
char **_argv=nullptr;
char* nextArg(bool need=false){
    if(_argn>=_argc){
        if(need) ThrowErr("缺少参数");
        else return nullptr;
    }
    return _argv[_argn++];
}

void analyzeTask(string str, TaskType& tasktype, TaskArgs& typeargs){
    vector<string> clips=strsplit(tolower(str.substr(1)),":");
    string type=clips[0];
    map<string,optional<string>> args;
    if(type=="calc"){
        tasktype=CALC_SCORES;
    }else if(type=="transcode"){
        tasktype=TRANSCODE;
    }else{
        ThrowErr("未知任务: "+type);
    }
    for(int i=1,index ; i<clips.size() ; ++i){
        if((index=clips[i].find('='))>=0){
            args[clips[i].substr(0,index)]=
                clips[i].substr(index+1);
        }else
            args[clips[i]]="on";
    }
    typeargs=args;
}

int main(int argc, char *argv[]){
    _argc=argc, _argv=argv;
    cpu_num=thread::hardware_concurrency();
    omp_set_num_threads(cpu_num);
    vector<Task> tasks;
    TaskType tasktype=CALC_SCORES;
    TaskArgs taskargs;
    try{
        const char* arg;
        while (arg=nextArg()){
            if(arg[0]==':'){
                analyzeTask(arg,tasktype,taskargs);
                tasks.emplace_back(tasktype,taskargs);
            }else{
                if(tasks.empty()) ThrowErr("请先指定任务Task");
                string file=arg;
                Task& task=tasks.back();
                InputVideo& vd=task.addInput(file);
                AvLog("添加[%s]\n",arg);
                vd.Print();
            }
        }
    }catch(string errMsg){
        av_log(NULL, AV_LOG_ERROR, "%s\n", errMsg.c_str());
        return -1;
    }
    try{
        for(Task& task:tasks) task.Run();
    }catch(string errMsg){
        av_log(NULL, AV_LOG_ERROR, "%s\n", errMsg.c_str());
        return -1;
    }
    return 0;
}