#include "PacketWriter.hpp"
#include "Common.hpp"
using namespace std;

PacketWriter::PacketWriter(OutputVideo &vd){
    AssertP(pkt_buf=av_packet_alloc());
    AssertP(pkt_ref=av_packet_alloc());
    fmt_ctx=vd.fmt_ctx;

    AssertP(ctx=avcodec_alloc_context3(vd.codec));
    ctx->width=vd.width;
    ctx->height=vd.height;
    ctx->pix_fmt=vd.pix_fmt;
    ctx->time_base=vd.vs_timebase;
    ctx->framerate=vd.fps;
    ctx->thread_count=GlobalConfig.cpu_num;
    if(fmt_ctx->oformat->flags&AVFMT_GLOBALHEADER)
        ctx->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    Assert(avcodec_open2(ctx, vd.codec, &vd.opt));
    Assert(avcodec_parameters_from_context(vd.v_stream->codecpar, ctx));
    dst_timebase=vd.vs_timebase;
    if(!vd.is_open) vd.OpenOutput();
}
PacketWriter::PacketWriter(OutputVideo &vd, InputVideo &vd_in):PacketWriter(vd){
    int as_sz=min(vd_in.a_streams.size(), vd.a_streams.size());
    stream_mapping = new int[as_sz+1];
    stream_mapping[vd_in.v_stream->index]=vd.v_stream->index;
    for(int i=0 ; i<as_sz ; ++i){
        stream_mapping[vd_in.a_streams[i]->index] = vd.a_streams[i]->index;
    }
}
PacketWriter::PacketWriter(PacketWriter &&pw){
    stream_mapping=pw.stream_mapping; pw.stream_mapping=nullptr;
    pkt_buf=pw.pkt_buf; pw.pkt_buf=nullptr;
    pkt_ref=pw.pkt_ref; pw.pkt_ref=nullptr;
    fmt_ctx=pw.fmt_ctx;
    ctx=pw.ctx; pw.ctx=nullptr;
    dst_timebase=pw.dst_timebase;
}
PacketWriter::~PacketWriter()
{
    if(ctx) avcodec_free_context(&ctx);
    if(pkt_buf) av_packet_free(&pkt_buf);
    if(pkt_ref) av_packet_free(&pkt_ref);
    if(stream_mapping) delete[] stream_mapping;
}

PacketWriter& PacketWriter::SendPacket(AVPacket *pkt){
    Assert(av_packet_ref(pkt_ref, pkt));
    if(stream_mapping) pkt_ref->stream_index=stream_mapping[pkt_ref->stream_index];
    Assert(av_interleaved_write_frame(fmt_ctx, pkt_ref));
    return *this;
}

PacketWriter& PacketWriter::SendVideoFrame(AVFrame *fr){
    AvLog("%06X\n",*(uint32_t*)(fr->data[0]+0x60000));
    Assert(avcodec_send_frame(ctx, fr));
    int ret;
    while(true){
        switch(ret=avcodec_receive_packet(ctx,pkt_buf)){
            case 0:
                av_packet_rescale_ts(pkt_buf,ctx->time_base,dst_timebase);
                SendPacket(pkt_buf);
                break;
            case AVERROR(EAGAIN):
                return *this;
            case AVERROR_EOF:
                WriteEnd();
                return *this;
            default:
                Assert(ret);
        }
    }
    return *this;
}

PacketWriter& PacketWriter::WriteEnd(){
    Assert(avcodec_send_frame(ctx,NULL));
    int ret;
    while(true){
        switch(ret=avcodec_receive_packet(ctx,pkt_buf)){
            case 0:
                av_packet_rescale_ts(pkt_buf,ctx->time_base,dst_timebase);
                SendPacket(pkt_buf);
                break;
            case AVERROR_EOF:
                Assert(av_write_trailer(fmt_ctx));
                return *this;
            default:
                Assert(ret);
        }
    }
    Assert(av_write_trailer(fmt_ctx));
    return *this;
}
