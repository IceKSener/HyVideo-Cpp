#ifndef SCORE_HPP
#define SCORE_HPP 1

#include <string>
#include <vector>
#include <fstream>
#include <map>
extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/imgutils.h"
}
#include "Common.hpp"
enum ScoreType{
    MSE,SSIM
};
class Score{
private:
    inline static const union{
        uint8_t c[4];
        uint32_t i;
    }MAGIC={(uint8_t)~'S','C','O','B'};
    std::vector<double> scores;
    ScoreType type;
    inline static const std::map<ScoreType,std::string> typestr={{MSE,"mse"},{SSIM,"ssim"}};
    double fps=0;
    struct{
        bool enable;
        double value;
    }Static={false,0}, Cut={false,0};
public:
    Score(const std::vector<double>& scores, ScoreType type=MSE);
    static Score LoadScob(std::string path);
    Score& setFPS(double fps){ this->fps=fps;return *this; }
    Score& setStatic(double value){ Static={true, value};return *this; }
    Score& setCut(double value){ Cut={true, value};return *this; }
    std::vector<double>& getScores(){ return scores; }
    int Dump(uint8_t* output=nullptr);
};

#endif //SCORE_HPP