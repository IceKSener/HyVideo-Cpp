#ifndef TASK_HPP
#define TASK_HPP 1

#include <deque>
#include <optional>
#include <map>

#include "InputVideo.hpp"

enum TaskType{
    CALC_SCORES,    //计算指数
    TRANSCODE,      //视频转码
};

typedef std::map<std::string,std::optional<std::string>> TaskArgs;

class Task{
public:
    Task(TaskType type, TaskArgs& args):type(type),args(args){ }
    Task(Task&&) = default;
    InputVideo& addInput(std::string path);
    // 开始执行任务
    bool Run();

    std::deque<InputVideo>& getInputs(){ return inputs; }
private:
    TaskType type;
    std::deque<InputVideo> inputs;
    TaskArgs args;

    // 以字符串类型读取参数
    std::string getStr(const std::string key, const std::string& def="");
    // 以整数类型读取参数
    int getInt(const std::string key, int def=0);
    // 以实数类型读取参数
    double getReal(const std::string key, double def=0);
    
    bool _taskCalcScores();
    bool _taskTranscode();
};

#endif //TASK_HPP