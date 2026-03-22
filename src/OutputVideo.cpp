#include "OutputVideo.hpp"

extern "C"{
    #include "libavutil/imgutils.h"
}

#include "utils/Assert.hpp"
#include "utils/Logger.hpp"

using namespace std;

OutputVideo::OutputVideo(OutputVideo&& vd)
    : path(vd.path)
    , is_init(vd.is_init), is_open(vd.is_open)
    , info(move(vd.info))
    , fmt_ctx(vd.fmt_ctx)
    , v_stream(vd.v_stream)
    , a_streams(move(vd.a_streams))
    , opt(vd.opt)
{
    vd.fmt_ctx=nullptr;
    vd.opt=nullptr;
}

OutputVideo::~OutputVideo() {
    if (fmt_ctx) {
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt_ctx->pb);
        avformat_free_context(fmt_ctx);
    }
    if (opt) av_dict_free(&opt);
}

OutputVideo& OutputVideo::copyVStreamParam(AVStream *vs_in) {
    AVCodecParameters *param = vs_in->codecpar;
    info.codec = avcodec_find_encoder(vs_in->codecpar->codec_id);
    info.width = param->width;
    info.height = param->height;
    info.pix_fmt = (AVPixelFormat)param->format;
    info.fps = vs_in->avg_frame_rate; 
    info.vs_timebase = vs_in->time_base;
    return *this;
}

OutputVideo& OutputVideo::initOutput() {
    if(is_init) return *this;
    if(!info.width || !info.height) ThrowErr("请指定输出视频分辨率");
    if(!info.fps.num) ThrowErr("请指定输出视频帧率");
    if(!info.codec) info.codec=avcodec_find_encoder(AV_CODEC_ID_H264);

    Assert(avformat_alloc_output_context2(&fmt_ctx, NULL, info.format.empty()?NULL:info.format.c_str(), path.c_str()));
    AssertP(v_stream = avformat_new_stream(fmt_ctx, NULL));

    v_stream->avg_frame_rate = info.fps;
    if (info.vs_timebase.num)
        v_stream->time_base = info.vs_timebase;
    else
        info.vs_timebase = v_stream->time_base = AV_TIME_BASE_Q;

    is_init = true;
    return *this;
}

OutputVideo& OutputVideo::openOutput() {
    if(is_open) return *this;
    if(!is_init) initOutput();

    if(!(fmt_ctx->oformat->flags&AVFMT_NOFILE))
        Assert(avio_open2(&fmt_ctx->pb, path.c_str(), AVIO_FLAG_WRITE, NULL, NULL));
    
    Assert(avformat_write_header(fmt_ctx, NULL));
    info.vs_timebase = v_stream->time_base;     // avformat_write_header后流的时间基被设置为正确的值

    is_open = true;
    return *this;
}

AVStream* OutputVideo::addAudio(const AVStream *input_audio) {
    if(is_open) ThrowErr("文件已开始写入，无法添加新的流");
    if(input_audio->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) return nullptr;
    if(!is_init) initOutput();
    AVStream *new_audio;
    AssertP(new_audio = avformat_new_stream(fmt_ctx, NULL));
    Assert(avcodec_parameters_copy(new_audio->codecpar, input_audio->codecpar));
    new_audio->time_base = input_audio->time_base;
    new_audio->codecpar->codec_tag = 0;     // 解决MP4转MKV时音频转换的错误
    a_streams.push_back(new_audio);
    return new_audio;
}

void OutputVideo::print() {
    if(!is_init) initOutput();
    AvLog("==============================\n");
    AvLog("File: %s\n", path.c_str());
    AvLog("%dx%d %.3lffps (%s)\n"
        , info.width, info.height, av_q2d(info.fps)
        , info.codec->long_name);
    AVCodecParameters *param = v_stream->codecpar;
    AvLog("FrameSize: %d bytes\n",av_image_get_buffer_size((AVPixelFormat)param->format,param->width,param->height,1));
    AvLog("==============================\n");
}