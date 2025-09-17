#ifndef OUTPUTVIDEO_HPP
#define OUTPUTVIDEO_HPP 1

#include <string>
#include <vector>
extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/imgutils.h"
}
#include "Common.hpp"
#include "InputVideo.hpp"

class OutputVideo{
friend class Task;
friend class VideoFrameWriter;
private:
    std::string path;
    bool is_open = false;
    int width=0, height=0;
    AVRational fps={0,1};
    std::string format;
    
    AVFormatContext *fmt_ctx = nullptr;
    AVStream* v_stream = nullptr;
    std::vector<AVStream*> a_streams;
    const AVCodec *codec = nullptr;
    AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;
    
    OutputVideo(const OutputVideo& v)=default;
    OutputVideo& operator=(const OutputVideo& v)=default;
public:
    OutputVideo(std::string path);
    OutputVideo(OutputVideo&& vd);
    ~OutputVideo();

    OutputVideo& CopyVideoParam(InputVideo& vd);
    OutputVideo& setWxH(int width, int height);
    OutputVideo& setFPS(AVRational fps){ this->fps=fps;return *this; }
    OutputVideo& setFormat(std::string format){ this->format=format;return *this; }
    OutputVideo& setEncoder(const AVCodec *encoder){ codec=encoder;return *this; }
    void OpenOutput();
    void Print();

    //DEBUG
    AVStream* getVS(){ return v_stream; }
};

#endif //OUTPUTVIDEO_HPP