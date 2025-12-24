#include "FrameGetter/VideoFrameReader.hpp"
#include "Common.hpp"

using namespace std;

VideoFrameReader::VideoFrameReader(InputVideo &vd, bool manual){
    if(!vd.is_open) vd.OpenInput();
    if(!manual) pkt_reader=new PacketReader(vd);
    AssertP(ctx=avcodec_alloc_context3(vd.codec));
    Assert(avcodec_parameters_to_context(ctx, vd.v_stream->codecpar));
    ctx->thread_count=GlobalConfig.cpu_num;
    ctx->time_base=vd.v_stream->time_base;
    Assert(avcodec_open2(ctx, vd.codec, NULL));
    AssertP(pkt=av_packet_alloc());
    AssertP(fr=av_frame_alloc());
    //DEBUG
    av_log(NULL, AV_LOG_INFO, "视频元数据[帧数:%d]\n",vd.num_frames);
}
VideoFrameReader::VideoFrameReader(VideoFrameReader &&vfr){
    is_end=vfr.is_end;
    fr=vfr.fr; vfr.fr=nullptr;
    pkt=vfr.pkt; vfr.pkt=nullptr;
    ctx=vfr.ctx; vfr.ctx=nullptr;
    pkt_reader=vfr.pkt_reader; vfr.pkt_reader=nullptr;
}
VideoFrameReader::~VideoFrameReader()
{
    if(fr) av_frame_free(&fr);
    if(pkt) av_packet_free(&pkt);
    if(ctx) avcodec_free_context(&ctx);
    if(pkt_reader) delete pkt_reader;
}

VideoFrameReader &VideoFrameReader::AddPacket(AVPacket *pkt){
    Assert(avcodec_send_packet(ctx, pkt));
    if(!pkt) is_end=true;
    return *this;
}
AVFrame* VideoFrameReader::NextFrame(AVFrame *fr){
    int ret;
    while(true){
        switch(ret=avcodec_receive_frame(ctx,this->fr)){
        case AVERROR(EAGAIN):
            if(pkt_reader){
                if(pkt_reader->NextVideoPacket(pkt)){
                    AddPacket(pkt);
                    av_packet_unref(pkt);
                }
                else if(!is_end) AddPacket(nullptr);
                else return nullptr;
                
                continue;
            }
            else return nullptr;
        case 0:
            this->fr->time_base=ctx->time_base;
            if(fr) av_frame_ref(fr, this->fr);
            return this->fr;
        case AVERROR_EOF:
            return nullptr;
        default:
            Assert(ret);
        }
    }
    if(fr) av_frame_ref(fr, this->fr);
    return this->fr;
}