#ifndef OUTPUTVIDEO_HPP
#define OUTPUTVIDEO_HPP 1

#include <string>
#include <vector>

extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
}

#include "InputVideo.hpp"

class OutputVideo{
public:
    OutputVideo(std::string path): path(path) {}
    OutputVideo(OutputVideo&& vd);
    ~OutputVideo();

    // 从视频流复制参数
    OutputVideo& copyVStreamParam(AVStream *vs_in);
    OutputVideo& setWxH(int width, int height){ info.width=width, info.height=height; return *this; }
    OutputVideo& setFPS(AVRational fps){ info.fps=fps; return *this; }
    OutputVideo& setFormat(std::string format){ info.format=format; return *this; }
    OutputVideo& setEncoder(const AVCodec *encoder){ info.codec=encoder; return *this; }
    OutputVideo& setVSTimebase(const AVRational& time_base){ info.vs_timebase=time_base; return *this; }
    OutputVideo& setPixelFormat(AVPixelFormat pix_fmt){ info.pix_fmt=pix_fmt; return *this; }
    OutputVideo& setOption(const char* key, int value){ av_dict_set_int(&opt,key,value,0); return *this; }
    OutputVideo& setOption(const char* key, const char* value){ av_dict_set(&opt,key,value,0); return *this; }
    // 复制音频流，返回输出视频的音频流
    AVStream* addAudio(const AVStream *input_audio);

    // 利用设置好的信息创建上下文
    OutputVideo& initOutput();
    // 创建输出文件，准备写入数据（仅由PacketWriter调用）
    OutputVideo& openOutput();
    bool isInit(){ return is_init; }
    bool isOpen(){ return is_open; }
    void print();

    const std::string& getPath() const{ return path; }
    struct _info;
    const _info& getInfo() const{ return info; }
    AVFormatContext* getFormatContext(){ return fmt_ctx; }
    AVStream* getVS(){ return v_stream; }
    std::vector<AVStream*>& getASs(){ return a_streams; }
    AVDictionary*& getOption(){ return opt; }
    AVRational getVSTimebase(){ return v_stream ? v_stream->time_base : av_make_q(0, 1); }
private:
    const std::string path;
    bool is_init = false, is_open=false;
    struct _info {
        int width=0, height=0;
        AVRational fps={0,1}, vs_timebase={0,1};
        std::string format = "mp4";
        AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;
        const AVCodec *codec = nullptr;
    } info;
    
    AVFormatContext *fmt_ctx = nullptr;
    AVStream* v_stream = nullptr;
    std::vector<AVStream*> a_streams;
    AVDictionary *opt = nullptr;
};

#endif //OUTPUTVIDEO_HPP