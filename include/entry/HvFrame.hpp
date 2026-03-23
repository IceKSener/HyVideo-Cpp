#ifndef HVFRAME
#define HVFRAME

extern "C" {
    #include "libavutil/frame.h"
}

#include "utils/Assert.hpp"

class HvFrame {
public:
    AVFrame *fr = nullptr;
    inline HvFrame() {
        AssertP(fr = av_frame_alloc());
    }
    inline HvFrame(const HvFrame& f) {
        AssertP(fr = av_frame_alloc());
        av_frame_ref(fr, f.fr);
    }
    inline HvFrame& operator=(const HvFrame& f) {
        if (fr != f.fr) {
            av_frame_unref(fr);
            av_frame_ref(fr, f.fr);
        }
        return *this;
    }
    inline HvFrame(HvFrame&& f) {
        fr = f.fr;
        f.fr = nullptr;
    }
    inline HvFrame& operator=(HvFrame&& f) {
        if (fr != f.fr) {
            av_frame_unref(fr);
            av_frame_move_ref(fr, f.fr);
        }
        return *this;
    }
    inline ~HvFrame() {
        if (fr) av_frame_free(&fr);
    }
    inline bool isEmpty() {
        return fr->height <= 0;
    }
    inline void swap(HvFrame& f) {
        AVFrame* tmp = fr;
        fr = f.fr;
        f.fr = tmp;
    }
    inline HvFrame& createBuffer(int width, int height, AVPixelFormat format) {
        av_frame_unref(fr);
        fr->width = width;
        fr->height = height;
        fr->format = format;
        Assert(av_frame_get_buffer(fr, 0));
        return *this;
    }
};
#endif // HVFRAME