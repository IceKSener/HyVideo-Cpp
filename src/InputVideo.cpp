#include "InputVideo.hpp"

#include "utils/Assert.hpp"
#include "utils/Logger.hpp"

using namespace std;

InputVideo::InputVideo(InputVideo&& vd)
    : path(vd.path)
    , is_open(vd.is_open)
    , info(move(vd.info))
    , fmt_ctx(vd.fmt_ctx)
    , v_stream(vd.v_stream)
    , a_streams(move(vd.a_streams))
{
    vd.fmt_ctx=nullptr;
}

InputVideo::~InputVideo() {
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
}

void InputVideo::openInput() {
    Assert(avformat_open_input(&fmt_ctx, path.c_str(), NULL, NULL));
    Assert(avformat_find_stream_info(fmt_ctx, NULL));

    for (int i=0 ; i<fmt_ctx->nb_streams ; ++i) {
        AVStream *stream = fmt_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            v_stream = stream;
            info.width = stream->codecpar->width;
            info.height = stream->codecpar->height;
            info.fps = stream->avg_frame_rate;
            info.codec = avcodec_find_decoder(stream->codecpar->codec_id);
            info.pix_fmt = (AVPixelFormat)v_stream->codecpar->format;
            info.num_frames = v_stream->nb_frames;
            info.sar=v_stream->sample_aspect_ratio;
            if (info.sar.num<=0) info.sar = {1,1};// TODO 可能bug？
            if (!info.num_frames) info.num_frames = v_stream->duration*av_q2d(info.fps)*av_q2d(v_stream->time_base);
        } else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            a_streams.push_back(stream);
        }
    }

    is_open=true;
}

void InputVideo::print(){
    if(!is_open) openInput();
    AvLog("==============================\n");
    AvLog("File: %s\n", path.c_str());
    AvLog("%dx%d %.3lffps (%s)\n"
        , info.width, info.height, av_q2d(info.fps)
        , info.codec->long_name);
    AvLog("Duration: %d(base:%d/%d - %.2lfs)\n", fmt_ctx->duration, 1, AV_TIME_BASE, (double)fmt_ctx->duration/AV_TIME_BASE);
    AvLog("VStream Duration: %d(base:%d/%d - %.2lfs)\n", v_stream->duration
        , v_stream->time_base.num, v_stream->time_base.den, v_stream->duration*av_q2d(v_stream->time_base));
    for(int i=0; i<a_streams.size() ; ++i){
        auto& as=a_streams[i];
        AvLog("AStream#%d Duration: %d(base:%d/%d - %.2lfs)\n",i, as->duration
            , as->time_base.num, as->time_base.den, as->duration*av_q2d(as->time_base));
    }
    AvLog("==============================\n");
}
