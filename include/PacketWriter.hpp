#ifndef PACKETWRITER_HPP
#define PACKETWRITER_HPP 1

#include <unordered_map>

extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
}

#include "OutputVideo.hpp"
#include "InputVideo.hpp"
#include "data/HvFrame.hpp"

class PacketWriter{
public:
    PacketWriter(OutputVideo& vd);
    // 附带流索引映射
    PacketWriter& setMapping(const std::unordered_map<int,int> mapping);
    PacketWriter(PacketWriter&& pw);
    ~PacketWriter();
    //写入Packet
    PacketWriter& sendPacket(AVPacket *pkt, const AVRational* in_timebase = nullptr);
    //写入Frame，缓存在内部Packet中
    PacketWriter& sendVideoFrame(const HvFrame& fr);
    //写入文件结尾，并将缓存内容写入清空
    void writeEnd();
private:
    std::unordered_map<int,int> *stream_mapping = nullptr;
    AVPacket *pkt_buf = nullptr, *pkt_ref=nullptr;
    HvFrame fr_ref;
    AVFormatContext *fmt_ctx = nullptr;
    AVCodecContext *ctx = nullptr;
    AVRational out_timebase;
    bool is_end = false;
};

#endif // PACKETWRITER_HPP