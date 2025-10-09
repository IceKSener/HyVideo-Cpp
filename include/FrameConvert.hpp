#ifndef FRAMECONVERT_HPP
#define FRAMECONVERT_HPP 1

extern "C"{
    #include "libswscale/swscale.h"
}

class FrameConvert{
private:
    SwsContext* sws_ctx=nullptr;
    AVFrame *fr_buf=nullptr;
    AVPixelFormat from,to;
    int iw,ih,ow,oh;

    FrameConvert(const FrameConvert& fc)=default;
    FrameConvert& operator=(const FrameConvert& fc)=default;
public:
    FrameConvert(int w, int h, AVPixelFormat from, AVPixelFormat to);
    FrameConvert(int iw, int ih, int ow, int oh, AVPixelFormat from, AVPixelFormat to);
    FrameConvert(FrameConvert&& fc);
    ~FrameConvert();
    AVFrame* Convert(AVFrame *fr);
};


#endif // FRAMECONVERT_HPP