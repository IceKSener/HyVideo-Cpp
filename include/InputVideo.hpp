#ifndef INPUTVIDEO_HPP
#define INPUTVIDEO_HPP 1

extern "C"{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
}
#include <string>
#include <vector>

class InputVideo{
private:
    const std::string path;
    bool is_open = false;
    struct _info{
        int width=0, height=0;
        AVRational fps={0,1}, sar={1,1};
        int num_frames=0;    //不一定准确
        AVPixelFormat pix_fmt;
        const AVCodec *codec = nullptr;
    } info;
    
    AVFormatContext *fmt_ctx = nullptr;
    const AVStream* v_stream = nullptr;
    std::vector<const AVStream*> a_streams;
public:
    InputVideo(std::string path): path(path) {}
    InputVideo(InputVideo&& v);
    ~InputVideo();

    bool isOpen(){ return is_open; }
    void openInput();

    void print();
    const std::string& getPath() const{ return path; }
    const _info& getInfo() const{ return info; }
    AVFormatContext* getFormatContext(){ return fmt_ctx; }
    const AVStream* getVS() const{ return v_stream; }
    const std::vector<const AVStream*>& getASs() const{ return a_streams; }
};

#endif //INPUTVIDEO_HPP