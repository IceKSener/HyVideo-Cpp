#ifndef TASK_HPP
#define TASK_HPP 1

#include <vector>
#include <optional>
#include <map>
#include "InputVideo.hpp"
#include "OutputVideo.hpp"
#include "VideoFrameReader.hpp"
#include "Score.hpp"

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
    std::vector<OutputVideo> outputs;
    Score CalcMSE(InputVideo& vd);
    TaskArgs args;

    std::vector<Score> CalcScores(ScoreType type);
public:
    Task(TaskType type, TaskArgs& args):type(type),args(args){ }
    std::vector<InputVideo>& getInputs(){ return inputs; }
    std::vector<OutputVideo>& getOutputs(){ return outputs; }

    InputVideo& addInput(std::string path);
    OutputVideo& addOutput(std::string path, std::string format);

    bool Run();
};

#endif //TASK_HPP