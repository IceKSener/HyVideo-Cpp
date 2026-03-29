#include "FrameGetter/RealESRGANFrameGetter.hpp"

#include <sstream>

#include "utils/Logger.hpp"

using namespace std;
using Mat = ncnn::Mat;

RealESRGANFrameGetter::RealESRGANFrameGetter(const shared_ptr<IFreamGetter> &getter, const Args &args)
    : getter(getter)
{
    info.model = args.model;
    info.use_gpu = args.use_gpu;
    info.gpu_index = args.gpu_index;
    info.scale = args.scale;
    info.tilesize = args.tilesize;
}

RealESRGANFrameGetter::RealESRGANFrameGetter(RealESRGANFrameGetter &&rfg)
    : info(move(rfg.info))
    , status(move(rfg.status))
    , getter(move(rfg.getter))
    , cvt(rfg.cvt)
{
    rfg.cvt = nullptr;
}

RealESRGANFrameGetter::~RealESRGANFrameGetter() {
    if (cvt) delete cvt;
}

bool RealESRGANFrameGetter::nextFrame(HvFrame& fr) {
    if (status.is_end) return false;
    auto& fr_in = status.fr_in, &fr_rgb=status.fr_rgb;
    const int scale = info.scale;
    
    if (!getter->nextFrame(fr_in)) {
        status.is_end = getter->isEnd();
        return false;
    }

    const int w=fr_in.fr->width, h=fr_in.fr->height;

    if(!status.realesrgan) _initRealESRGAN();

    Mat mi; // mi仅用于存储图片宽高和数据地址
    if (fr_in.fr->format==AV_PIX_FMT_RGB24 && w*3==fr_in.fr->linesize[0]) {
        mi = Mat(w, h, fr_in.fr->data[0], (size_t)3, 3);
    } else {
        if (!cvt) _initConverter(w, h);
        fr_rgb.createBuffer(w, h, AV_PIX_FMT_RGB24, 1);
        cvt->convert(fr_in, fr_rgb);
        mi = Mat(w, h, fr_rgb.fr->data[0], (size_t)3, 3);
    }

    fr.createBuffer(w*scale, h*scale, AV_PIX_FMT_RGB24, 1);
    Mat mo(w*scale, h*scale, fr.fr->data[0], (size_t)3, 3);
    status.realesrgan->process(mi, mo);
    av_frame_copy_props(fr.fr, fr_in.fr);

    return true;
}

void RealESRGANFrameGetter::_initRealESRGAN() {
    // TODO 不适合多线程使用一个模型实例
    auto& esrgan = status.realesrgan;
    const int scale = info.scale;

    if (!info.use_gpu) {
        int gpu_index = ncnn::get_default_gpu_index();
        AvLog("RealESRGAN不支持非GPU模式运行，已自动选择GPU%d: %s\n"
            , gpu_index, ncnn::get_gpu_info(gpu_index).device_name());
            info.use_gpu = true;
            info.gpu_index = gpu_index;
    }

    // TODO 分析输入模型的参数(TTA等)
    bool tta_mode = false;
        
    stringstream _model_name, _key;
    _model_name << info.model;
    if (info.model == "realesr-animevideov3") _model_name << "-x" << scale;
    const string model_root = _model_name.str();

    _key << model_root;
    if (tta_mode) _key << "-TTA";
    const string key = _key.str();

    if (esrgan = _realesrgans[key])
        return;
    
    esrgan = new RealESRGAN(info.gpu_index, tta_mode);
    
    string full_root = "./models/RealESRGAN/" + model_root;
#ifdef _WIN32
    wstring w_full_root(full_root.begin(), full_root.end());
    if(esrgan->load(w_full_root+L".param", w_full_root+L".bin")) throw model_root+"模型打开失败";
#else
    if(esrgan->load(full_root+".param", full_root+".bin")) throw model_root+"模型打开失败";
#endif
    esrgan->scale = scale;
    if (info.tilesize != 0) {
        esrgan->tilesize = info.tilesize;
    } else {
        uint32_t heap_budget = ncnn::get_gpu_device(info.gpu_index)->get_heap_budget();
        if (heap_budget > 1900)     esrgan->tilesize=200;
        else if (heap_budget > 550) esrgan->tilesize=100;
        else if (heap_budget > 190) esrgan->tilesize=64;
        else                        esrgan->tilesize=32;
    }
    esrgan->prepadding = 10;
    
    _realesrgans[key] = esrgan;
}

void RealESRGANFrameGetter::_initConverter(int width, int height) {
    if (cvt) return;
    cvt = new FrameConvert(width, height, AV_PIX_FMT_RGB24);
}