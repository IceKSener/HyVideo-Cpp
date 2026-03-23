#include "FrameConvert.hpp"

#include "utils/Assert.hpp"

using namespace std;

FrameConvert::FrameConvert(int dst_w, int dst_h, AVPixelFormat dst_f)
: dst_fmt({dst_w, dst_h, dst_f}) {}

FrameConvert::~FrameConvert() {
    for (auto& kv : sws_map) {
        sws_freeContext(kv.second);
    }
}

 HvFrame& FrameConvert::convert(const HvFrame& fr_in, HvFrame& fr_out) {
    AVFrame &fi=*fr_in.fr;
    const FrameFormat in_fmt = {fi.width, fi.height, (AVPixelFormat)fi.format};
    if (dst_fmt == in_fmt) return fr_out = fr_in;

    fr_buf.createBuffer(dst_fmt.width, dst_fmt.height, dst_fmt.format);
    
    SwsContext* sws_ctx;
    auto iter = sws_map.find(in_fmt);
    if (iter != sws_map.end()){
        sws_ctx = iter->second;
    } else {
        AssertP(sws_ctx = sws_getContext(
            in_fmt.width, in_fmt.height, in_fmt.format
            , dst_fmt.width, dst_fmt.height, dst_fmt.format
            , SWS_BILINEAR, NULL, NULL, NULL));
        sws_map[in_fmt] = sws_ctx;
    }
    
    sws_scale(sws_ctx, fi.data, fi.linesize, 0, fi.height, fr_buf.fr->data, fr_buf.fr->linesize);
    fr_out = move(fr_buf);
    av_frame_copy_props(fr_out.fr, fr_in.fr);
    return fr_out;
}
