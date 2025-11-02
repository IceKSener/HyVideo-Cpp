#ifndef FRAMECONVERT_HPP
#define FRAMECONVERT_HPP 1

extern "C"{
    #include "libswscale/swscale.h"
}
#include <unordered_map>

class FrameConvert{
private:
    struct FrameFormat{
        int width;
        int height;
        AVPixelFormat format;
        bool operator==(const FrameFormat& other) const {
            return width == other.width && height == other.height && format == other.format;
        }
    };
    struct FormatHash {
        size_t operator()(const FrameFormat& k) const {
            return (size_t)k.width ^ ((size_t)k.height << 16) ^ ((size_t)k.format << 24);
        }
    };
    
    std::unordered_map<FrameFormat,SwsContext*,FormatHash> sws_map;
    AVFrame *fr_buf=nullptr;
    const FrameFormat dst_format;

    FrameConvert(const FrameConvert& fc)=default;
    FrameConvert& operator=(const FrameConvert& fc)=default;
public:
    FrameConvert(int dst_w, int dst_h, AVPixelFormat dst_f);
    FrameConvert(FrameConvert&& fc);
    ~FrameConvert();
    AVFrame* Convert(AVFrame *fr, AVFrame *fr_out=nullptr);
};


#endif // FRAMECONVERT_HPP