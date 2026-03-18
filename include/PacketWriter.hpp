#ifndef PACKETWRITER_HPP
#define PACKETWRITER_HPP 1

extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
}

#include "OutputVideo.hpp"
#include "InputVideo.hpp"

class PacketWriter{
public:
    PacketWriter(OutputVideo& vd);
    // 附带流索引映射
    PacketWriter(OutputVideo& vd, InputVideo& vd_in);
    PacketWriter(PacketWriter&& pw);
    ~PacketWriter();
    //写入Packet
    PacketWriter& sendPacket(AVPacket *pkt, const AVRational* in_timebase = nullptr);
    //写入Frame，缓存在内部Packet中
    PacketWriter& sendVideoFrame(const AVFrame *fr);
    //写入文件结尾，并将缓存内容写入清空
    PacketWriter& writeEnd();
private:
    int *stream_mapping = nullptr;
    AVPacket *pkt_buf = nullptr, *pkt_ref=nullptr;
    AVFrame *fr_ref = nullptr;
    AVFormatContext *fmt_ctx = nullptr;
    AVCodecContext *ctx = nullptr;
    AVRational out_timebase;
};

#endif // PACKETWRITER_HPP