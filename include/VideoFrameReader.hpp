#ifndef VIDEOFRAMEREADER_HPP
#define VIDEOFRAMEREADER_HPP 1

#include "IFrameGetter.hpp"
#include "InputVideo.hpp"
#include "PacketReader.hpp"

class VideoFrameReader:public IFreamGetter{
private:
    bool is_end=false;
    AVCodecContext *ctx = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *fr = nullptr;
    PacketReader *pkt_reader = nullptr;
    
    VideoFrameReader(const VideoFrameReader& vfr)=default;
    VideoFrameReader& operator=(const VideoFrameReader& vfr)=default;
public:
    /*
    * 创建视频的帧读取类
    * @param vd 输入的视频
    * @param manual 为true时需要手动添加Packet
    */
    VideoFrameReader(InputVideo& vd, bool manual=false);
    VideoFrameReader(VideoFrameReader&& vfr);
    ~VideoFrameReader();
    // 添加Packet，不会unref传入的pkt
    VideoFrameReader& AddPacket(AVPacket *pkt);
    // 读取视频下一帧，读不到（结束或需要Packet）则返回nullptr
    AVFrame* NextFrame(AVFrame *fr=nullptr) override;
};


#endif // VIDEOFRAMEREADER_HPP