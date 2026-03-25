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
    inline static const union {
        uint8_t c[4];
        uint32_t i;
    }MAGIC={(uint8_t)~'S','C','O','B'};
    inline static const std::map<ScoreType,std::string> typestr={{MSE,"mse"},{SSIM,"ssim"}};
public:
    std::vector<double> scores;
    ScoreType type;
    double fps=0;
    std::optional<double> Static,Cut;
    Score(const std::vector<double>& scores, ScoreType type=MSE): scores(scores), type(type) {}
    // 从二进制文件读取数据
    static Score LoadScob(std::string path);
    Score& setFPS(double fps){ this->fps=fps;return *this; }
    Score& setStatic(double value){ Static=value;return *this; }
    Score& setCut(double value){ Cut=value;return *this; }
    // 根据阈值计算每帧的处理方法
    std::vector<int8_t> CalcProcess() const;
    // 将数据导出到内存，返回数据大小
    int Dump(uint8_t* output=nullptr) const;
};

#endif //SCORE_HPP