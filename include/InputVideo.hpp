#ifndef INPUTVIDEO_HPP
#define INPUTVIDEO_HPP 1

extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
}
#include <string>
#include <vector>

class InputVideo{
friend class OutputVideo;
friend class Task;
friend class PacketReader;
friend class PacketWriter;
friend class VideoFrameReader;
private:
    std::string path;
    bool is_open = false;
    int width=0, height=0;
    AVRational fps={0,1}, sar={1,1};
    int num_frames=0;    //不一定准确
    AVPixelFormat pix_fmt;
    
    AVFormatContext *fmt_ctx = nullptr;
    AVStream* v_stream = nullptr;
    std::vector<AVStream*> a_streams;
    const AVCodec *codec = nullptr;

    InputVideo(const InputVideo& v)=default;
    InputVideo& operator=(const InputVideo& v)=default;
public:
    InputVideo(std::string path);
    InputVideo(InputVideo&& v);
    ~InputVideo();

    void OpenInput();

    void Print();
    //DEBUG
    AVStream* getVS(){ return v_stream; }
};

#endif //INPUTVIDEO_HPP