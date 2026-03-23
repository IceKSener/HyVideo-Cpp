#ifndef REALCUGANFRAMEGETTER_HPP
#define REALCUGANFRAMEGETTER_HPP 1

#include  <memory>
#include <unordered_map>

#include "realcugan/realcugan.h"

#include "IFrameGetter.hpp"
#include "FrameConvert.hpp"
#include "Score.hpp"

class RealCUGANFrameGetter:public IFreamGetter{
public:
    struct Args{
        bool use_gpu = false;
        int gpu_index = 0;
        std::string model = "models-se";
        int noise = -1;
        int scale = 2;
        int syncgap = 3;
        int tilesize = 0;
    };

    RealCUGANFrameGetter(const std::shared_ptr<IFreamGetter>& getter, const Args& args);
    RealCUGANFrameGetter(RealCUGANFrameGetter&& rfg);
    ~RealCUGANFrameGetter();
    
    bool nextFrame(HvFrame& fr) override;
    bool isEnd() override{ return status.is_end; }
private:
    // 保存所有打开的模型，不需要重复初始化
    inline static std::unordered_map<std::string, RealCUGAN*> _realcugans;
    struct _info {
        std::string model;
        bool use_gpu;
        int gpu_index;
        int noise;
        int scale;
        int syncgap;
        int tilesize;
    } info; // 保存当前配置
    struct _status {
        RealCUGAN *realcugan = nullptr;         //不释放，用以多次使用
        bool is_end = false;

        HvFrame fr_in;  // 保存输入帧
        HvFrame fr_rgb; // 转换为rgb帧进行处理
    } status; // 保存输出状态
    std::shared_ptr<IFreamGetter> getter;
    FrameConvert *cvt = nullptr;

    // 获取/创建RealCUGAN模型实例
    void _initRealCUGAN();
    // 创建帧转换器并设置宽和高
    void _initConverter(int width, int height);
};

#endif // REALCUGANFRAMEGETTER_HPP