#include "OutputVideo.hpp"
#include "Common.hpp"
extern "C"{
    #include "libavutil/imgutils.h"
}

using namespace std;
OutputVideo::OutputVideo(string path):path(path){ }
OutputVideo::OutputVideo(OutputVideo&& vd){
    *this=vd;
    memset(&vd, 0, sizeof(vd));
}

OutputVideo::~OutputVideo(){
    if(fmt_ctx){
        avio_closep(&fmt_ctx->pb);
        avformat_free_context(fmt_ctx);
    }
}

OutputVideo& OutputVideo::CopyVideoParam(InputVideo& vd){
    if(!vd.is_open) vd.OpenInput();
    width=vd.width; height=vd.height;
    fps=vd.fps; pix_fmt=vd.pix_fmt;
    return *this;
}
OutputVideo& OutputVideo::setWxH(int width, int height){
    if(width&1)++width; if(height&1)++height;
    this->width=width, this->height=height;
    return *this;
}
void OutputVideo::OpenOutput(){
    if(is_open) return;
    if(!width || !height)ThrowErr("请指定输出视频分辨率");
    if(!fps.num)ThrowErr("请指定输出视频帧率");
    AVOutputFormat of;
    Assert(avformat_alloc_output_context2(&fmt_ctx,NULL,format.empty()?NULL:format.c_str(),path.c_str())); //TODO
    if(!codec) codec=avcodec_find_encoder(AV_CODEC_ID_H264);
    is_open=true;
    return;
}

void OutputVideo::Print(){
    if(!is_open) OpenOutput();
    printf("%dx%d(Rate:%.3lf,codec:%s)\n",width,height,av_q2d(fps),codec->long_name);
    AVCodecParameters *param = v_stream->codecpar;
    printf("FrameSize:%d bytes\n",av_image_get_buffer_size((AVPixelFormat)param->format,param->width,param->height,1));
}