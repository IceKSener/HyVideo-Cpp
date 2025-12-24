#ifndef IFRAMEGETTER_HPP
#define IFRAMEGETTER_HPP 1

extern "C"{
    #include "libavcodec/avcodec.h"
}

class IFreamGetter{
public:
    virtual AVFrame* NextFrame(AVFrame *fr=nullptr) = 0;
    //获取下一帧为null时使用IsEnd查看是否结束
    virtual bool IsEnd() = 0;
};

#endif //IFRAMEGETTER_HPP