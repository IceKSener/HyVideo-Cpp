#include "RifeFrameGetter.hpp"
#include "Common.hpp"
extern "C"{
    #include "libavutil/imgutils.h"
}

using namespace std;
using Mat=ncnn::Mat;

void RifeFrameGetter::_InitRIFE(){
    if(rife=_rifes[model]) return;
    string model_dir = "./models/RIFE/"+model;
    rife=new RIFE(use_gpu?0:-1, false, false, false, 1, false, true);
#ifdef _WIN32
    if(rife->load(wstring(model_dir.begin(),model_dir.end()))) throw model+"模型打开失败";
#else
    if(rife->load(model_dir)) throw model+"模型打开失败";
#endif
    _rifes[model]=rife;
}
void RifeFrameGetter::_InitConverter(AVFrame *fr){
    if(cvt) return;
    cvt=new FrameConvert(fr->width, fr->height, AV_PIX_FMT_RGB24);
}


RifeFrameGetter::RifeFrameGetter(const std::shared_ptr<IFreamGetter>& getter, const Args& args):getter(getter){
    AssertP(f0=av_frame_alloc());
    AssertP(f1=av_frame_alloc());
    AssertP(f0_rgb=av_frame_alloc());
    AssertP(f1_rgb=av_frame_alloc());
    AssertP(fr=av_frame_alloc());
    md=new ncnn::Mat;
    model=args.model;
    use_gpu=args.use_gpu;
}

RifeFrameGetter::RifeFrameGetter(RifeFrameGetter &&rfg){
    model=std::move(rfg.model);
    use_gpu=rfg.use_gpu;
    fpsx=rfg.fpsx;
    process=std::move(rfg.process);
    rife=rfg.rife;
    fr_index=rfg.fr_index, f0_index=rfg.f0_index, f1_index=rfg.f1_index;
    f0_pts=rfg.f0_pts, f1_pts=rfg.f1_pts;
    is_end=rfg.is_end;
    cvt=rfg.cvt; rfg.cvt=nullptr;
    getter=std::move(rfg.getter);
    f0=rfg.f0, f1=rfg.f1, f0_rgb=rfg.f0_rgb, f1_rgb=rfg.f1_rgb, fr=rfg.fr;
    rfg.f0 = rfg.f1 = rfg.f0_rgb = rfg.f1_rgb = rfg.fr = nullptr;
    md=rfg.md; rfg.md=nullptr;
    rgb_valid[0]=rfg.rgb_valid[0]; rgb_valid[1]=rfg.rgb_valid[1];
    _NextFrame=rfg._NextFrame;
}

RifeFrameGetter::~RifeFrameGetter(){
    if(f0) av_frame_free(&f0);
    if(f1) av_frame_free(&f1);
    if(f0_rgb) av_frame_free(&f0_rgb);
    if(f1_rgb) av_frame_free(&f1_rgb);
    if(fr) av_frame_free(&fr);
    if(md) delete md;
    if(cvt) delete cvt;
}

RifeFrameGetter &RifeFrameGetter::SetProcess(bool process, const Score *score){
    if(process && score!=nullptr){
        this->process.emplace(score->CalcProcess());
        _NextFrame=&RifeFrameGetter::_NextFrameProecess;
    }else{
        this->process.reset();
        _NextFrame=&RifeFrameGetter::_NextFrameNoProecess;
    }
    return *this;
}

AVFrame *RifeFrameGetter::_NextFrameProecess(){
    ThrowErr("暂不支持帧处理");
    return nullptr;
}
AVFrame *RifeFrameGetter::_NextFrameNoProecess(){
    // timestep=fr_index/fpsx-f0_index
    AVRational timestep={fr_index*fpsx.den-f0_index*fpsx.num, fpsx.num};
    if(timestep.num==0){ ++fr_index;return f0; }
    if(!f1->height){
        if(!getter->NextFrame(f1)){
            is_end=getter->IsEnd();
            return nullptr;
        }
        f1_pts=f1->pts;
    }
    AVFrame *tmp_fr;
    // 位移到对应帧
    while(timestep.num>=timestep.den){
        // TODO BUG 获取不到帧时，反复释放
        if(!(tmp_fr=getter->NextFrame())){
            if(getter->IsEnd()){
                is_end=true;
                // ++fr_index;
                return f0;
            }else{
                return nullptr;
            }
        }
        av_frame_unref(f0);
        av_frame_move_ref(f0, f1);
        av_frame_move_ref(f1, tmp_fr);
        rife->buf_next();
        {
            AVFrame *tmp = f0_rgb;
            f0_rgb = f1_rgb;
            f1_rgb = tmp;
            rgb_valid[0]=rgb_valid[1];
            rgb_valid[1]=false;
        }
        timestep.num-=timestep.den;
        f0_index=f1_index++;
        f0_pts=f1_pts;
        f1_pts=f1->pts;
        // *m0=*m1;
        //TODO 待优化
        // m0->release();
    }
    if(timestep.num==0){ ++fr_index;return f0; }

    const int& w = f1->width;
    const int& h = f0->height;
    ncnn::Mat m0,m1;
    if(f0->format==AV_PIX_FMT_RGB24){
        m0=ncnn::Mat(w,h,f0->data[0],(size_t)3,3);
    }else{
        if(!f0_rgb->height){
            f0_rgb->width = w;
            f0_rgb->height = h;
            f0_rgb->format = AV_PIX_FMT_RGB24;
            Assert(av_frame_get_buffer(f0_rgb, 1));
        }
        if(!rgb_valid[0]){
            cvt->Convert(f0,f0_rgb);
            rgb_valid[0]=true;
        }
        m0=ncnn::Mat(w,h,f0_rgb->data[0],(size_t)3,1);
    }
    if(f1->format==AV_PIX_FMT_RGB24){
        m1=ncnn::Mat(w,h,f1->data[0],(size_t)3,1);
    }else{
        if(!f1_rgb->height){
            f1_rgb->width = w;
            f1_rgb->height = h;
            f1_rgb->format = AV_PIX_FMT_RGB24;
            Assert(av_frame_get_buffer(f1_rgb, 1));
        }
        if(!rgb_valid[1]){
            cvt->Convert(f1,f1_rgb);
            rgb_valid[1]=true;
        }
        m1=ncnn::Mat(w,h,f1_rgb->data[0],(size_t)3,1);
    }
    // if(md->empty()) md->create(w, h,(size_t)3,3);
    
    clocker.start(20);
    // ncnn::Mat mo=rife->process(m0, m1, av_q2d(timestep), *md);
    ncnn::Mat mo = rife->process_buf(m0, m1, av_q2d(timestep), *md);
    clocker.end(20);
    AssertI(av_image_fill_arrays(fr->data, fr->linesize, (uint8_t*)mo.data, AV_PIX_FMT_RGB24, w, h,1));
    fr->time_base=f1->time_base;
    fr->pts=f0_pts+(f1_pts-f0_pts)*av_q2d(timestep)+0.5;
    ++fr_index;
    return fr;
}
AVFrame* RifeFrameGetter::NextFrame(AVFrame *fr){
    if(is_end) return nullptr;
    if(!f0->height){
        if(!getter->NextFrame(f0)){
            if(getter->IsEnd()) ThrowErr("无法获取视频第一帧");
            return nullptr;
        }
        this->fr->width = f0->width;
        this->fr->height = f0->height;
        this->fr->format = AV_PIX_FMT_RGB24;
        
        f0_pts=f0->pts;
    }
    if(!rife) _InitRIFE();
    if(!cvt) _InitConverter(f0);
    AVFrame *out = (this->*_NextFrame)();
    if(fr && out){
        av_frame_unref(fr);
        av_frame_ref(fr,out);
        return fr;
    }
    return out;
}
