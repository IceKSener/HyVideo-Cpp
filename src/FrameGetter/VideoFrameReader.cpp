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
    AssertP(fr = av_frame_alloc());
}

VideoFrameReader::VideoFrameReader(VideoFrameReader &&vfr) {
    is_end = vfr.is_end;
    time_base = vfr.time_base;
    fr = vfr.fr; vfr.fr = nullptr;
    pkt = vfr.pkt; vfr.pkt = nullptr;
    ctx = vfr.ctx; vfr.ctx = nullptr;
    pkt_reader = vfr.pkt_reader; vfr.pkt_reader = nullptr;
}

VideoFrameReader::~VideoFrameReader() {
    if (fr) av_frame_free(&fr);
    if (pkt) av_packet_free(&pkt);
    if (ctx) avcodec_free_context(&ctx);
    if (pkt_reader) delete pkt_reader;
}

VideoFrameReader &VideoFrameReader::addPacket(const AVPacket *pkt) {
    Assert(avcodec_send_packet(ctx, pkt));
    if(!pkt) is_end = true;
    return *this;
}

AVFrame* VideoFrameReader::nextFrame(AVFrame *fr) {
    int ret;
    while (true) {
        switch (ret = avcodec_receive_frame(ctx, this->fr)) {
        case AVERROR(EAGAIN):
            if (pkt_reader) {
                // 自动从PacketReader中取出packet
                if (pkt_reader->NextVideoPacket(pkt)) {
                    addPacket(pkt);
                    av_packet_unref(pkt);
                } 
                else if (!is_end) addPacket(nullptr);
                else return nullptr;
                
                continue;
            }
            else return nullptr; // 需要手动传入packet
        case 0:
            if (fr && fr!=this->fr) {
                // 用户传入fr指针时将帧输出到用户的fr中
                av_frame_unref(fr);
                av_frame_move_ref(fr, this->fr);
            }
            else fr = this->fr;
            fr->time_base = time_base;
            return fr;
        case AVERROR_EOF:
            return nullptr;
        default:
            Assert(ret);
        }
    }
    return nullptr;
}