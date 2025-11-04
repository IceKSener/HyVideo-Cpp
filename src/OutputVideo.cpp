#include "OutputVideo.hpp"
#include "Common.hpp"
extern "C"{
    #include "libavutil/imgutils.h"
}

using namespace std;
OutputVideo::OutputVideo(string path):path(path){ }
OutputVideo::OutputVideo(OutputVideo&& vd){
    path=std::move(vd.path);
    is_init=vd.is_init; is_open=vd.is_open;
    width=vd.width; height=vd.height;
    fps=vd.fps; vs_timebase=vd.vs_timebase;
    format=std::move(vd.format);
    pix_fmt=vd.pix_fmt;
    fmt_ctx=vd.fmt_ctx; vd.fmt_ctx=nullptr;
    v_stream=vd.v_stream;
    a_streams=std::move(vd.a_streams);
    codec=vd.codec;
    opt=vd.opt; vd.opt=nullptr;
}

OutputVideo::~OutputVideo(){
    if(fmt_ctx){
        if(!(fmt_ctx->oformat->flags&AVFMT_NOFILE))
            avio_closep(&fmt_ctx->pb);
        avformat_free_context(fmt_ctx);
    }
    if(opt) av_dict_free(&opt);
}

OutputVideo& OutputVideo::CopyVStreamParam(AVStream *vs_in){
    AVCodecParameters *param = vs_in->codecpar;
    width=param->width; height=param->height;
    pix_fmt=(AVPixelFormat)param->format;
    fps=vs_in->avg_frame_rate; 
    vs_timebase=vs_in->time_base;
    return *this;
}
OutputVideo& OutputVideo::InitOutput(){
    if(is_init) return *this;
    if(!width || !height)ThrowErr("请指定输出视频分辨率");
    if(!fps.num)ThrowErr("请指定输出视频帧率");
    if(!codec) codec=avcodec_find_encoder(AV_CODEC_ID_H264);

    Assert(avformat_alloc_output_context2(&fmt_ctx,NULL,format.empty()?NULL:format.c_str(),path.c_str()));
    AssertP(v_stream=avformat_new_stream(fmt_ctx, NULL));

    // AVCodecParameters& codecpar = *v_stream->codecpar;
    // codecpar.codec_id=codec->id;
    // codecpar.codec_type=AVMEDIA_TYPE_VIDEO;
    // codecpar.width=width; codecpar.height=height;
    // codecpar.format=pix_fmt;
    // codecpar.bit_rate=0;
    v_stream->avg_frame_rate=fps;
    v_stream->time_base=vs_timebase;

    is_init = true;
    return *this;
}
OutputVideo& OutputVideo::OpenOutput(){
    if(is_open) return *this;
    if(!is_init) InitOutput();

    if(!(fmt_ctx->oformat->flags&AVFMT_NOFILE))
        Assert(avio_open2(&fmt_ctx->pb, path.c_str(), AVIO_FLAG_WRITE, NULL, NULL));
    
    Assert(avformat_write_header(fmt_ctx, NULL));

    is_open = true;
    return *this;
}

OutputVideo &OutputVideo::AddAudio(const AVStream *input_audio){
    if(input_audio->codecpar->codec_type!=AVMEDIA_TYPE_AUDIO) return *this;
    if(!is_init) InitOutput();
    AVStream *new_audio;
    AssertP(new_audio=avformat_new_stream(fmt_ctx, NULL));
    Assert(avcodec_parameters_copy(new_audio->codecpar, input_audio->codecpar));
    new_audio->time_base=input_audio->time_base;
    a_streams.push_back(new_audio);
    return *this;
}

OutputVideo& OutputVideo::Print(){
    if(!is_init) InitOutput();
    printf("%dx%d(Rate:%.3lf,codec:%s)\n",width,height,av_q2d(fps),codec->long_name);
    AVCodecParameters *param = v_stream->codecpar;
    printf("FrameSize:%d bytes\n",av_image_get_buffer_size((AVPixelFormat)param->format,param->width,param->height,1));
    return *this;
}