#ifndef OUTPUTVIDEO_HPP
#define OUTPUTVIDEO_HPP 1

#include "InputVideo.hpp"
extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
}
#include <string>
#include <vector>

class OutputVideo{
friend class Task;
friend class PacketWriter;
private:
    std::string path;
    bool is_init = false, is_open=false;
    int width=0, height=0;
    AVRational fps={0,1}, vs_timebase={0,1};
    std::string format = "mp4";
    AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;
    
    AVFormatContext *fmt_ctx = nullptr;
    AVStream* v_stream = nullptr;
    std::vector<AVStream*> a_streams;
    const AVCodec *codec = nullptr;
    AVDictionary *opt = nullptr;
public:
    OutputVideo(std::string path);
    OutputVideo(OutputVideo&& vd);
    ~OutputVideo();

    OutputVideo& CopyVStreamParam(AVStream *vs_in);
    OutputVideo& SetWxH(int width, int height){ this->width=width, this->height=height; return *this; }
    OutputVideo& SetFPS(AVRational fps){ this->fps=fps;return *this; }
    OutputVideo& SetFormat(std::string format){ this->format=format;return *this; }
    OutputVideo& SetEncoder(const AVCodec *encoder){ codec=encoder;return *this; }
    OutputVideo& SetVSTimebase(const AVRational& time_base){ vs_timebase=time_base; return *this; }
    OutputVideo& SetPixelFormat(AVPixelFormat pix_fmt){ this->pix_fmt=pix_fmt;return *this; }
    OutputVideo& SetOption(const std::string& key, int value){ av_dict_set_int(&opt,key.c_str(),value,0); return *this; }
    OutputVideo& AddAudio(const AVStream *input_audio);

    OutputVideo& InitOutput();
    OutputVideo& OpenOutput();
    OutputVideo& Print();

    //DEBUG
    AVStream* getVS(){ return v_stream; }
};

#endif //OUTPUTVIDEO_HPP