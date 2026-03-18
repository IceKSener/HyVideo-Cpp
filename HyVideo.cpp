#include <string>
#include <vector>
#include "GlobalConfig.hpp"
#include "utils/Assert.hpp"
#include "utils/Logger.hpp"
#include "utils/FileStr.hpp"
#include "utils/Clocker.hpp"
#include "Task.hpp"

using namespace std;

class ArgParser{
private:
    const int argc;
    char **argv;
    int argi;
public:
    ArgParser(int argc, char **argv)
        : argc(argc)
        , argv(argv)
        , argi(1) {}
    const char* nextArg(bool need=false){
        if(argi>=argc){
            if(need) ThrowErr("缺少参数");
            else return nullptr;
        }
        return argv[argi++];
    }
};

// 解析任务参数，格式: ":任务类型[:参数名[=参数值(默认为on)]]"
Task analyzeTask(string str) {
    vector<string> clips = strsplit(tolower(str.substr(1)), ":");
    if (clips.empty()) ThrowErr("任务参数不能为空");
    string type = clips[0];
    TaskType tasktype;
    TaskArgs args;
    if (type == "calc") {
        tasktype = CALC_SCORES;
    } else if (type == "transcode") {
        tasktype = TRANSCODE;
    } else {
        ThrowErr("未知任务: "+type);
    }
    int index;
    for (int i=1 ; i<clips.size() ; ++i) {
        const auto& arg = clips[i];
        if((index=arg.find('=')) >= 0)
            args[arg.substr(0,index)] = arg.substr(index+1);
        else
            args[arg] = "on";
    }
    return Task(tasktype, args);
}

static void test(){
}

int main(int argc, char *argv[]) {
    clocker.start(1);
    test();
    ArgParser ap(argc, argv);
    vector<Task> tasks;
    // 解析参数获取程序任务
    try {
        const char* arg;
        while (arg = ap.nextArg()) {
            if (arg[0] == ':'){
                tasks.emplace_back(analyzeTask(arg));
            } else if (arg[0] == '-') {
                string s_arg = tolower(arg);
                if (s_arg=="-t" || s_arg=="--thread") {
                    GlobalConfig.setCpuNum(atoi(ap.nextArg(true)));
                } else if (s_arg=="-s" || s_arg=="--strict") {
                    GlobalConfig.exit_on_error = false;
                }
            } else {
                if (tasks.empty()) ThrowErr("请先指定任务Task");
                Task& task = tasks.back();
                InputVideo& vd = task.addInput(arg);
            }
        }
    } catch(string errMsg) {
        av_log(NULL, AV_LOG_ERROR, "%s\n", errMsg.c_str());
        return -1;
    }

    for(Task& task: tasks) {
        try {
            task.Run();
        } catch(string errMsg) {
            av_log(NULL, AV_LOG_ERROR, "%s\n", errMsg.c_str());
            if (GlobalConfig.exit_on_error) return -1;
        } catch(exception e) {
            av_log(NULL, AV_LOG_ERROR, "%s\n", e.what());
            if (GlobalConfig.exit_on_error) return -1;
        }
    }
    AvLog("结束\n");
    clocker.end(1);
    return 0;
}