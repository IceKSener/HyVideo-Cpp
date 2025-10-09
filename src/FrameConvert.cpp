#include "FrameConvert.hpp"
#include "Common.hpp"
// extern "C"{
//     #include "libavutil/imgutils.h"
// }

using namespace std;

FrameConvert::FrameConvert(int w, int h, AVPixelFormat from, AVPixelFormat to):FrameConvert(w,h,w,h,from,to){}

FrameConvert::FrameConvert(int iw, int ih, int ow, int oh, AVPixelFormat from, AVPixelFormat to){
    if(from!=to || iw!=ow || ih!=oh){
        AssertP(sws_ctx=sws_getContext(iw,ih,from,ow,oh,to,SWS_BILINEAR,NULL,NULL,NULL));
        AssertP(fr_buf=av_frame_alloc());
        fr_buf->width=ow;
        fr_buf->height=oh;
        fr_buf->format=to;
        Assert(av_frame_get_buffer(fr_buf, 0));
    }
}

FrameConvert::FrameConvert(FrameConvert &&fc):from(fc.from),to(fc.to){
    *this=fc;
    memset(&fc, 0, sizeof(fc));
}

FrameConvert::~FrameConvert(){
    if(fr_buf) av_frame_free(&fr_buf);
    if(sws_ctx) sws_freeContext(sws_ctx);
}

AVFrame *FrameConvert::Convert(AVFrame *fr){
    if(!sws_ctx) return fr;
    sws_scale(sws_ctx,fr->data, fr->linesize, 0, fr->height,fr_buf->data, fr_buf->linesize);
    av_frame_copy_props(fr_buf,fr);
    return fr_buf;
}
