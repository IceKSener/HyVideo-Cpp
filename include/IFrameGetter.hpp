#ifndef IFRAMEGETTER_HPP
#define IFRAMEGETTER_HPP 1

extern "C"{
    #include "libavcodec/avcodec.h"
}

class IFreamGetter{
public:
    virtual AVFrame* NextFrame(AVFrame*) = 0;
};

#endif //IFRAMEGETTER_HPP