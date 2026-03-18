#include "PacketWriter.hpp"

#include "utils/Assert.hpp"
#include "GlobalConfig.hpp"

using namespace std;

PacketWriter::PacketWriter(OutputVideo &vd) {
    AssertP(pkt_buf = av_packet_alloc());
    AssertP(pkt_ref = av_packet_alloc());
    AssertP(fr_ref = av_frame_alloc());
    if (!vd.isInit()) vd.initOutput();
    auto& vd_info = vd.getInfo();
    fmt_ctx = vd.getFormatContext();

    // 创建编码器上下文
    AssertP(ctx = avcodec_alloc_context3(vd_info.codec));
    ctx->width = vd_info.width;
    ctx->height = vd_info.height;
    ctx->pix_fmt = vd_info.pix_fmt;
    ctx->time_base = {1, 1};
    ctx->framerate = vd_info.fps;
    ctx->thread_count = GlobalConfig.cpu_num;
    if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    Assert(avcodec_open2(ctx, vd_info.codec, &vd.getOption()));
    // 将正确的数据写回视频流编码信息中
    Assert(avcodec_parameters_from_context(vd.getVS()->codecpar, ctx));
    if (!vd.isOpen()) vd.openOutput();
    out_timebase = vd.getVSTimebase();
}

PacketWriter::PacketWriter(OutputVideo &vd, InputVideo &vd_in): PacketWriter(vd) {
    // 为每个流序号建立映射
    // TODO bug 与OutVideo的addAudio可能冲突
    const auto& in_as = vd_in.getASs();
    const auto& out_as = vd.getASs();
    int as_sz = min(in_as.size(), out_as.size());
    stream_mapping = new int[as_sz+1];
    stream_mapping[vd_in.getVS()->index]=vd.getVS()->index;
    for (int i=0 ; i<as_sz ; ++i){
        stream_mapping[in_as[i]->index] = out_as[i]->index;
    }
}

PacketWriter::PacketWriter(PacketWriter &&pw) {
    stream_mapping=pw.stream_mapping; pw.stream_mapping=nullptr;
    pkt_buf=pw.pkt_buf; pw.pkt_buf=nullptr;
    pkt_ref=pw.pkt_ref; pw.pkt_ref=nullptr;
    fr_ref=pw.fr_ref; pw.fr_ref=nullptr;
    fmt_ctx=pw.fmt_ctx;
    ctx=pw.ctx; pw.ctx=nullptr;
    out_timebase = pw.out_timebase;
}

PacketWriter::~PacketWriter() {
    if (ctx) avcodec_free_context(&ctx);
    if (pkt_buf) av_packet_free(&pkt_buf);
    if (pkt_ref) av_packet_free(&pkt_ref);
    if (fr_ref) av_frame_free(&fr_ref);
    if (stream_mapping) delete[] stream_mapping;
}

PacketWriter& PacketWriter::sendPacket(AVPacket *pkt, const AVRational* in_timebase){
    Assert(av_packet_ref(pkt_ref, pkt));
    if (stream_mapping) pkt_ref->stream_index = stream_mapping[pkt_ref->stream_index];
    if (in_timebase) av_packet_rescale_ts(pkt_ref, *in_timebase, fmt_ctx->streams[pkt_ref->stream_index]->time_base);
    Assert(av_interleaved_write_frame(fmt_ctx, pkt_ref));
    return *this;
}

PacketWriter& PacketWriter::sendVideoFrame(const AVFrame *fr) {
    if (!fr) return *this;
    // 将帧的时间基转为输出视频流的时间基
    av_frame_ref(fr_ref, fr);
    fr_ref->pts = av_rescale_q(fr->pts, fr->time_base, out_timebase);
    fr_ref->time_base = out_timebase;
    Assert(avcodec_send_frame(ctx, fr_ref));
    av_frame_unref(fr_ref);

    int ret;
    while (true) {
        switch (ret = avcodec_receive_packet(ctx, pkt_buf)) {
            case 0:
                sendPacket(pkt_buf);
                break;
            case AVERROR(EAGAIN):
                return *this;
            case AVERROR_EOF:
                writeEnd();
                return *this;
            default:
                Assert(ret);
        }
    }
    return *this;
}

PacketWriter& PacketWriter::writeEnd() {
    Assert(avcodec_send_frame(ctx, NULL));
    int ret;
    while (true) {
        switch (ret = avcodec_receive_packet(ctx, pkt_buf)){
            case 0:
                sendPacket(pkt_buf);
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
