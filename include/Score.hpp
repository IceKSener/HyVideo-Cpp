#ifndef SCORE_HPP
#define SCORE_HPP 1

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <optional>

enum ScoreType{
    MSE,SSIM
};
class Score{
private:
    inline static const union{
        uint8_t c[4];
        uint32_t i;
    }MAGIC={(uint8_t)~'S','C','O','B'};
    inline static const std::map<ScoreType,std::string> typestr={{MSE,"mse"},{SSIM,"ssim"}};
public:
    std::vector<double> scores;
    ScoreType type;
    double fps=0;
    std::optional<double> Static,Cut;
    Score(const std::vector<double>& scores, ScoreType type=MSE);
    static Score LoadScob(std::string path);
    Score& SetFPS(double fps){ this->fps=fps;return *this; }
    Score& SetStatic(double value){ Static=value;return *this; }
    Score& SetCut(double value){ Cut=value;return *this; }
    std::vector<int8_t> CalcProcess()const;
    int Dump(uint8_t* output=nullptr)const;
};

#endif //SCORE_HPP