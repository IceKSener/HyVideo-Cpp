#ifndef RIFEFRAMEGETTER_HPP
#define RIFEFRAMEGETTER_HPP 1

#include "IFrameGetter.hpp"
#include "FrameConvert.hpp"
#include "ncnn/mat.h"
#include "rife/rife.h"
#include <map>

struct RifeArgs{
    bool use_gpu=false;
    std::string model="rife-v4.22-lite";
};
class RifeFrameGetter:public IFreamGetter{
private:
    std::string model;
    bool use_gpu;
    inline static std::map<std::string, RIFE*> _rifes;
    RIFE *rife=nullptr;         //不释放，防止多次引用
    FrameConvert *cvt=nullptr;
    IFreamGetter *getter;
    void _InitRIFE();
    void _InitConverter(AVFrame *fr);
    AVFrame *f0=nullptr,*f1=nullptr;
public:
    RifeFrameGetter(IFreamGetter* getter, const RifeArgs& args);
    ~RifeFrameGetter();
    AVFrame* NextFrame(AVFrame *fr=nullptr) override;
};

#endif // RIFEFRAMEGETTER_HPP