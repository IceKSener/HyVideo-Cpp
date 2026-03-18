#include "PacketReader.hpp"

#include "utils/Assert.hpp"

PacketReader::PacketReader(InputVideo &vd) {
    AssertP(pkt = av_packet_alloc());
    if (!vd.isOpen()) vd.openInput();
    fmt_ctx = vd.getFormatContext();
}

PacketReader::PacketReader(PacketReader &&pr)
    : fmt_ctx(pr.fmt_ctx)
    , pkt(pr.pkt)
{
    pr.pkt = nullptr;
}

PacketReader::~PacketReader() {
    if (pkt) av_packet_free(&pkt);
}

AVPacket* PacketReader::NextPacket(AVPacket *pkt) {
    if (!pkt) pkt = this->pkt;
    int ret = av_read_frame(fmt_ctx, pkt);
    if(ret==AVERROR_EOF) return nullptr;
    else Assert(ret);
    return pkt;
}

AVPacket* PacketReader::NextVideoPacket(AVPacket *pkt) {
    if (!pkt) pkt = this->pkt;
    while (NextPacket(pkt)) {
        if (fmt_ctx->streams[pkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            return pkt;
        av_packet_unref(pkt);
    }
    return nullptr;
}
