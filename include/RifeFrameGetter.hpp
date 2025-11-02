#ifndef RIFEFRAMEGETTER_HPP
#define RIFEFRAMEGETTER_HPP 1

#include "IFrameGetter.hpp"
#include "FrameConvert.hpp"
#include "Score.hpp"
#include "ncnn/mat.h"
#include "rife/rife.h"
#include  <memory>
#include <map>

class RifeFrameGetter:public IFreamGetter{
private:
    inline static std::map<std::string, RIFE*> _rifes;
    std::string model;
    bool use_gpu;
    AVRational fpsx={1,1};
    std::optional<std::vector<int8_t>> process;
    RIFE *rife=nullptr;         //不释放，防止多次引用
    int fr_index=0,f0_index=0,f1_index=1;
    int f0_pts,f1_pts=0;
    bool is_end=false;
    FrameConvert *cvt=nullptr;
    std::shared_ptr<IFreamGetter> getter;

    void _InitRIFE();
    void _InitConverter(AVFrame *fr);
    AVFrame *f0=nullptr,*f1=nullptr,*f0_rgb=nullptr,*f1_rgb=nullptr,*fr=nullptr;
    ncnn::Mat *md=nullptr;
    bool rgb_valid[2]={false, false};
    //TODO 待优化
    RifeFrameGetter(const RifeFrameGetter& rfg) = default;
    RifeFrameGetter& operator=(const RifeFrameGetter& fc)=default;
    // 根据是否process决定使用的函数
    AVFrame *(RifeFrameGetter::*_NextFrame)()=&RifeFrameGetter::_NextFrameNoProecess;
    AVFrame* _NextFrameProecess();
    AVFrame* _NextFrameNoProecess();
public:
    struct Args{
        bool use_gpu=false;
        std::string model="rife-v4.22-lite";
    };

    RifeFrameGetter(const std::shared_ptr<IFreamGetter>& getter, const Args& args);
    RifeFrameGetter(RifeFrameGetter&& fc);
    ~RifeFrameGetter();
    RifeFrameGetter& SetFPSX(AVRational fpsx){ this->fpsx=fpsx;return *this; }
    RifeFrameGetter& SetProcess(bool process, const Score *score=nullptr);
    AVFrame* NextFrame(AVFrame *fr=nullptr) override;
    bool IsEnd() override{ return is_end; }
};

#endif // RIFEFRAMEGETTER_HPP