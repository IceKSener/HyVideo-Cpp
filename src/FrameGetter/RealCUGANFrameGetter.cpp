#include "FrameGetter/RealCUGANFrameGetter.hpp"

#include <sstream>

using namespace std;
using Mat = ncnn::Mat;

RealCUGANFrameGetter::RealCUGANFrameGetter(const shared_ptr<IFreamGetter> &getter, const Args &args)
    : getter(getter)
{
    info.model = args.model;
    info.use_gpu = args.use_gpu;
    info.gpu_index = args.gpu_index;
    info.noise = args.noise;
    info.scale = args.scale;
    info.syncgap = args.syncgap;
    info.tilesize = args.tilesize;
}

RealCUGANFrameGetter::RealCUGANFrameGetter(RealCUGANFrameGetter &&rfg)
    : info(move(rfg.info))
    , status(move(rfg.status))
    , getter(move(rfg.getter))
    , cvt(rfg.cvt)
{
    rfg.cvt = nullptr;
}

RealCUGANFrameGetter::~RealCUGANFrameGetter() {
    if (cvt) delete cvt;
}

bool RealCUGANFrameGetter::nextFrame(HvFrame& fr) {
    if (status.is_end) return false;
    auto& fr_in = status.fr_in;
    const int scale = info.scale;
    
    if (!getter->nextFrame(fr_in)) {
        status.is_end = getter->isEnd();
        return false;
    }

    const int w=fr_in.fr->width, h=fr_in.fr->height;

    if(!status.realcugan) _initRealCUGAN();

    Mat mi; // mi仅用于存储图片宽高和数据地址
    if (fr_in.fr->format == AV_PIX_FMT_RGB24) {
        mi = Mat(w, h, fr_in.fr->data[0], (size_t)3, 3);
    } else {
        if (!cvt) _initConverter(w, h);
        cvt->convert(fr_in, status.fr_rgb);
        mi = Mat(w, h, status.fr_rgb.fr->data[0], (size_t)3, 3);
    }

    fr.createBuffer(w*scale, h*scale, AV_PIX_FMT_RGB24);
    Mat mo(w*scale, h*scale, fr.fr->data[0], (size_t)3, 3);
    status.realcugan->process(mi, mo);
    av_frame_copy_props(fr.fr, fr_in.fr);

    static int _frame_num=0;
    fprintf(stderr, "FRAME#%d\n", ++_frame_num);

    return true;
}

void RealCUGANFrameGetter::_initRealCUGAN() {
    // TODO 不适合多线程使用一个模型实例
    auto& cugan = status.realcugan;
    const int scale = info.scale;

    // TODO 分析输入模型的参数(TTA等)
    bool tta_mode = false;
        
    stringstream _model_name, _key;
    _model_name << info.model << "/up" << scale << "x-";
    if (info.noise == -1) _model_name << "conservative";
    else if (info.noise == 0) _model_name << "no-denoise";
    else _model_name << "denoise" << info.noise << "x";
    const string model_root = _model_name.str();

    _key << model_root;
    if (info.use_gpu) _key << "-G" << info.gpu_index;
    if (tta_mode) _key << "-TTA";
    const string key = _key.str();

    if (cugan = _realcugans[key])
        return;
    
    cugan = new RealCUGAN(info.use_gpu?info.gpu_index:-1, tta_mode, 1);
    
    string full_root = "./models/RealCUGAN/" + model_root;
#ifdef _WIN32
    wstring w_full_root(full_root.begin(), full_root.end());
    if(cugan->load(w_full_root+L".param", w_full_root+L".bin")) throw model_root+"模型打开失败";
#else
    if(cugan->load(full_root+".param", full_root+".bin")) throw model_root+"模型打开失败";
#endif
    cugan->noise = info.noise;
    cugan->scale = scale;
    if (info.tilesize != 0) {
        cugan->tilesize = info.tilesize;
    } else if (info.use_gpu) {
        uint32_t heap_budget = ncnn::get_gpu_device(info.gpu_index)->get_heap_budget();
        if (scale == 2) {
            if (heap_budget > 1300)     cugan->tilesize=400;
            else if (heap_budget > 800) cugan->tilesize=300;
            else if (heap_budget > 400) cugan->tilesize=200;
            else if (heap_budget > 200) cugan->tilesize=100;
            else                        cugan->tilesize=32;
        } else if (scale == 3) {
            if (heap_budget > 3300)     cugan->tilesize=400;
            else if (heap_budget > 1900) cugan->tilesize=300;
            else if (heap_budget > 950) cugan->tilesize=200;
            else if (heap_budget > 320) cugan->tilesize=100;
            else                        cugan->tilesize=32;
        } else if (scale == 4) {
            if (heap_budget > 1690)     cugan->tilesize=400;
            else if (heap_budget > 980) cugan->tilesize=300;
            else if (heap_budget > 530) cugan->tilesize=200;
            else if (heap_budget > 240) cugan->tilesize=100;
            else                        cugan->tilesize=32;
        }
    } else {
        cugan->tilesize = 400;
    }
    if (scale == 2) cugan->prepadding=18;
    else if (scale == 3) cugan->prepadding=18;
    else if (scale == 4) cugan->prepadding=18;
    else cugan->prepadding=0;
    
    cugan->syncgap = info.syncgap;
    _realcugans[key] = cugan;
}

void RealCUGANFrameGetter::_initConverter(int width, int height) {
    if (cvt) return;
    cvt = new FrameConvert(width, height, AV_PIX_FMT_RGB24);
}