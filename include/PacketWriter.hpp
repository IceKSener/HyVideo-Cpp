#ifndef PACKETWRITER_HPP
#define PACKETWRITER_HPP 1

#include "OutputVideo.hpp"
#include "InputVideo.hpp"
extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
}

class PacketWriter{
private:
    int *stream_mapping = nullptr;
    AVPacket *pkt_buf = nullptr, *pkt_ref=nullptr;
    AVFormatContext *fmt_ctx = nullptr;
    AVCodecContext *ctx = nullptr;
    AVRational dst_timebase;

    PacketWriter(const PacketWriter& v)=default;
    PacketWriter& operator=(const PacketWriter& v)=default;
public:
    PacketWriter(OutputVideo& vd);
    PacketWriter(OutputVideo& vd, InputVideo& vd_in);
    PacketWriter(PacketWriter&& w);
    ~PacketWriter();
    //写入Packet
    PacketWriter& SendPacket(AVPacket *pkt);
    //写入Frame，缓存在内部Packet中
    PacketWriter& SendVideoFrame(AVFrame *fr);
    //写入文件结尾，并将缓存内容写入清空
    PacketWriter& WriteEnd();
};

#endif // PACKETWRITER_HPP