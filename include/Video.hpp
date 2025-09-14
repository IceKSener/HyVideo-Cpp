#ifndef VIDEO_HPP
#define VIDEO_HPP 1

#include <string>
#include <vector>
extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/imgutils.h"
}
#include "Common.hpp"

enum VideoMode{
    IN, OUT
};
class Video{
friend class Task;
friend class VideoFrameReader;
private:
    std::string _path;
    VideoMode _mode;
    bool _is_open = false;
    int width=0, height=0;
    AVRational fps={0,1};
    
    AVFormatContext *fmt_ctx = nullptr;
    AVStream* v_stream = nullptr;
    std::vector<AVStream*> a_streams;
    const AVCodec *codec = nullptr;
    AVPixelFormat pix_fmt;

    void OpenInput();
public:
    Video(const Video& v)=default;
    Video& operator=(const Video& v)=default;
    Video(std::string path, VideoMode mode=IN);
    Video(Video&& v);
    ~Video();

    void OpenOutput();
    void Print();

    //DEBUG
    AVStream* getVS(){ return v_stream; }
};

#endif //VIDEO_HPP