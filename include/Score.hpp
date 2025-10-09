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
    std::vector<double> scores;
    ScoreType type;
    inline static const std::map<ScoreType,std::string> typestr={{MSE,"mse"},{SSIM,"ssim"}};
    double fps=0;
    std::optional<double> Static,Cut;
public:
    Score(const std::vector<double>& scores, ScoreType type=MSE);
    static Score LoadScob(std::string path);
    Score& setFPS(double fps){ this->fps=fps;return *this; }
    Score& setStatic(double value){ Static=value;return *this; }
    Score& setCut(double value){ Cut=value;return *this; }
    std::vector<double>& getScores(){ return scores; }
    int Dump(uint8_t* output=nullptr)const;
};

#endif //SCORE_HPP