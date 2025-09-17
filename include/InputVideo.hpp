#ifndef INPUTVIDEO_HPP
#define INPUTVIDEO_HPP 1

#include <string>
#include <vector>
extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/imgutils.h"
}
#include "Common.hpp"

class InputVideo{
friend class OutputVideo;
friend class Task;
friend class VideoFrameReader;
friend class VideoFrameWriter;
private:
    std::string path;
    bool is_open = false;
    int width=0, height=0;
    AVRational fps={0,1};
    
    AVFormatContext *fmt_ctx = nullptr;
    AVStream* v_stream = nullptr;
    std::vector<AVStream*> a_streams;
    const AVCodec *codec = nullptr;
    AVPixelFormat pix_fmt;

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