#include "VideoFrameReader.hpp"

using namespace std;

extern uint32_t cpu_num;

bool VideoFrameReader::_NextVideoPacket(){
    int ret;
    while(true){
        ret=av_read_frame(fmt_ctx, pkt);
        if(pkt->stream_index!=vs_index) {
            av_packet_unref(pkt);
            continue;
        }
        Assert(avcodec_send_packet(ctx,pkt));
        av_packet_unref(pkt);
        if(ret==AVERROR_EOF) return false;
        return true;
    }
}

VideoFrameReader::VideoFrameReader(InputVideo& vd){
    if(!vd.is_open) vd.OpenInput();
    fmt_ctx=vd.fmt_ctx;
    vs_index=vd.v_stream->index;
    AssertP(ctx=avcodec_alloc_context3(vd.codec));
    Assert(avcodec_parameters_to_context(ctx, vd.v_stream->codecpar));
    ctx->thread_count=cpu_num;
    Assert(avcodec_open2(ctx, vd.codec, NULL));
    AssertP(pkt=av_packet_alloc());
    AssertP(fr=av_frame_alloc());
    //DEBUG
    av_log(NULL, AV_LOG_INFO, "视频元数据[帧数:%d]\n",vd.v_stream->nb_frames);
}
VideoFrameReader::~VideoFrameReader(){
    if(fr) av_frame_free(&fr);
    if(pkt) av_packet_free(&pkt);
    if(ctx) avcodec_free_context(&ctx);
}

AVFrame* VideoFrameReader::NextFrame(AVFrame *fr){
    if(!fr) fr=this->fr;
    int ret;
    while(true){
        switch(ret=avcodec_receive_frame(ctx,fr)){
        case AVERROR(EAGAIN):
            _NextVideoPacket();
            continue;
        case 0:
            return fr;
        case AVERROR_EOF:
            return nullptr;
        default:
            Assert(ret);
        }
    }
    return fr;
}