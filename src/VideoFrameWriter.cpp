#include "VideoFrameWriter.hpp"

using namespace std;

extern uint32_t cpu_num;

void VideoFrameWriter::Open(){
    if(is_open) return;
    AssertP(v_stream=avformat_new_stream(fmt_ctx, vd.codec));
    // v_stream->time_base={};
    vs_index=v_stream->index;

    int w=vd.width,h=vd.height;
    AssertP(ctx=avcodec_alloc_context3(vd.codec));
    ctx->width=w; ctx->height=h;
    ctx->codec_type=AVMEDIA_TYPE_VIDEO;
    ctx->framerate=vd.fps;
    ctx->pix_fmt=vd.pix_fmt;
    // ctx->thread_count=cpu_num;
    Assert(avcodec_open2(ctx, vd.codec, NULL));
    Assert(avcodec_parameters_from_context(v_stream->codecpar,ctx));
    av_dump_format(fmt_ctx, 0, vd.path.c_str(), 1);
    Assert(avformat_write_header(fmt_ctx,NULL));

    AssertP(pkt=av_packet_alloc());
    AssertP(fr=av_frame_alloc());
    fr->width=w, fr->height=h;
    fr->format=ctx->pix_fmt;
    Assert(av_frame_get_buffer(fr,16));
    is_open=true;
}
VideoFrameWriter::VideoFrameWriter(OutputVideo& vd, bool open):vd(vd){
    if(!vd.is_open) vd.OpenOutput();
    if(open) Open();
}
VideoFrameWriter::~VideoFrameWriter(){
    if(fr) av_frame_free(&fr);
    if(pkt) av_packet_free(&pkt);
    if(ctx) avcodec_free_context(&ctx);
}
bool VideoFrameWriter::_SendFrame(AVFrame *fr){
    Assert(avcodec_send_frame(ctx, fr));
    int ret;
    while(true){
        switch(ret=avcodec_receive_packet(ctx,pkt)){
            case 0:
                av_packet_rescale_ts(pkt,ctx->time_base,v_stream->time_base);
                pkt->stream_index=v_stream->index;
                Assert(av_interleaved_write_frame(fmt_ctx,pkt));
                av_packet_unref(pkt);
                break;
            case AVERROR(EAGAIN):
                return true;
            case AVERROR_EOF:
                av_write_trailer(fmt_ctx);
                return true;
            default:
                Assert(ret);
        }
    }
    return true;
}
AVFrame* VideoFrameWriter::WriteFrame(AVFrame *fr){
    if(!is_open) Open();
    _SendFrame(fr);
    return fr;
}
bool VideoFrameWriter::WriteEnd(){
    if(is_end) return false;
    _SendFrame(NULL);
    return is_end=true;
}