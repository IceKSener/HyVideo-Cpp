#include "VideoFrameReader.hpp"
#include "Common.hpp"

using namespace std;

extern uint32_t cpu_num;

VideoFrameReader::VideoFrameReader(InputVideo &vd, bool manual){
    if(!vd.is_open) vd.OpenInput();
    if(!manual) pkt_reader=new PacketReader(vd);
    AssertP(ctx=avcodec_alloc_context3(vd.codec));
    Assert(avcodec_parameters_to_context(ctx, vd.v_stream->codecpar));
    ctx->thread_count=cpu_num;
    ctx->time_base=vd.v_stream->time_base;
    Assert(avcodec_open2(ctx, vd.codec, NULL));
    AssertP(pkt=av_packet_alloc());
    AssertP(fr=av_frame_alloc());
    //DEBUG
    av_log(NULL, AV_LOG_INFO, "视频元数据[帧数:%d]\n",vd.num_frames);
}
VideoFrameReader::VideoFrameReader(VideoFrameReader &&vfr){
    *this=vfr;
    memset(&vfr, 0, sizeof(vfr));
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
    if(!fr) fr=this->fr;
    int ret;
    while(true){
        switch(ret=avcodec_receive_frame(ctx,fr)){
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
            fr->time_base=ctx->time_base;
            return fr;
        case AVERROR_EOF:
            return nullptr;
        default:
            Assert(ret);
        }
    }
    return fr;
}