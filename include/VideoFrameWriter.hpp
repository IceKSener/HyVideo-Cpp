#ifndef VIDEOFRAMEWRITER_HPP
#define VIDEOFRAMEWRITER_HPP 1

#include "Common.hpp"
#include "OutputVideo.hpp"

class VideoFrameWriter{
private:
    bool is_open=false, is_end=false;
    AVFormatContext *fmt_ctx;
    AVCodecContext *ctx = nullptr;
    AVStream *v_stream = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *fr = nullptr;
    int vs_index = -1;
    OutputVideo& vd;
    bool _SendFrame(AVFrame *fr);
public:
    void Open();
    VideoFrameWriter(OutputVideo& vd, bool open=true);
    ~VideoFrameWriter();
    AVFrame* WriteFrame(AVFrame *fr);
    bool WriteEnd();
};


#endif // VIDEOFRAMEWRITER_HPP