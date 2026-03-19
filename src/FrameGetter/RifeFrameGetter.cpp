#include "FrameGetter/RifeFrameGetter.hpp"

extern "C"{
    #include "libavutil/imgutils.h"
}

#include "utils/Assert.hpp"
#include "utils/Clocker.hpp"
#include "utils/Logger.hpp"
#include "utils/FileStr.hpp"

using namespace std;
using Mat=ncnn::Mat;

static AVFrame* rescaleFrame(AVFrame* fr, const AVRational& timebase, double pts) {
    if (timebase.den < 500) {
        // 解决时间戳精度不足的错误
        fr->time_base = AV_TIME_BASE_Q;
        fr->pts = pts*AV_TIME_BASE*av_q2d(timebase)+0.5;
    } else {
        fr->time_base = timebase;
        fr->pts = pts + 0.5;
    }
    return fr;
}

RifeFrameGetter::RifeFrameGetter(const shared_ptr<IFreamGetter>& getter, const Args& args)
    : getter(getter)
{
    info.model = args.model;
    info.use_gpu = args.use_gpu;
    AssertP(status.f0 = av_frame_alloc());
    AssertP(status.f1 = av_frame_alloc());
    AssertP(status.f0_rgb = av_frame_alloc());
    AssertP(status.f1_rgb = av_frame_alloc());
    AssertP(status.fr = av_frame_alloc());
}

RifeFrameGetter::RifeFrameGetter(RifeFrameGetter &&rfg)
    : info(move(rfg.info))
    , status(move(rfg.status))
    , getter(move(rfg.getter))
    , cvt(rfg.cvt)
    , mo(move(rfg.mo))
{
    rfg.cvt=nullptr;
    auto& rs = rfg.status;
    rs.f0 = rs.f1 = rs.fr = rs.f0_rgb = rs.f1_rgb = nullptr;
}

RifeFrameGetter::~RifeFrameGetter() {
    if (status.f0) av_frame_free(&status.f0);
    if (status.f1) av_frame_free(&status.f1);
    if (status.f0_rgb) av_frame_free(&status.f0_rgb);
    if (status.f1_rgb) av_frame_free(&status.f1_rgb);
    if (status.fr) av_frame_free(&status.fr);
    if (cvt) delete cvt;
}

RifeFrameGetter& RifeFrameGetter::setProcess(bool process, const Score *score) {
    if (process && score!=nullptr) {
        info.process.emplace(score->CalcProcess());
        info._NextFrame = _nextFrameProcess;
    } else {
        info.process.reset();
        info._NextFrame = _nextFrameNoProcess;
    }
    return *this;
}

AVFrame* RifeFrameGetter::nextFrame(AVFrame *fr) {
    if (status.is_end) return nullptr;
    auto& f0 = status.f0;
    if (!f0->height) {
        // f0 内仍无数据
        if (!getter->nextFrame(f0)) {
            if (getter->isEnd()) ThrowErr("无法获取视频第一帧");
            return nullptr;
        }
        status.f0_pts = status.f0->pts;
    }
    if(!status.rife) _initRIFE();
    if(!cvt) _initConverter(f0->width, f0->height);
    AVFrame *out = (this->*info._NextFrame)();
    if(fr && out){
        // 用户指定的输出
        av_frame_unref(fr);
        av_frame_ref(fr, out);
        return fr;
    }
    return out;
}

void RifeFrameGetter::_initRIFE(){
    // TODO 不适合多线程使用一个模型实例
    const string& model = info.model;
    if (status.rife = _rifes[model]) {
        status.rife->buf_clear();
        return;
    }
    string model_dir = "./models/RIFE/" + model;
    // TODO 分析输入模型的类型和参数
    status.rife = new RIFE(info.use_gpu?0:-1, false, false, false, 1, false, true);
#ifdef _WIN32
    if(status.rife->load(wstring(model_dir.begin(),model_dir.end()))) throw model+"模型打开失败";
#else
    if(status.rife->load(model_dir)) throw model+"模型打开失败";
#endif
    _rifes[model] = status.rife;
}

void RifeFrameGetter::_initConverter(int width, int height){
    if (cvt) return;
    cvt = new FrameConvert(width, height, AV_PIX_FMT_RGB24);
}

AVFrame* RifeFrameGetter::_makeMiddelFrame(double timestep) {
    int &fr_index=status.fr_index, &f0_pts=status.f0_pts, &f1_pts=status.f1_pts;
    const auto &f0=status.f0, &f1=status.f1;
    auto &fr=status.fr, &f0_rgb=status.f0_rgb, &f1_rgb=status.f1_rgb;
    auto &rgb_valid = status.rgb_valid;
    // 将f0、f1转为RGB格式
    const int w=f1->width, h=f1->height;
    Mat m0, m1; // m0，m1仅用于存储图片宽高和数据地址
    if (f0->format == AV_PIX_FMT_RGB24) {
        m0 = Mat(w,h,f0->data[0],(size_t)3,1);
    } else {
        if (!f0_rgb->height) {
            f0_rgb->width = w;
            f0_rgb->height = h;
            f0_rgb->format = AV_PIX_FMT_RGB24;
            Assert(av_frame_get_buffer(f0_rgb, 1));
        }
        if (!rgb_valid[0]) {
            cvt->convert(f0, f0_rgb);
            rgb_valid[0] = true;
        }
        m0 = Mat(w,h,f0_rgb->data[0],(size_t)3,1);
    }
    if (f1->format == AV_PIX_FMT_RGB24) {
        m1 = Mat(w,h,f1->data[0],(size_t)3,1);
    } else {
        if (!f1_rgb->height) {
            f1_rgb->width = w;
            f1_rgb->height = h;
            f1_rgb->format = AV_PIX_FMT_RGB24;
            Assert(av_frame_get_buffer(f1_rgb, 1));
        }
        if (!rgb_valid[1]) {
            cvt->convert(f1, f1_rgb);
            rgb_valid[1] = true;
        }
        m1 = ncnn::Mat(w,h,f1_rgb->data[0],(size_t)3,1);
    }
    // DEBUG
    clocker.start(20);
    mo = status.rife->process_buf(m0, m1, timestep, mo);
    clocker.end(20);
    av_frame_unref(fr);
    fr->width = w;
    fr->height = h;
    fr->format = AV_PIX_FMT_RGB24;
    AssertI(av_image_fill_arrays(fr->data, fr->linesize, (uint8_t*)mo.data, AV_PIX_FMT_RGB24, w, h, 1));
    
    rescaleFrame(fr, f1->time_base, f0_pts+(f1_pts-f0_pts)*timestep);
    ++fr_index;
    return fr;
}

AVFrame* RifeFrameGetter::_nextFrameProcess(){
    int &fr_index=status.fr_index, &f0_index=status.f0_index, &f1_index=status.f1_index
        , &f0_pts=status.f0_pts, &f1_pts=status.f1_pts;
    const auto &fpsx = info.fpsx;
    const auto &f0=status.f0, &f1=status.f1;
    auto &fr=status.fr, &f0_rgb=status.f0_rgb, &f1_rgb=status.f1_rgb;
    auto &rgb_valid = status.rgb_valid;
    // fr_pos=fr_index/fpsx
    AVRational fr_pos={fr_index*fpsx.den, fpsx.num};
    // if(fr_pos==f0_index)
    if (fr_pos.num == f0_index*fr_pos.den) { ++fr_index; return f0; }
    if (!f1->height) {
        if (!getter->nextFrame(f1)) {
            status.is_end = getter->isEnd();
            return nullptr;
        }
        f1_pts = f1->pts;
    }
    const auto& process = info.process.value();
    // 读取对f1的处理信息：[0-不处理][<0(静帧)跳过][>0(转场)复制前面的帧]
    int8_t prcs = f1_index<process.size() ? process[f1_index] : 0;
    // 跳过静帧，将新的有效帧存入f1
    while (prcs < 0) {
        // 获取下一帧
        if (!getter->nextFrame(f1)) {
            if (getter->isEnd()) break; // 将最后读取到的帧作为f1
            else return nullptr;
        }
        AvLog("跳过静帧[%5d](%s)\n", f1_index, getTimeStr(f1_pts, f1->time_base).c_str());
        f1_pts = f1->pts;
        ++f1_index;
        rgb_valid[1] = false;
        prcs = f1_index<process.size() ? process[f1_index] : 0;
    }
    // 位移到对应帧，判断fr_pos>=f1_index
    AVFrame *tmp_fr;
    while (fr_pos.num >= f1_index*fr_pos.den) {
        if (!(tmp_fr = getter->nextFrame())) {
            if (getter->isEnd()) {
                status.is_end = true;
                // ++fr_index;
                return f1;  // 待补帧超出视频范围，返回最后一帧
            } else {
                return nullptr;
            }
        }
        // 位移 f0<-f1 f1<-tmp_fr
        av_frame_unref(f0);
        av_frame_move_ref(f0, f1);
        av_frame_move_ref(f1, tmp_fr);
        status.rife->buf_next();
        {
            AVFrame *tmp = f0_rgb;
            f0_rgb = f1_rgb;
            f1_rgb = tmp;
            rgb_valid[0] = rgb_valid[1];
            rgb_valid[1] = false;
        }
        f0_index = f1_index++;
        prcs = f1_index<process.size() ? process[f1_index] : 0;
        f0_pts = f1_pts;
        f1_pts = f1->pts;
        // 跳过静帧，将新的有效帧存入f1
        while (prcs < 0) {
            // 获取下一帧
            if (!getter->nextFrame(f1)) {
                if (getter->isEnd()) break;
                else return nullptr;
            }
            AvLog("跳过静帧[%5d](%s)\n", f1_index, getTimeStr(f1_pts, f1->time_base).c_str());
            f1_pts = f1->pts;
            ++f1_index;
            rgb_valid[1] = false;
            prcs = f1_index<process.size() ? process[f1_index] : 0;
        }
    }
    // if(fr_pos==f0_index)
    if (fr_pos.num == f0_index*fr_pos.den){ ++fr_index; return f0; }
    
    // timestep=(fr_pos-f0_index)/(f1_index-f0_index)
    double timestep = (double)(fr_pos.num-f0_index*fr_pos.den)/(fr_pos.den*(f1_index-f0_index));
    // 判断f1是否为转场帧
    if (prcs > 0) {
        AvLog("转场帧[%5d](%s)\n", f1_index, getTimeStr(f1_pts, f1->time_base).c_str());
        av_frame_unref(fr);
        av_frame_ref(fr, f0);
        rescaleFrame(fr, f1->time_base, f0_pts+(f1_pts-f0_pts)*timestep);
        ++fr_index;
        return fr;
    }

    return _makeMiddelFrame(timestep);
}

AVFrame* RifeFrameGetter::_nextFrameNoProcess(){
    int &fr_index=status.fr_index, &f0_index=status.f0_index, &f1_index=status.f1_index
        , &f0_pts=status.f0_pts, &f1_pts=status.f1_pts;
    const auto &fpsx = info.fpsx;
    const auto &f0=status.f0, &f1=status.f1;
    auto &fr=status.fr, &f0_rgb=status.f0_rgb, &f1_rgb=status.f1_rgb;
    auto &rgb_valid = status.rgb_valid;
    // 当前帧的时间步长：timestep=fr_index/fpsx-f0_index
    AVRational timestep = {fr_index*fpsx.den-f0_index*fpsx.num, fpsx.num};
    if (timestep.num == 0) { ++fr_index; return f0; }
    if (!f1->height) {
        if (!getter->nextFrame(f1)) {
            status.is_end = getter->isEnd();
            return nullptr;
        }
        f1_pts = f1->pts;
    }
    AVFrame *tmp_fr;
    // 当timestep>=1时，位移到对应帧
    while (timestep.num>=timestep.den) {
        if (!(tmp_fr = getter->nextFrame())) {
            if (getter->isEnd()) {
                // 读取完视频所有帧后返回f1
                status.is_end=true;
                // ++fr_index;
                return f1;
            } else {
                return nullptr;
            }
        }
        // 位移 f0<-f1 f1<-tmp_fr
        av_frame_unref(f0);
        av_frame_move_ref(f0, f1);
        av_frame_move_ref(f1, tmp_fr);
        status.rife->buf_next();    // 将rife实例中的缓存进行位移
        {
            AVFrame *tmp = f0_rgb;
            f0_rgb = f1_rgb;
            f1_rgb = tmp;
            rgb_valid[0] = rgb_valid[1];
            rgb_valid[1] = false;
        }
        timestep.num -= timestep.den;
        f0_index = f1_index++;
        f0_pts = f1_pts;
        f1_pts = f1->pts;
    }
    if (timestep.num==0) { ++fr_index; return f0; }

    return _makeMiddelFrame(av_q2d(timestep));
}