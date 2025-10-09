#ifndef FRAMECONVERT_HPP
#define FRAMECONVERT_HPP 1

extern "C"{
    #include "libswscale/swscale.h"
}

class FrameConvert{
private:
    SwsContext* sws_ctx=nullptr;
    const AVPixelFormat from,to;
public:
    FrameConvert(int w, int h, AVPixelFormat from, AVPixelFormat to);
    FrameConvert(FrameConvert&& fc);
    ~FrameConvert();
    AVFrame* Convert(AVFrame *fr);
};


#endif // FRAMECONVERT_HPP