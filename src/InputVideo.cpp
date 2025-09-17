#include "InputVideo.hpp"

using namespace std;
InputVideo::InputVideo(string path):path(path){ }
InputVideo::InputVideo(InputVideo&& vd){
    *this=vd;
    memset(&vd, 0, sizeof(vd));
}

InputVideo::~InputVideo(){
    if(fmt_ctx) avformat_close_input(&fmt_ctx);
}
void InputVideo::OpenInput(){
    Assert(avformat_open_input(&fmt_ctx, path.c_str(), NULL, NULL));
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

    is_open=true;
}

void InputVideo::Print(){
    if(!is_open) OpenInput();
    printf("%dx%d(Rate:%.3lf,codec:%s)\n",width,height,av_q2d(fps),codec->long_name);
    AVCodecParameters *param = v_stream->codecpar;
    printf("FrameSize:%d bytes\n",av_image_get_buffer_size((AVPixelFormat)param->format,param->width,param->height,1));
}