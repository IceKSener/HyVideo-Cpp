#include "FrameConvert.hpp"

#include "utils/Assert.hpp"

using namespace std;

FrameConvert::FrameConvert(int dst_w, int dst_h, AVPixelFormat dst_f): dst_format({dst_w,dst_h,dst_f}) {
    AssertP(fr_buf = av_frame_alloc());
    fr_buf->width=dst_w;
    fr_buf->height=dst_h;
    fr_buf->format=dst_f;
    Assert(av_frame_get_buffer(fr_buf, 0));
}

FrameConvert::FrameConvert(FrameConvert &&fc)
    : dst_format(fc.dst_format)
    , sws_map(move(fc.sws_map))
    , fr_buf(fc.fr_buf)
{
    fc.fr_buf=nullptr;
}

FrameConvert::~FrameConvert() {
    if (fr_buf) av_frame_free(&fr_buf);
    for (auto& kv : sws_map) {
        sws_freeContext(kv.second);
    }
}

AVFrame *FrameConvert::convert(AVFrame *fr, AVFrame *fr_out) {
    if (!fr_out) fr_out = fr_buf;
    FrameFormat in_fmt = {fr->width, fr->height, (AVPixelFormat)fr->format};
    if(dst_format == in_fmt) return fr;
    SwsContext* sws_ctx;
    
    auto iter = sws_map.find(in_fmt);
    if (iter != sws_map.end()){
        sws_ctx = iter->second;
    } else {
        AssertP(sws_ctx = sws_getContext(fr->width, fr->height
            , in_fmt.format, dst_format.width
            , dst_format.height, dst_format.format
            , SWS_BILINEAR, NULL, NULL, NULL));
        sws_map[in_fmt] = sws_ctx;
    }
    
    sws_scale(sws_ctx, fr->data, fr->linesize, 0, fr->height,fr_out->data, fr_out->linesize);
    av_frame_copy_props(fr_out, fr);
    return fr_out;
}
