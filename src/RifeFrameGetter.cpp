#include "RifeFrameGetter.hpp"
#include "Common.hpp"

using namespace std;
using Mat=ncnn::Mat;

void RifeFrameGetter::_InitRIFE(){
    if(rife=_rifes[model]) return;
    rife=new RIFE(use_gpu?0:-1, false, false, false, 1, false, true);
#ifdef _WIN32
    rife->load(wstring(model.begin(),model.end()));
#else
    rife->load(model);
#endif
    _rifes[model]=rife;
}
void RifeFrameGetter::_InitConverter(AVFrame *fr){
    if(cvt) return;
    cvt=new FrameConvert(fr->width, fr->height, (AVPixelFormat)fr->format, AV_PIX_FMT_RGB24);
}

RifeFrameGetter::RifeFrameGetter(IFreamGetter *getter, const RifeArgs& args):getter(getter){
    AssertP(f0=av_frame_alloc());
    AssertP(f1=av_frame_alloc());
    f0=getter->NextFrame(f0);
    model=args.model;
    use_gpu=args.use_gpu;
}

RifeFrameGetter::~RifeFrameGetter(){
    if(f0) av_frame_free(&f0);
    if(f1) av_frame_free(&f1);
}

AVFrame *RifeFrameGetter::NextFrame(AVFrame *fr){
    if(!rife) _InitRIFE();
    fr=getter->NextFrame(fr);
    if(!cvt) _InitConverter(fr);
    //TODO
    // Mat i0=Mat::from_pixels(),i1,o;
    // rife.process();
    return nullptr;
}
