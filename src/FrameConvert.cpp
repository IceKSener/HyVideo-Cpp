#include "FrameConvert.hpp"
#include "Common.hpp"
extern "C"{
    #include "libavutil/imgutils.h"
}

using namespace std;

FrameConvert::FrameConvert(int w, int h, AVPixelFormat from, AVPixelFormat to):from(from),to(to){
    if(from!=to)
        AssertP(sws_ctx=sws_getContext(w,h,from,w,h,to,SWS_BILINEAR,NULL,NULL,NULL));
}

FrameConvert::FrameConvert(FrameConvert &&fc):from(fc.from),to(fc.to){
    sws_ctx=fc.sws_ctx;
    fc.sws_ctx=nullptr;
}

FrameConvert::~FrameConvert(){
    if(sws_ctx) sws_freeContext(sws_ctx);
}

AVFrame *FrameConvert::Convert(AVFrame *fr){
    if(!sws_ctx) return fr;
    AVFrame *dst;
    AssertP(dst=av_frame_alloc());
    dst->width=fr->width;
    dst->height=fr->height;
    dst->format=to;
    Assert(av_frame_get_buffer(dst,1));
    sws_scale(sws_ctx,fr->data, fr->linesize, 0, fr->height,dst->data, dst->linesize);
    return dst;
}
