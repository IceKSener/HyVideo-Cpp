#ifndef TASK_HPP
#define TASK_HPP 1

#include "InputVideo.hpp"
#include <vector>
#include <optional>
#include <map>

extern "C"{
    #include "libavutil/avutil.h"
}
enum TaskType{
    CALC_SCORES,    //计算指数
    TRANSCODE,      //视频转码
};
typedef std::map<std::string,std::optional<std::string>> TaskArgs;
class Task{
private:
    TaskType type = CALC_SCORES;
    std::vector<InputVideo> inputs;
    TaskArgs args;

    std::string getStr(TaskArgs args, const std::string key, const std::string& def="");
    int getInt(TaskArgs args, const std::string key, int def=0);
    double getReal(TaskArgs args, const std::string key, double def=0);
    bool _taskCalcScores();
    bool _taskTranscode();
public:
    Task(TaskType type, TaskArgs& args):type(type),args(args){ }
    std::vector<InputVideo>& getInputs(){ return inputs; }

    InputVideo& addInput(std::string path);

    bool Run();
};

#endif //TASK_HPP