#ifndef VIDEOFRAMEREADER_HPP
#define VIDEOFRAMEREADER_HPP 1

#include "Common.hpp"
#include "IFrameGetter.hpp"
#include "InputVideo.hpp"

class VideoFrameReader:public IFreamGetter{
private:
    AVFormatContext *fmt_ctx;
    AVCodecContext *ctx = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *fr = nullptr;
    int vs_index = -1;
    bool _NextVideoPacket();
public:
    VideoFrameReader(InputVideo& vd, int vs_index=-1);
    ~VideoFrameReader();
    AVFrame* NextFrame(AVFrame *fr=nullptr) override;
};


#endif // VIDEOFRAMEREADER_HPP