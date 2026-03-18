#ifndef VIDEOFRAMEREADER_HPP
#define VIDEOFRAMEREADER_HPP 1

#include "IFrameGetter.hpp"
#include "InputVideo.hpp"
#include "PacketReader.hpp"

class VideoFrameReader:public IFreamGetter{
public:
    /*
    * 创建视频的帧读取类
    * @param vd 输入的视频
    * @param manual 为true时需要手动添加Packet
    */
   // 当设置manual为true时，需要手动传入packet
    VideoFrameReader(InputVideo& vd, bool manual=false);
    VideoFrameReader(VideoFrameReader&& vfr);
    ~VideoFrameReader();
    // 添加Packet，不会unref传入的pkt
    VideoFrameReader& addPacket(AVPacket *pkt);
    // 读取视频下一帧，读不到（结束或需要Packet）则返回nullptr
    AVFrame* nextFrame(AVFrame *fr=nullptr) override;
    bool isEnd() override{ return is_end; }
private:
    bool is_end = false;
    AVRational time_base = {0, 1};
    AVCodecContext *ctx = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *fr = nullptr;
    PacketReader *pkt_reader = nullptr;
};


#endif // VIDEOFRAMEREADER_HPP