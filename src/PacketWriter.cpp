#include "PacketWriter.hpp"
#include "Common.hpp"

using namespace std;

PacketWriter::PacketWriter(OutputVideo &vd){
    if(!vd.is_open) vd.OpenOutput();
    AssertP(pkt_buf=av_packet_alloc());
    AssertP(pkt_ref=av_packet_alloc());
    fmt_ctx=vd.fmt_ctx;

    AssertP(ctx=avcodec_alloc_context3(vd.codec));
    Assert(avcodec_parameters_to_context(ctx, vd.v_stream->codecpar));
    dst_timebase=ctx->time_base=vd.vs_timebase;
    Assert(avcodec_open2(ctx, vd.codec, &vd.opt));
}
PacketWriter::PacketWriter(OutputVideo &vd, InputVideo &vd_in):PacketWriter(vd){
    int as_sz=min(vd_in.a_streams.size(), vd.a_streams.size());
    stream_mapping = new int[as_sz+1];
    stream_mapping[vd_in.v_stream->index]=vd.v_stream->index;
    for(int i=0 ; i<as_sz ; ++i){
        stream_mapping[vd_in.a_streams[i]->index] = vd.a_streams[i]->index;
    }
}
PacketWriter::PacketWriter(PacketWriter &&w){
    *this=w;
    memset(&w, 0, sizeof(w));
}
PacketWriter::~PacketWriter()
{
    if(ctx) avcodec_free_context(&ctx);
    if(pkt_buf) av_packet_free(&pkt_buf);
    if(pkt_ref) av_packet_free(&pkt_ref);
    if(stream_mapping) delete[] stream_mapping;
}

PacketWriter& PacketWriter::SendPacket(AVPacket *pkt){
    if(stream_mapping) pkt->stream_index=stream_mapping[pkt->stream_index];
    Assert(av_packet_ref(pkt_ref, pkt));
    Assert(av_interleaved_write_frame(fmt_ctx, pkt_ref));
    return *this;
}

PacketWriter& PacketWriter::SendVideoFrame(AVFrame *fr){
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
                av_write_trailer(fmt_ctx);
                return *this;
            default:
                Assert(ret);
        }
    }
    av_write_trailer(fmt_ctx);
    return *this;
}
