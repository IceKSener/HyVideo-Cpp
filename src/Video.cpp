#include "Video.hpp"

using namespace std;
Video::Video(string path, VideoMode mode):_path(path),_mode(mode){
    if(mode==VideoMode::IN){
        OpenInput();
    }
}
Video::Video(Video&& vd){
    *this=vd;
    memset(&vd, 0, sizeof(vd));
}

Video::~Video(){
    if(fmt_ctx){
        if(_mode==VideoMode::IN) avformat_close_input(&fmt_ctx);
        else ;//TODO
    }
}

void Video::OpenInput(){
    Assert(avformat_open_input(&fmt_ctx, _path.c_str(), NULL, NULL));
    Assert(avformat_find_stream_info(fmt_ctx, NULL));

    for(int i=0 ; i<fmt_ctx->nb_streams ; ++i){
        AVStream *stream = fmt_ctx->streams[i];
        if(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
            v_stream = stream;
            width = stream->codecpar->width;
            height = stream->codecpar->height;
            fps = stream->avg_frame_rate;
            codec = avcodec_find_decoder(stream->codecpar->codec_id);
            pix_fmt = (AVPixelFormat)v_stream->codecpar->format;
        }else if(stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            a_streams.push_back(stream);
        }
    }

    _is_open=true;
}

void Video::OpenOutput(){
    if(_mode!=VideoMode::OUT || _is_open) return;
    return;
}

void Video::Print(){
    printf("%dx%d(Rate:%.3lf,codec:%s)\n",width,height,av_q2d(fps),codec->long_name);
    AVCodecParameters *param = v_stream->codecpar;
    printf("FrameSize:%d bytes\n",av_image_get_buffer_size((AVPixelFormat)param->format,param->width,param->height,1));
}