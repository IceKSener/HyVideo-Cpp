#include "FrameGetter/VideoFrameReader.hpp"

#include "GlobalConfig.hpp"
#include "utils/Assert.hpp"

using namespace std;

VideoFrameReader::VideoFrameReader(InputVideo &vd, bool manual) {
    if (!vd.isOpen()) vd.openInput();
    if (!manual) pkt_reader = new PacketReader(vd);
    auto& info = vd.getInfo();
    AssertP(ctx = avcodec_alloc_context3(info.codec));
    Assert(avcodec_parameters_to_context(ctx, vd.getVS()->codecpar));
    ctx->thread_count = GlobalConfig.cpu_num;
    time_base = vd.getVS()->time_base;
    Assert(avcodec_open2(ctx, info.codec, NULL));
    AssertP(pkt = av_packet_alloc());
}

VideoFrameReader::VideoFrameReader(VideoFrameReader &&vfr)
:   fr_inner(move(vfr.fr_inner))
{
    is_end = vfr.is_end;
    time_base = vfr.time_base;
    pkt = vfr.pkt; vfr.pkt = nullptr;
    ctx = vfr.ctx; vfr.ctx = nullptr;
    pkt_reader = vfr.pkt_reader; vfr.pkt_reader = nullptr;
}

VideoFrameReader::~VideoFrameReader() {
    if (pkt) av_packet_free(&pkt);
    if (ctx) avcodec_free_context(&ctx);
    if (pkt_reader) delete pkt_reader;
}

VideoFrameReader &VideoFrameReader::addPacket(const AVPacket *pkt) {
    Assert(avcodec_send_packet(ctx, pkt));
    if(!pkt) is_end = true;
    return *this;
}

bool VideoFrameReader::nextFrame(HvFrame& fr) {
    int ret;
    while (true) {
        switch (ret = avcodec_receive_frame(ctx, fr_inner.fr)) {
        case AVERROR(EAGAIN):
            if (pkt_reader) {
                // 自动从PacketReader中取出packet
                if (pkt_reader->NextVideoPacket(pkt)) {
                    addPacket(pkt);
                    av_packet_unref(pkt);
                } 
                else if (!is_end) addPacket(nullptr);
                else return false;
                
                continue;
            }
            else return false; // 需要手动传入packet
        case 0:
            fr = move(fr_inner);
            fr.fr->time_base = time_base;
            return true;
        case AVERROR_EOF:
            return false;
        default:
            Assert(ret);
        }
    }
    return false;
}