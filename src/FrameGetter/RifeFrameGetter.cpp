#include "FrameGetter/RifeFrameGetter.hpp"

extern "C"{
    #include "libavutil/imgutils.h"
}

#include "utils/Assert.hpp"
#include "utils/Logger.hpp"
#include "utils/FileStr.hpp"

using namespace std;
using Mat = ncnn::Mat;

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
    info.gpu_index = args.gpu_index;
}

RifeFrameGetter::RifeFrameGetter(RifeFrameGetter &&rfg)
    : info(move(rfg.info))
    , status(move(rfg.status))
    , getter(move(rfg.getter))
    , cvt(rfg.cvt)
{
    rfg.cvt = nullptr;
}

RifeFrameGetter::~RifeFrameGetter() {
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

bool RifeFrameGetter::nextFrame(HvFrame& fr) {
    if (status.is_end) return false;
    auto& f0 = status.f0;
    if (f0.isEmpty()) {
        // f0 内仍无数据
        if (!getter->nextFrame(f0)) {
            if (getter->isEnd()) ThrowErr("无法获取视频第一帧");
            return false;
        }
        status.f0_pts = f0.fr->pts;
    }
    if(!status.rife) _initRIFE();
    if(!cvt) _initConverter(f0.fr->width, f0.fr->height);
    return (this->*info._NextFrame)(fr);
}

void RifeFrameGetter::_initRIFE(){
    // TODO 不适合多线程使用一个模型实例
    const string& model = info.model;

    // TODO 分析输入模型的参数(TTA，uhd等)
    bool tta_mode = false, tta_temporal_mode = false, uhd_mode = false;
    
    stringstream _key;
    _key << model;
    if (info.use_gpu) _key << "-G" << info.gpu_index;
    if (tta_mode) _key << "-TTA";
    if (tta_temporal_mode) _key << "-TTAT" ;
    if (uhd_mode) _key << "-U";
    const string key = _key.str();

    if (status.rife = _rifes[key]) {
        status.rife->buf_clear();
        return;
    }

    bool rife_v2=false, rife_v4=false;
    if (model.find("rife-v2") != string::npos)      rife_v2 = true;
    else if (model.find("rife-v3") != string::npos) rife_v2 = true;
    else if (model.find("rife-v4") != string::npos) rife_v4 = true;
    status.rife = new RIFE(info.use_gpu?info.gpu_index:-1, tta_mode, tta_temporal_mode, uhd_mode, 1, rife_v2, rife_v4);
    
    string model_dir = "./models/RIFE/" + model;
#ifdef _WIN32
    if(status.rife->load(wstring(model_dir.begin(),model_dir.end()))) throw model+"模型打开失败";
#else
    if(status.rife->load(model_dir)) throw model+"模型打开失败";
#endif
    _rifes[key] = status.rife;
}

void RifeFrameGetter::_initConverter(int width, int height){
    if (cvt) return;
    cvt = new FrameConvert(width, height, AV_PIX_FMT_RGB24);
}

HvFrame RifeFrameGetter::_makeMiddelFrame(double timestep) {
    int &fr_index=status.fr_index, &f0_pts=status.f0_pts, &f1_pts=status.f1_pts;
    const auto &f0=status.f0, &f1=status.f1;
    auto &f0_rgb=status.f0_rgb, &f1_rgb=status.f1_rgb;
    auto &rgb_valid = status.rgb_valid;
    // 将f0、f1转为RGB格式
    const int w=f1.fr->width, h=f1.fr->height;
    Mat m0, m1; // m0，m1仅用于存储图片宽高和数据地址
    if (f0.fr->format==AV_PIX_FMT_RGB24 && f0.fr->width*3==f0.fr->linesize[0]) {
        m0 = Mat(w, h, f0.fr->data[0], (size_t)3, 1);
    } else {
        if (!rgb_valid[0]) {
            f0_rgb.createBuffer(w, h, AV_PIX_FMT_RGB24, 1);
            cvt->convert(f0, f0_rgb);
            rgb_valid[0] = true;
        }
        m0 = Mat(w, h, f0_rgb.fr->data[0], (size_t)3, 1);
    }
    if (f1.fr->format==AV_PIX_FMT_RGB24 && f1.fr->width*3==f1.fr->linesize[0]) {
        m1 = Mat(w, h, f1.fr->data[0], (size_t)3, 1);
    } else {
        if (!rgb_valid[1]) {
            f1_rgb.createBuffer(w, h, AV_PIX_FMT_RGB24, 1);
            cvt->convert(f1, f1_rgb);
            rgb_valid[1] = true;
        }
        m1 = Mat(w, h, f1_rgb.fr->data[0], (size_t)3, 1);
    }
    HvFrame fr_out;
    fr_out.createBuffer(w, h, AV_PIX_FMT_RGB24, 1);
    Mat mo(w, h, fr_out.fr->data[0], (size_t)3, 1);
    status.rife->process_buf(m0, m1, timestep, mo);
    
    rescaleFrame(fr_out.fr, f1.fr->time_base, f0_pts+(f1_pts-f0_pts)*timestep);
    ++fr_index;
    return fr_out;
}

bool RifeFrameGetter::_nextFrameProcess(HvFrame& fr){
    int &fr_index=status.fr_index, &f0_index=status.f0_index, &f1_index=status.f1_index
        , &f0_pts=status.f0_pts, &f1_pts=status.f1_pts;
    const auto &fpsx = info.fpsx;
    auto &f0=status.f0, &f1=status.f1;
    auto &f0_rgb=status.f0_rgb, &f1_rgb=status.f1_rgb;
    auto &rgb_valid = status.rgb_valid;
    // fr_pos=fr_index/fpsx
    AVRational fr_pos={fr_index*fpsx.den, fpsx.num};
    // if(fr_pos==f0_index)
    if (fr_pos.num == f0_index*fr_pos.den) { ++fr_index; fr=f0; return true; }
    if (f1.isEmpty()) {
        if (!getter->nextFrame(f1)) {
            status.is_end = getter->isEnd();
            return false;
        }
        f1_pts = f1.fr->pts;
    }
    const auto& process = info.process.value();
    // 读取对f1的处理信息：[0-不处理][<0(静帧)跳过][>0(转场)复制前面的帧]
    int8_t prcs = f1_index<process.size() ? process[f1_index] : 0;
    // 跳过静帧，将新的有效帧存入f1
    while (prcs < 0) {
        // 获取下一帧
        if (!getter->nextFrame(f1)) {
            if (getter->isEnd()) break; // 将最后读取到的帧作为f1
            else return false;
        }
        AvLog("跳过静帧[%5d](%s)\n", f1_index, getTimeStr(f1_pts, f1.fr->time_base).c_str());
        f1_pts = f1.fr->pts;
        ++f1_index;
        rgb_valid[1] = false;
        prcs = f1_index<process.size() ? process[f1_index] : 0;
    }
    // 位移到对应帧，判断fr_pos>=f1_index
    if (fr_pos.num >= f1_index*fr_pos.den) {
        HvFrame tmp_fr;
        while (fr_pos.num >= f1_index*fr_pos.den) {
            if (!getter->nextFrame(tmp_fr)) {
                if (getter->isEnd()) {
                    status.is_end = true;
                    // ++fr_index;
                    fr = f1;
                    return true;  // 待补帧超出视频范围，返回最后一帧
                } else {
                    return false;
                }
            }
            // 位移 f0<-f1 f1<-tmp_fr
            f0 = move(f1);
            f1 = move(tmp_fr);
            status.rife->buf_next();
            
            f0_rgb.swap(f1_rgb);    // 使用交换而不释放，省去重新分配内存
            rgb_valid[0] = rgb_valid[1];
            rgb_valid[1] = false;
            
            f0_index = f1_index++;
            prcs = f1_index<process.size() ? process[f1_index] : 0;
            f0_pts = f1_pts;
            f1_pts = f1.fr->pts;
            // 跳过静帧，将新的有效帧存入f1
            while (prcs < 0) {
                // 获取下一帧
                if (!getter->nextFrame(f1)) {
                    if (getter->isEnd()) break;
                    else return false;
                }
                AvLog("跳过静帧[%5d](%s)\n", f1_index, getTimeStr(f1_pts, f1.fr->time_base).c_str());
                f1_pts = f1.fr->pts;
                ++f1_index;
                rgb_valid[1] = false;
                prcs = f1_index<process.size() ? process[f1_index] : 0;
            }
        }
    }
    
    // if(fr_pos==f0_index)
    if (fr_pos.num == f0_index*fr_pos.den){ ++fr_index; fr=f0; return true; }
    
    // timestep=(fr_pos-f0_index)/(f1_index-f0_index)
    double timestep = (double)(fr_pos.num-f0_index*fr_pos.den)/(fr_pos.den*(f1_index-f0_index));
    // 判断f1是否为转场帧
    if (prcs > 0) {
        AvLog("转场帧[%5d](%s)\n", f1_index, getTimeStr(f1_pts, f1.fr->time_base).c_str());
        fr = f0;
        rescaleFrame(fr.fr, f1.fr->time_base, f0_pts+(f1_pts-f0_pts)*timestep);
        ++fr_index;
        return true;
    }

    fr = _makeMiddelFrame(timestep);
    return true;
}

bool RifeFrameGetter::_nextFrameNoProcess(HvFrame& fr){
    int &fr_index=status.fr_index, &f0_index=status.f0_index, &f1_index=status.f1_index
        , &f0_pts=status.f0_pts, &f1_pts=status.f1_pts;
    const auto &fpsx = info.fpsx;
    auto &f0=status.f0, &f1=status.f1;
    auto &f0_rgb=status.f0_rgb, &f1_rgb=status.f1_rgb;
    auto &rgb_valid = status.rgb_valid;
    // 当前帧的时间步长：timestep=fr_index/fpsx-f0_index
    AVRational timestep = {fr_index*fpsx.den-f0_index*fpsx.num, fpsx.num};
    if (timestep.num == 0) { ++fr_index; fr=f0; return true; }
    if (f1.isEmpty()) {
        if (!getter->nextFrame(f1)) {
            status.is_end = getter->isEnd();
            return false;
        }
        f1_pts = f1.fr->pts;
    }
    // 当timestep>=1时，位移到对应帧
    if (timestep.num >= timestep.den) {
        HvFrame tmp_fr;
        while (timestep.num >= timestep.den) {
            if (!getter->nextFrame(tmp_fr)) {
                if (getter->isEnd()) {
                    // 读取完视频所有帧后返回f1
                    status.is_end=true;
                    // ++fr_index;
                    fr = f1;
                    return true;
                } else {
                    return false;
                }
            }
            // 位移 f0<-f1 f1<-tmp_fr
            f0 = move(f1);
            f1 = move(tmp_fr);
            status.rife->buf_next();    // 将rife实例中的缓存进行位移
            
            f0_rgb.swap(f1_rgb);    // 使用交换而不释放，省去重新分配内存
            rgb_valid[0] = rgb_valid[1];
            rgb_valid[1] = false;
            
            timestep.num -= timestep.den;
            f0_index = f1_index++;
            f0_pts = f1_pts;
            f1_pts = f1.fr->pts;
        }
    }
    
    if (timestep.num==0) { ++fr_index; fr=f0; return true; }

    fr = _makeMiddelFrame(av_q2d(timestep));
    return true;
}