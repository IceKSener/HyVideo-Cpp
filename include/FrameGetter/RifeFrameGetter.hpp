#ifndef RIFEFRAMEGETTER_HPP
#define RIFEFRAMEGETTER_HPP 1

#include  <memory>
#include <unordered_map>

#include "mat.h"
#include "rife/rife.h"

#include "IFrameGetter.hpp"
#include "FrameConvert.hpp"
#include "Score.hpp"

class RifeFrameGetter:public IFreamGetter{
public:
    struct Args{
        bool use_gpu = false;
        int gpu_index = 0;
        std::string model = "rife-v4.22-lite";
    };

    RifeFrameGetter(const std::shared_ptr<IFreamGetter>& getter, const Args& args);
    RifeFrameGetter(RifeFrameGetter&& rfg);
    ~RifeFrameGetter();
    RifeFrameGetter& setFPSX(AVRational fpsx){ info.fpsx=fpsx; return *this; }
    RifeFrameGetter& setProcess(bool process, const Score *score=nullptr);
    AVFrame* nextFrame(AVFrame *fr=nullptr) override;
    bool isEnd() override{ return status.is_end; }
private:
    // 保存所有打开的模型，不需要重复初始化
    inline static std::unordered_map<std::string, RIFE*> _rifes;
    struct _info {
        std::string model;
        bool use_gpu;
        int gpu_index;
        AVRational fpsx = {1, 1};
        std::optional<std::vector<int8_t>> process;
        // 根据是否process决定使用的函数
        AVFrame *(RifeFrameGetter::*_NextFrame)() = &RifeFrameGetter::_nextFrameNoProcess;
    } info; // 保存当前配置
    struct _status {
        RIFE *rife = nullptr;         //不释放，用以多次使用
        bool is_end = false;

        int fr_index=0, f0_index=0, f1_index=1; // 记录帧序号
        int f0_pts, f1_pts=0;   // 记录帧的pts时间
        AVFrame *f0=nullptr, *f1=nullptr;   // 保存输入帧
        AVFrame *fr = nullptr;  // 输出帧（可能经过处理，也可能为f0,f1原本的帧）
        AVFrame *f0_rgb=nullptr, *f1_rgb=nullptr;   // 转换为rgb帧进行处理
        bool rgb_valid[2] = {false, false}; // 记录fn_rgb中的帧是否可用
    } status; // 保存输出状态
    std::shared_ptr<IFreamGetter> getter;
    FrameConvert *cvt = nullptr;
    ncnn::Mat mo;   // 存储中间帧的像素数据

    // 获取/创建RIFE模型实例
    void _initRIFE();
    // 创建帧转换器并设置宽和高
    void _initConverter(int width, int height);
    AVFrame* _makeMiddelFrame(double timestep);    // 获取f0和f1的中间帧
    AVFrame* _nextFrameProcess();      // 经过帧处理的补帧
    AVFrame* _nextFrameNoProcess();    // 直接补帧
};

#endif // RIFEFRAMEGETTER_HPP