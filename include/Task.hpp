#ifndef TASK_HPP
#define TASK_HPP 1

#include <vector>
#include "Video.hpp"
#include "VideoFrameReader.hpp"
#include "Score.hpp"

extern "C"{
    #include "libavutil/avutil.h"
}
enum TaskType{
    CALC_SCORES
};
class Task{
private:
    TaskType type = CALC_SCORES;
    std::vector<Video> inputs;
    std::vector<Video> outputs;
    Score CalcMSE(Video& vd);
public:
    std::vector<Video>& getInputs(){ return inputs; }
    std::vector<Video>& getOutputs(){ return outputs; }

    Video& addInput(std::string path);
    Video& addOutput(std::string path, std::string format);

    bool Run();

    std::vector<Score> CalcScores(ScoreType type);
};

#endif //TASK_HPP