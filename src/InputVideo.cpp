#include "InputVideo.hpp"

#include "Common.hpp"

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
            num_frames = v_stream->nb_frames;
            sar=v_stream->sample_aspect_ratio;
            if(sar.num<=0) sar={1,1};//TODO 可能bug？
            if(!num_frames) num_frames=v_stream->duration*av_q2d(fps)*av_q2d(v_stream->time_base);
        }else if(stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
            a_streams.push_back(stream);
        }
    }

    is_open=true;
}

void InputVideo::Print(){
    if(!is_open) OpenInput();
    AvLog("%dx%d(Rate:%.3lf,codec:%s)\n",width,height,av_q2d(fps),codec->long_name);
    AvLog("Duration:%d(base:%d/%d)\n",fmt_ctx->duration, 1, AV_TIME_BASE);
    AvLog("VStream Duration:%d(base:%d/%d)\n",v_stream->duration, v_stream->time_base.num, v_stream->time_base.den);
    for(int i=0; i<a_streams.size() ; ++i){
        auto& as=a_streams[i];
        AvLog("AStream%d Duration:%d(base:%d/%d)\n",i, as->duration, as->time_base.num, as->time_base.den);
    }
}