#ifndef PACKETREADER_HPP
#define PACKETREADER_HPP 1

#include "InputVideo.hpp"
extern "C"{
    #include "libavformat/avformat.h"
}

class PacketReader{
private:
    AVPacket *pkt;
    AVFormatContext *fmt_ctx;
public:
    PacketReader(InputVideo& vd);
    PacketReader(PacketReader&& pr);
    ~PacketReader();
    //读取文件的下一个Packet，结束则返回nullptr
    AVPacket* NextPacket(AVPacket *pkt=nullptr);
    //读取视频的下一个Packet，结束则返回nullptr
    AVPacket* NextVideoPacket(AVPacket *pkt=nullptr);
};


#endif // PACKETREADER_HPP