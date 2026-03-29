#ifndef REALESRGANFRAMEGETTER_HPP
#define REALESRGANFRAMEGETTER_HPP 1

#include  <memory>
#include <unordered_map>

#include "realesrgan/realesrgan.h"

#include "IFrameGetter.hpp"
#include "FrameConvert.hpp"

class RealESRGANFrameGetter:public IFreamGetter{
public:
    struct Args{
        bool use_gpu = false;
        int gpu_index = 0;
        std::string model = "realesr-animevideov3";
        int scale = 2;
        int tilesize = 0;
    };

    RealESRGANFrameGetter(const std::shared_ptr<IFreamGetter>& getter, const Args& args);
    RealESRGANFrameGetter(RealESRGANFrameGetter&& rfg);
    ~RealESRGANFrameGetter();
    
    bool nextFrame(HvFrame& fr) override;
    bool isEnd() override{ return status.is_end; }
private:
    // 保存所有打开的模型，不需要重复初始化
    inline static std::unordered_map<std::string, RealESRGAN*> _realesrgans;
    struct _info {
        std::string model;
        bool use_gpu;
        int gpu_index;
        int scale;
        int tilesize;
    } info; // 保存当前配置
    struct _status {
        RealESRGAN *realesrgan = nullptr;   //不释放，用以多次使用
        bool is_end = false;

        HvFrame fr_in;  // 保存输入帧
        HvFrame fr_rgb; // 转换为rgb帧进行处理
    } status; // 保存输出状态
    std::shared_ptr<IFreamGetter> getter;
    FrameConvert *cvt = nullptr;

    // 获取/创建RealESRGAN模型实例
    void _initRealESRGAN();
    // 创建帧转换器并设置宽和高
    void _initConverter(int width, int height);
};

#endif // REALESRGANFRAMEGETTER_HPP