#ifndef FRAMECONVERT_HPP
#define FRAMECONVERT_HPP 1

#include <unordered_map>

extern "C"{
    #include "libswscale/swscale.h"
}

#include "entry/HvFrame.hpp"

class FrameConvert {
public:
    // 指定输出帧的格式
    FrameConvert(int dst_w, int dst_h, AVPixelFormat dst_f);
    FrameConvert(FrameConvert&& fc) = default;
    ~FrameConvert();
    HvFrame& convert(const HvFrame& fr_in, HvFrame& fr_out);
private:
    struct FrameFormat {
        int width;
        int height;
        AVPixelFormat format;
        bool operator==(const FrameFormat& other) const{
            return width==other.width && height==other.height && format==other.format;
        }
    };
    struct FormatHash {
        size_t operator()(const FrameFormat& k) const{
            return (size_t)k.width ^ ((size_t)k.height<<16) ^ ((size_t)k.format<<24);
        }
    };
    
    std::unordered_map<FrameFormat,SwsContext*,FormatHash> sws_map;
    HvFrame fr_buf;
    const FrameFormat dst_fmt;
};


#endif // FRAMECONVERT_HPP