// rife implemented with ncnn library

#include "Common.hpp"
#include "rife/rife.h"

#include <algorithm>
#include <vector>

#include "rife/comps/rife_preproc.comp.hex.h"
#include "rife/comps/rife_postproc.comp.hex.h"
#include "rife/comps/rife_preproc_tta.comp.hex.h"
#include "rife/comps/rife_postproc_tta.comp.hex.h"
#include "rife/comps/rife_flow_tta_avg.comp.hex.h"
#include "rife/comps/rife_v2_flow_tta_avg.comp.hex.h"
#include "rife/comps/rife_v4_flow_tta_avg.comp.hex.h"
#include "rife/comps/rife_flow_tta_temporal_avg.comp.hex.h"
#include "rife/comps/rife_v2_flow_tta_temporal_avg.comp.hex.h"
#include "rife/comps/rife_v4_flow_tta_temporal_avg.comp.hex.h"
#include "rife/comps/rife_out_tta_temporal_avg.comp.hex.h"
#include "rife/comps/rife_v4_timestep.comp.hex.h"
#include "rife/comps/rife_v4_timestep_tta.comp.hex.h"

#include "rife/rife_ops.h"

DEFINE_LAYER_CREATOR(Warp)

RIFE::RIFE(int gpuid, bool _tta_mode, bool _tta_temporal_mode, bool _uhd_mode, int _num_threads, bool _rife_v2, bool _rife_v4, int _padding){
    vkdev = gpuid == -1 ? 0 : ncnn::get_gpu_device(gpuid);

    rife_preproc = 0;
    rife_postproc = 0;
    rife_flow_tta_avg = 0;
    rife_flow_tta_temporal_avg = 0;
    rife_out_tta_temporal_avg = 0;
    rife_v4_timestep = 0;
    rife_uhd_downscale_image = 0;
    rife_uhd_upscale_flow = 0;
    rife_uhd_double_flow = 0;
    rife_v2_slice_flow = 0;
    tta_mode = _tta_mode;
    tta_temporal_mode = _tta_temporal_mode;
    uhd_mode = _uhd_mode;
    num_threads = _num_threads;
    rife_v2 = _rife_v2;
    rife_v4 = _rife_v4;
    padding = _padding;
}

RIFE::~RIFE(){
    // cleanup preprocess and postprocess pipeline
    {
        delete rife_preproc;
        delete rife_postproc;
        delete rife_flow_tta_avg;
        delete rife_flow_tta_temporal_avg;
        delete rife_out_tta_temporal_avg;
        delete rife_v4_timestep;
    }

    if (uhd_mode)
    {
        rife_uhd_downscale_image->destroy_pipeline(flownet.opt);
        delete rife_uhd_downscale_image;

        rife_uhd_upscale_flow->destroy_pipeline(flownet.opt);
        delete rife_uhd_upscale_flow;

        rife_uhd_double_flow->destroy_pipeline(flownet.opt);
        delete rife_uhd_double_flow;
    }

    if (rife_v2)
    {
        rife_v2_slice_flow->destroy_pipeline(flownet.opt);
        delete rife_v2_slice_flow;
    }
}

#if _WIN32
static bool load_param_model(ncnn::Net& net, const std::wstring& modeldir, const wchar_t* name){
    std::wstring parampath=modeldir+L"/"+name+L".param";
    std::wstring modelpath=modeldir+L"/"+name+L".bin";

    {
        FILE* fp = _wfopen(parampath.c_str(), L"rb");
        if (!fp)
        {
            fwprintf(stderr, L"_wfopen %ls failed\n", parampath.c_str());
            return false;
        }
        net.load_param(fp);
        fclose(fp);
    }
    {
        FILE* fp = _wfopen(modelpath.c_str(), L"rb");
        if (!fp)
        {
            fwprintf(stderr, L"_wfopen %ls failed\n", modelpath.c_str());
            return false;
        }
        net.load_model(fp);
        fclose(fp);
    }
    return true;
}
#else
static void load_param_model(ncnn::Net& net, const std::string& modeldir, const char* name){
    char parampath[256];
    char modelpath[256];
    sprintf(parampath, "%s/%s.param", modeldir.c_str(), name);
    sprintf(modelpath, "%s/%s.bin", modeldir.c_str(), name);

    if(net.load_param(parampath) || net.load_model(modelpath)) return false;
    return true;
}
#endif

#if _WIN32
int RIFE::load(const std::wstring& modeldir)
#else
int RIFE::load(const std::string& modeldir)
#endif
{
    ncnn::Option opt;
    opt.num_threads = num_threads;
    opt.use_vulkan_compute = vkdev ? true : false;
    opt.use_fp16_packed = vkdev ? true : false;
    opt.use_fp16_storage = vkdev ? true : false;
    opt.use_fp16_arithmetic = false;
    opt.use_int8_storage = true;

    flownet.opt = opt;
    contextnet.opt = opt;
    fusionnet.opt = opt;

    flownet.set_vulkan_device(vkdev);
    contextnet.set_vulkan_device(vkdev);
    fusionnet.set_vulkan_device(vkdev);

    flownet.register_custom_layer("rife.Warp", Warp_layer_creator);
    contextnet.register_custom_layer("rife.Warp", Warp_layer_creator);
    fusionnet.register_custom_layer("rife.Warp", Warp_layer_creator);

#if _WIN32
    if(!load_param_model(flownet, modeldir, L"flownet")) return -1;
    if (!rife_v4)
    {
        if(!load_param_model(contextnet, modeldir, L"contextnet")) return -1;
        if(!load_param_model(fusionnet, modeldir, L"fusionnet")) return -1;
    }
#else
    if(!load_param_model(flownet, modeldir, "flownet")) return -1;
    if (!rife_v4)
    {
        if(!load_param_model(contextnet, modeldir, "contextnet")) return -1;
        if(!load_param_model(fusionnet, modeldir, "fusionnet")) return -1;
    }
#endif

    // initialize preprocess and postprocess pipeline
    if (vkdev)
    {
        std::vector<ncnn::vk_specialization_type> specializations(1);
        specializations[0].i = 0;

        {
            static std::vector<uint32_t> spirv;
            static ncnn::Mutex lock;
            {
                ncnn::MutexLockGuard guard(lock);
                if (spirv.empty())
                {
                    if (tta_mode)
                        compile_spirv_module(rife_preproc_tta_comp_data, sizeof(rife_preproc_tta_comp_data), opt, spirv);
                    else
                        compile_spirv_module(rife_preproc_comp_data, sizeof(rife_preproc_comp_data), opt, spirv);
                }
            }

            rife_preproc = new ncnn::Pipeline(vkdev);
            rife_preproc->set_optimal_local_size_xyz(8, 8, 3);
            rife_preproc->create(spirv.data(), spirv.size() * 4, specializations);
        }

        {
            static std::vector<uint32_t> spirv;
            static ncnn::Mutex lock;
            {
                ncnn::MutexLockGuard guard(lock);
                if (spirv.empty())
                {
                    if (tta_mode)
                        compile_spirv_module(rife_postproc_tta_comp_data, sizeof(rife_postproc_tta_comp_data), opt, spirv);
                    else
                        compile_spirv_module(rife_postproc_comp_data, sizeof(rife_postproc_comp_data), opt, spirv);
                }
            }

            rife_postproc = new ncnn::Pipeline(vkdev);
            rife_postproc->set_optimal_local_size_xyz(8, 8, 3);
            rife_postproc->create(spirv.data(), spirv.size() * 4, specializations);
        }
    }

    if (vkdev && tta_mode)
    {
        static std::vector<uint32_t> spirv;
        static ncnn::Mutex lock;
        {
            ncnn::MutexLockGuard guard(lock);
            if (spirv.empty())
            {
                if (rife_v4)
                {
                    compile_spirv_module(rife_v4_flow_tta_avg_comp_data, sizeof(rife_v4_flow_tta_avg_comp_data), opt, spirv);
                }
                else if (rife_v2)
                {
                    compile_spirv_module(rife_v2_flow_tta_avg_comp_data, sizeof(rife_v2_flow_tta_avg_comp_data), opt, spirv);
                }
                else
                {
                    compile_spirv_module(rife_flow_tta_avg_comp_data, sizeof(rife_flow_tta_avg_comp_data), opt, spirv);
                }
            }
        }

        std::vector<ncnn::vk_specialization_type> specializations(0);

        rife_flow_tta_avg = new ncnn::Pipeline(vkdev);
        rife_flow_tta_avg->set_optimal_local_size_xyz(8, 8, 1);
        rife_flow_tta_avg->create(spirv.data(), spirv.size() * 4, specializations);
    }

    if (vkdev && tta_temporal_mode)
    {
        static std::vector<uint32_t> spirv;
        static ncnn::Mutex lock;
        {
            ncnn::MutexLockGuard guard(lock);
            if (spirv.empty())
            {
                if (rife_v4)
                {
                    compile_spirv_module(rife_v4_flow_tta_temporal_avg_comp_data, sizeof(rife_v4_flow_tta_temporal_avg_comp_data), opt, spirv);
                }
                else if (rife_v2)
                {
                    compile_spirv_module(rife_v2_flow_tta_temporal_avg_comp_data, sizeof(rife_v2_flow_tta_temporal_avg_comp_data), opt, spirv);
                }
                else
                {
                    compile_spirv_module(rife_flow_tta_temporal_avg_comp_data, sizeof(rife_flow_tta_temporal_avg_comp_data), opt, spirv);
                }
            }
        }

        std::vector<ncnn::vk_specialization_type> specializations(0);

        rife_flow_tta_temporal_avg = new ncnn::Pipeline(vkdev);
        rife_flow_tta_temporal_avg->set_optimal_local_size_xyz(8, 8, 1);
        rife_flow_tta_temporal_avg->create(spirv.data(), spirv.size() * 4, specializations);
    }

    if (vkdev && tta_temporal_mode)
    {
        static std::vector<uint32_t> spirv;
        static ncnn::Mutex lock;
        {
            ncnn::MutexLockGuard guard(lock);
            if (spirv.empty())
            {
                compile_spirv_module(rife_out_tta_temporal_avg_comp_data, sizeof(rife_out_tta_temporal_avg_comp_data), opt, spirv);
            }
        }

        std::vector<ncnn::vk_specialization_type> specializations(0);

        rife_out_tta_temporal_avg = new ncnn::Pipeline(vkdev);
        rife_out_tta_temporal_avg->set_optimal_local_size_xyz(8, 8, 1);
        rife_out_tta_temporal_avg->create(spirv.data(), spirv.size() * 4, specializations);
    }

    if (uhd_mode)
    {
        {
            rife_uhd_downscale_image = ncnn::create_layer("Interp");
            rife_uhd_downscale_image->vkdev = vkdev;

            ncnn::ParamDict pd;
            pd.set(0, 2);// bilinear
            pd.set(1, 0.5f);
            pd.set(2, 0.5f);
            rife_uhd_downscale_image->load_param(pd);

            rife_uhd_downscale_image->create_pipeline(opt);
        }
        {
            rife_uhd_upscale_flow = ncnn::create_layer("Interp");
            rife_uhd_upscale_flow->vkdev = vkdev;

            ncnn::ParamDict pd;
            pd.set(0, 2);// bilinear
            pd.set(1, 2.f);
            pd.set(2, 2.f);
            rife_uhd_upscale_flow->load_param(pd);

            rife_uhd_upscale_flow->create_pipeline(opt);
        }
        {
            rife_uhd_double_flow = ncnn::create_layer("BinaryOp");
            rife_uhd_double_flow->vkdev = vkdev;

            ncnn::ParamDict pd;
            pd.set(0, 2);// mul
            pd.set(1, 1);// with_scalar
            pd.set(2, 2.f);// b
            rife_uhd_double_flow->load_param(pd);

            rife_uhd_double_flow->create_pipeline(opt);
        }
    }

    if (rife_v2)
    {
        {
            rife_v2_slice_flow = ncnn::create_layer("Slice");
            rife_v2_slice_flow->vkdev = vkdev;

            ncnn::Mat slice_points(2);
            slice_points.fill<int>(-233);

            ncnn::ParamDict pd;
            pd.set(0, slice_points);
            pd.set(1, 0);// axis

            rife_v2_slice_flow->load_param(pd);

            rife_v2_slice_flow->create_pipeline(opt);
        }
    }

    if (rife_v4)
    {
        if (vkdev)
        {
            static std::vector<uint32_t> spirv;
            static ncnn::Mutex lock;
            {
                ncnn::MutexLockGuard guard(lock);
                if (spirv.empty())
                {
                    if (tta_mode)
                        compile_spirv_module(rife_v4_timestep_tta_comp_data, sizeof(rife_v4_timestep_tta_comp_data), opt, spirv);
                    else
                        compile_spirv_module(rife_v4_timestep_comp_data, sizeof(rife_v4_timestep_comp_data), opt, spirv);
                }
            }

            std::vector<ncnn::vk_specialization_type> specializations;

            rife_v4_timestep = new ncnn::Pipeline(vkdev);
            rife_v4_timestep->set_optimal_local_size_xyz(8, 8, 1);
            rife_v4_timestep->create(spirv.data(), spirv.size() * 4, specializations);
        }
    }

    return 0;
}


class RIFE::GpuContext{
public:
    ncnn::VkAllocator* blob_vkallocator;
    ncnn::VkAllocator* staging_vkallocator;
    ncnn::VkCompute& cmd;
    int w_padded, h_padded;
    size_t in_out_tile_elemsize;
    GpuContext(ncnn::VkCompute& cmd, ncnn::VkAllocator* blob_vkallocator, ncnn::VkAllocator* staging_vkallocator, int w_padded, int h_padded, size_t in_out_tile_elemsize)
        : cmd(cmd), blob_vkallocator(blob_vkallocator), staging_vkallocator(staging_vkallocator), w_padded(w_padded), h_padded(h_padded), in_out_tile_elemsize(in_out_tile_elemsize) {}
    // 将图像填充入GPU Mat，并归一化
    void func_img_pad(ncnn::VkMat& in_gpu_padded, ncnn::VkMat& in_gpu, ncnn::Pipeline* rife_preproc){
        in_gpu_padded.create(w_padded, h_padded, 3, in_out_tile_elemsize, 1, blob_vkallocator);
        std::vector<ncnn::VkMat> bindings(2);
        bindings[0] = in_gpu;
        bindings[1] = in_gpu_padded;
        std::vector<ncnn::vk_constant_type> constants(6);
        constants[0].i = in_gpu.w;
        constants[1].i = in_gpu.h;
        constants[2].i = in_gpu.cstep;
        constants[3].i = w_padded;
        constants[4].i = h_padded;
        constants[5].i = in_gpu_padded.cstep;
        cmd.record_pipeline(rife_preproc, bindings, constants, in_gpu_padded);
    }
    // 将图像填充入8个方向的GPU Mat，并归一化
    void func_tta_img_pad(ncnn::VkMat in_gpu_padded[8], ncnn::VkMat& in_gpu, ncnn::Pipeline* rife_preproc_tta){
        for(int i=0; i<8; i++)
            in_gpu_padded[i].create(w_padded, h_padded, 3, in_out_tile_elemsize, 1, blob_vkallocator);
        std::vector<ncnn::VkMat> bindings(9);
        bindings[0] = in_gpu;
        for(int i=0; i<8; i++)
            bindings[i+1] = in_gpu_padded[i];
        std::vector<ncnn::vk_constant_type> constants(6);
        constants[0].i = in_gpu.w;
        constants[1].i = in_gpu.h;
        constants[2].i = in_gpu.cstep;
        constants[3].i = w_padded;
        constants[4].i = h_padded;
        constants[5].i = in_gpu_padded[0].cstep;
        cmd.record_pipeline(rife_preproc_tta, bindings, constants, in_gpu_padded[0]);
    }
    // 将时间填充入GPU Mat
    void func_time_pad(ncnn::VkMat& timestep_gpu_padded, float timestep, ncnn::Pipeline* rife_v4_timestep){
        timestep_gpu_padded.create(w_padded, h_padded, 1, in_out_tile_elemsize, 1, blob_vkallocator);
        std::vector<ncnn::VkMat> bindings(1);
        bindings[0] = timestep_gpu_padded;
        std::vector<ncnn::vk_constant_type> constants(4);
        constants[0].i = w_padded;
        constants[1].i = h_padded;
        constants[2].i = timestep_gpu_padded.cstep;
        constants[3].f = timestep;
        cmd.record_pipeline(rife_v4_timestep, bindings, constants, timestep_gpu_padded);
    }
    // 将时间填充入2个GPU Mat
    void func_tta_time_pad(ncnn::VkMat timestep_gpu_padded[2], float timestep, ncnn::Pipeline* rife_v4_timestep_tta){
        timestep_gpu_padded[0].create(w_padded, h_padded, 1, in_out_tile_elemsize, 1, blob_vkallocator);
        timestep_gpu_padded[1].create(w_padded, h_padded, 1, in_out_tile_elemsize, 1, blob_vkallocator);
        std::vector<ncnn::VkMat> bindings(2);
        bindings[0] = timestep_gpu_padded[0];
        bindings[1] = timestep_gpu_padded[1];
        std::vector<ncnn::vk_constant_type> constants(4);
        constants[0].i = w_padded;
        constants[1].i = h_padded;
        constants[2].i = timestep_gpu_padded[0].cstep;
        constants[3].f = timestep;
        cmd.record_pipeline(rife_v4_timestep_tta, bindings, constants, timestep_gpu_padded[0]);
    }
    // 计算光流n
    void func_calc_flow_n(const ncnn::Net& flownet, ncnn::VkMat& in0_gpu_padded, ncnn::VkMat& in1_gpu_padded, ncnn::VkMat& timestep_gpu_padded, ncnn::VkMat flow[4], int fi){
        ncnn::Extractor ex = flownet.create_extractor();
        ex.set_blob_vkallocator(blob_vkallocator);
        ex.set_workspace_vkallocator(blob_vkallocator);
        ex.set_staging_vkallocator(staging_vkallocator);
        ex.input("in0", in0_gpu_padded);
        ex.input("in1", in1_gpu_padded);
        ex.input("in2", timestep_gpu_padded);
        switch(fi){
            case 3: ex.input("flow2", flow[2]);
            case 2: ex.input("flow1", flow[1]);
            case 1: ex.input("flow0", flow[0]);
            default:{
                char tmp[16];
                sprintf(tmp, "flow%d", fi);
                ex.extract(tmp, flow[fi], cmd);
            }
        }
    }
    // 合并正反向光流
    void func_merge_flows(ncnn::VkMat& flow, ncnn::VkMat& flow_reversed, ncnn::Pipeline* rife_flow_tta_temporal_avg){
        std::vector<ncnn::VkMat> bindings(2);
        bindings[0] = flow;
        bindings[1] = flow_reversed;
        std::vector<ncnn::vk_constant_type> constants(3);
        constants[0].i = flow.w;
        constants[1].i = flow.h;
        constants[2].i = flow.cstep;
        ncnn::VkMat dispatcher;
        dispatcher.w = flow.w;
        dispatcher.h = flow.h;
        dispatcher.c = 1;
        cmd.record_pipeline(rife_flow_tta_temporal_avg, bindings, constants, dispatcher);
    }
    // 求8个方向的光流n的均值
    void func_tta_avg_flows_n(ncnn::VkMat flow[8][4], int fi, ncnn::Pipeline* rife_flow_tta_avg){
        std::vector<ncnn::VkMat> bindings(8);
        for(int i=0; i<8; i++)
            bindings[i] = flow[i][fi];
        std::vector<ncnn::vk_constant_type> constants(3);
        constants[0].i = flow[0][fi].w;
        constants[1].i = flow[0][fi].h;
        constants[2].i = flow[0][fi].cstep;
        ncnn::VkMat dispatcher;
        dispatcher.w = flow[0][fi].w;
        dispatcher.h = flow[0][fi].h;
        dispatcher.c = 1;
        cmd.record_pipeline(rife_flow_tta_avg, bindings, constants, dispatcher);
    }
    // 利用4个光流计算插值结果
    void func_calc_out_by_flow(const ncnn::Net& flownet, ncnn::VkMat& in0_gpu_padded, ncnn::VkMat& in1_gpu_padded, ncnn::VkMat& timestep_gpu_padded, ncnn::VkMat flow[4], ncnn::VkMat out_gpu_padded){
        ncnn::Extractor ex = flownet.create_extractor();
        ex.set_blob_vkallocator(blob_vkallocator);
        ex.set_workspace_vkallocator(blob_vkallocator);
        ex.set_staging_vkallocator(staging_vkallocator);
        ex.input("in0", in0_gpu_padded);
        ex.input("in1", in1_gpu_padded);
        ex.input("in2", timestep_gpu_padded);
        ex.input("flow0", flow[0]);
        ex.input("flow1", flow[1]);
        ex.input("flow2", flow[2]);
        ex.input("flow3", flow[3]);
        ex.extract("out0", out_gpu_padded, cmd);
    }
    // 合并正反向插值结果
    void func_merge_out(ncnn::VkMat& out_gpu_padded, ncnn::VkMat& out_gpu_padded_reversed, ncnn::Pipeline* rife_out_tta_temporal_avg){
        std::vector<ncnn::VkMat> bindings(2);
        bindings[0] = out_gpu_padded;
        bindings[1] = out_gpu_padded_reversed;
        std::vector<ncnn::vk_constant_type> constants(3);
        constants[0].i = out_gpu_padded.w;
        constants[1].i = out_gpu_padded.h;
        constants[2].i = out_gpu_padded.cstep;
        ncnn::VkMat dispatcher;
        dispatcher.w = out_gpu_padded.w;
        dispatcher.h = out_gpu_padded.h;
        dispatcher.c = 3;
        cmd.record_pipeline(rife_out_tta_temporal_avg, bindings, constants, dispatcher);
    }
    // 将8个方向的插值结果合并
    void func_tta_postproc(ncnn::VkMat out_gpu_padded[8], ncnn::VkMat& out_gpu, ncnn::Pipeline* rife_postproc_tta){
        std::vector<ncnn::VkMat> bindings(9);
        for(int i=0; i<8; i++)
            bindings[i] = out_gpu_padded[i];
        bindings[8] = out_gpu;
        std::vector<ncnn::vk_constant_type> constants(6);
        constants[0].i = out_gpu_padded[0].w;
        constants[1].i = out_gpu_padded[0].h;
        constants[2].i = out_gpu_padded[0].cstep;
        constants[3].i = out_gpu.w;
        constants[4].i = out_gpu.h;
        constants[5].i = out_gpu.cstep;
        cmd.record_pipeline(rife_postproc_tta, bindings, constants, out_gpu);
    }
    // 计算插值结果
    void func_calc_out(const ncnn::Net& flownet, ncnn::VkMat& in0_gpu_padded, ncnn::VkMat& in1_gpu_padded, ncnn::VkMat& timestep_gpu_padded, ncnn::VkMat& out_gpu_padded){
        ncnn::Extractor ex = flownet.create_extractor();
        ex.set_blob_vkallocator(blob_vkallocator);
        ex.set_workspace_vkallocator(blob_vkallocator);
        ex.set_staging_vkallocator(staging_vkallocator);
        ex.input("in0", in0_gpu_padded);
        ex.input("in1", in1_gpu_padded);
        ex.input("in2", timestep_gpu_padded);
        ex.extract("out0", out_gpu_padded, cmd);
    }
    // 将插值结果反归一化去除填充并转换回图像格式
    void func_postproc(ncnn::VkMat& out_gpu_padded, ncnn::VkMat& out_gpu, ncnn::Pipeline* rife_postproc){
        std::vector<ncnn::VkMat> bindings(2);
        bindings[0] = out_gpu_padded;
        bindings[1] = out_gpu;
        std::vector<ncnn::vk_constant_type> constants(6);
        constants[0].i = w_padded;
        constants[1].i = h_padded;
        constants[2].i = out_gpu_padded.cstep;
        constants[3].i = out_gpu.w;
        constants[4].i = out_gpu.h;
        constants[5].i = out_gpu.cstep;
        cmd.record_pipeline(rife_postproc, bindings, constants, out_gpu);
    }
};

class RIFE::CpuContext{
public:
    int w,h,c;
    int w_padded, h_padded;
    CpuContext(int w, int h, int c, int w_padded, int h_padded)
        :w(w),h(h),c(c),w_padded(w_padded),h_padded(h_padded){}
    // 将图像填充入Mat，并归一化
    void func_img_pad(ncnn::Mat& in_padded, ncnn::Mat& in){
        in_padded.create(w_padded, h_padded, 3);
        for (int q=0 ; q<3 ; q++){
            float* outptr = in_padded.channel(q);
            int i;
            #pragma omp parallel for
            for (i=0 ; i<h ; i++){
                const float* ptr = in.channel(q).row(i);
                int j = 0;
                for (; j<w ; j++)
                    *outptr++ = *ptr++ * (1 / 255.f);
                for(; j<w_padded ; j++)
                    *outptr++ = 0.f;
            }
            #pragma omp parallel for
            for (i=h ; i<h_padded ; i++){
                for (int j = 0; j < w_padded; j++)
                    *outptr++ = 0.f;
            }
        }
    }
    // 将图像填充入8个方向的Mat，并归一化
    void func_tta_img_pad(ncnn::Mat in_padded[8], ncnn::Mat& in){
        func_img_pad(in_padded[0], in);
        for(int i=1 ; i<4 ; i++)
            in_padded[i].create(w_padded, h_padded, 3);
        for(int i=4 ; i<8 ; i++)
            in_padded[i].create(h_padded, w_padded, 3);
        for (int q = 0; q < 3; q++){
            ncnn::Mat channels[8];
            for(int i=0 ; i<8 ; i++)
                channels[i] = in_padded[i].channel(q);
            #pragma omp parallel for
            for(int i=0 ; i<h_padded ; i++){
                const float* outptr0 = channels[0].row(i);
                float* outptr1 = channels[1].row(i) + w_padded - 1;
                float* outptr2 = channels[2].row(h_padded - 1 - i) + w_padded - 1;
                float* outptr3 = channels[3].row(h_padded - 1 - i);
                for (int j = 0; j < w_padded; j++){
                    float* outptr4 = channels[4].row(j) + i;
                    float* outptr5 = channels[5].row(j) + h_padded - 1 - i;
                    float* outptr6 = channels[6].row(w_padded - 1 - j) + h_padded - 1 - i;
                    float* outptr7 = channels[7].row(w_padded - 1 - j) + i;
                    float v = *outptr0++;
                    *outptr1-- = v;
                    *outptr2-- = v;
                    *outptr3++ = v;
                    *outptr4 = v;
                    *outptr5 = v;
                    *outptr6 = v;
                    *outptr7 = v;
                }
            }
        }
    
    }
    // 计算光流n
    void func_calc_flow_n(const ncnn::Net& flownet, ncnn::Mat& in0_padded, ncnn::Mat& in1_padded, ncnn::Mat& timestep_padded, ncnn::Mat flow[4], int fi){
        ncnn::Extractor ex = flownet.create_extractor();
        ex.input("in0", in0_padded);
        ex.input("in1", in1_padded);
        ex.input("in2", timestep_padded);
        switch (fi){
            case 3: ex.input("flow2", flow[2]);
            case 2: ex.input("flow1", flow[1]);
            case 1: ex.input("flow0", flow[0]);
            default:{
                char tmp[16];
                sprintf(tmp, "flow%d", fi);
                ex.extract(tmp, flow[fi]);
            }
        }
    }
    // 合并正反向光流
    void func_merge_flows(ncnn::Mat& flow, ncnn::Mat& flow_reversed){
        float* flow_x = flow.channel(0);
        float* flow_y = flow.channel(1);
        float* flow_z = flow.channel(2);
        float* flow_w = flow.channel(3);
        float* flow_m = flow.channel(4);
        float* flow_reversed_x = flow_reversed.channel(0);
        float* flow_reversed_y = flow_reversed.channel(1);
        float* flow_reversed_z = flow_reversed.channel(2);
        float* flow_reversed_w = flow_reversed.channel(3);
        float* flow_reversed_m = flow_reversed.channel(4);
        #pragma omp parallel for
        for (int i = 0; i < flow.h; i++){
            for (int j = 0; j < flow.w; j++){
                float x = (*flow_x + *flow_reversed_z) * 0.5f;
                float y = (*flow_y + *flow_reversed_w) * 0.5f;
                float z = (*flow_z + *flow_reversed_x) * 0.5f;
                float w = (*flow_w + *flow_reversed_y) * 0.5f;
                float m = (*flow_m - *flow_reversed_m) * 0.5f;

                *flow_x++ = x;
                *flow_y++ = y;
                *flow_z++ = z;
                *flow_w++ = w;
                *flow_m++ = m;
                *flow_reversed_x++ = z;
                *flow_reversed_y++ = w;
                *flow_reversed_z++ = x;
                *flow_reversed_w++ = y;
                *flow_reversed_m++ = -m;
            }
        }
    }
    // // 求8个方向的光流n的均值
    void func_tta_avg_flows_n(ncnn::Mat flow[8][4], int fi){
        ncnn::Mat flow_v[5][8];
        // flow_v[0] = x, flow_v[1] = y ,flow_v[2] = z, flow_v[3] = w, flow_v[4] = m,
        for(int i=0 ; i<5 ; i++)
            for(int j=0 ; j<8 ; j++)
                flow_v[i][j] = flow[j][fi].channel(i);
        #pragma omp parallel for
        for(int i=0 ; i<flow_v[0][0].h ; i++){
            float* v[5][8];
            auto& flow_x0 = flow_v[0][0];
            for(int j=0 ; j<5 ; ++j){
                v[j][0] = flow_v[j][0].row(i);
                v[j][1] = flow_v[j][1].row(i) + flow_x0.w - 1;
                v[j][2] = flow_v[j][2].row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                v[j][3] = flow_v[j][3].row(flow_x0.h - 1 - i);
            }
            for(int j=0 ; j<flow_x0.w ; j++){
                for(int k=0 ; k<5 ; k++){
                    v[k][4] = flow_v[k][4].row(j) + i;
                    v[k][5] = flow_v[k][5].row(j) + flow_x0.h - 1 - i;
                    v[k][6] = flow_v[k][6].row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                    v[k][7] = flow_v[k][7].row(flow_x0.w - 1 - j) + i;
                }
                float x = (*v[0][0] + -*v[0][1] + -*v[0][2] + *v[0][3] + *v[1][4] + *v[1][5] + -*v[1][6] + -*v[1][7]) * 0.125f;
                float y = (*v[1][0] + *v[1][1] + -*v[1][2] + -*v[1][3] + *v[0][4] + -*v[0][5] + -*v[0][6] + *v[0][7]) * 0.125f;
                float z = (*v[2][0] + -*v[2][1] + -*v[2][2] + *v[2][3] + *v[3][4] + *v[3][5] + -*v[3][6] + -*v[3][7]) * 0.125f;
                float w = (*v[3][0] + *v[3][1] + -*v[3][2] + -*v[3][3] + *v[2][4] + -*v[2][5] + -*v[2][6] + *v[2][7]) * 0.125f;
                float m = (*v[4][0] + *v[4][1] + *v[4][2] + *v[4][3] + *v[4][4] + *v[4][5] + *v[4][6] + *v[4][7]) * 0.125f;

                *v[0][0]++ = x;
                *v[0][1]-- = -x;
                *v[0][2]-- = -x;
                *v[0][3]++ = x;
                *v[0][4] = y;
                *v[0][5] = -y;
                *v[0][6] = -y;
                *v[0][7] = y;

                *v[1][0]++ = y;
                *v[1][1]-- = y;
                *v[1][2]-- = -y;
                *v[1][3]++ = -y;
                *v[1][4] = x;
                *v[1][5] = x;
                *v[1][6] = -x;
                *v[1][7] = -x;

                *v[2][0]++ = z;
                *v[2][1]-- = -z;
                *v[2][2]-- = -z;
                *v[2][3]++ = z;
                *v[2][4] = w;
                *v[2][5] = -w;
                *v[2][6] = -w;
                *v[2][7] = w;

                *v[3][0]++ = w;
                *v[3][1]-- = w;
                *v[3][2]-- = -w;
                *v[3][3]++ = -w;
                *v[3][4] = z;
                *v[3][5] = z;
                *v[3][6] = -z;
                *v[3][7] = -z;

                *v[4][0]++ = m;
                *v[4][1]-- = m;
                *v[4][2]-- = m;
                *v[4][3]++ = m;
                *v[4][4] = m;
                *v[4][5] = m;
                *v[4][6] = m;
                *v[4][7] = m;
            }
        }
    }
    // 利用4个光流计算插值结果
    void func_calc_out_by_flow(const ncnn::Net& flownet, ncnn::Mat& in0_padded, ncnn::Mat& in1_padded, ncnn::Mat& timestep_padded, ncnn::Mat flow[4], ncnn::Mat out_padded){
        ncnn::Extractor ex = flownet.create_extractor();
        ex.input("in0", in0_padded);
        ex.input("in1", in1_padded);
        ex.input("in2", timestep_padded);
        ex.input("flow0", flow[0]);
        ex.input("flow1", flow[1]);
        ex.input("flow2", flow[2]);
        ex.input("flow3", flow[3]);
        ex.extract("out0", out_padded);
    }
    // 合并正反向插值结果，反归一化，去填充
    void func_merge_out(ncnn::Mat& out_padded, ncnn::Mat& out_padded_reversed, ncnn::Mat& out){
        if(out.empty()) out.create(w, h, c, (size_t)4u, 1);
        for (int q=0 ; q<3 ; q++){
            float* outptr = out.channel(q);
            #pragma omp parallel for
            for (int i=0 ; i<out.h ; i++){
                const float* ptr = out_padded.channel(q).row(i);
                const float* ptr1 = out_padded_reversed.channel(q).row(i);
                for (int j = 0; j<out.w; j++){
                    *outptr++ = (*ptr++ + *ptr1++) * 0.5f * 255.f + 0.5f;
                }
            }
        }
    }
    // 将8个方向的正反向插值结果合并，反归一化，去填充
    void func_tta_tpr_postproc(ncnn::Mat out_padded[8], ncnn::Mat out_padded_reversed[8], ncnn::Mat& out){
        if(out.empty()) out.create(w, h, c);
        for (int q=0 ; q<3 ; q++){
            ncnn::Mat channels[8], channels_rev[8];
            for(int i=0 ; i<8 ; ++i){
                channels[i] = out_padded[i].channel(q);
                channels_rev[i] = out_padded_reversed[i].channel(q);
            }
            float* outptr = out.channel(q);
            #pragma omp parallel for
            for (int i=0 ; i<h ; i++){
                const float* ptr0 = channels[0].row(i);
                const float* ptr1 = channels[1].row(i) + w_padded - 1;
                const float* ptr2 = channels[2].row(h_padded - 1 - i) + w_padded - 1;
                const float* ptr3 = channels[3].row(h_padded - 1 - i);
                const float* ptrr0 = channels_rev[0].row(i);
                const float* ptrr1 = channels_rev[1].row(i) + w_padded - 1;
                const float* ptrr2 = channels_rev[2].row(h_padded - 1 - i) + w_padded - 1;
                const float* ptrr3 = channels_rev[3].row(h_padded - 1 - i);
                for(int j=0 ; j<w ; j++){
                    const float* ptr4 = channels[4].row(j) + i;
                    const float* ptr5 = channels[5].row(j) + h_padded - 1 - i;
                    const float* ptr6 = channels[6].row(w_padded - 1 - j) + h_padded - 1 - i;
                    const float* ptr7 = channels[7].row(w_padded - 1 - j) + i;
                    const float* ptrr4 = channels_rev[4].row(j) + i;
                    const float* ptrr5 = channels_rev[5].row(j) + h_padded - 1 - i;
                    const float* ptrr6 = channels_rev[6].row(w_padded - 1 - j) + h_padded - 1 - i;
                    const float* ptrr7 = channels_rev[7].row(w_padded - 1 - j) + i;
                    float v = (*ptr0++ + *ptr1-- + *ptr2-- + *ptr3++ + *ptr4 + *ptr5 + *ptr6 + *ptr7) / 8;
                    float vr = (*ptrr0++ + *ptrr1-- + *ptrr2-- + *ptrr3++ + *ptrr4 + *ptrr5 + *ptrr6 + *ptrr7) / 8;
                    *outptr++ = (v + vr) * 0.5f * 255.f + 0.5f;
                }
            }
        }
    }
    // 将8个方向的插值结果合并，反归一化，去填充
    void func_tta_postproc(ncnn::Mat out_padded[8], ncnn::Mat& out){
        if(out.empty()) out.create(w, h, c);
        for (int q=0 ; q<3 ; q++){
            ncnn::Mat channels[8];
            for(int i=0 ; i<8 ; ++i)
                channels[i] = out_padded[i].channel(q);
            float* outptr = out.channel(q);
            #pragma omp parallel for
            for (int i=0 ; i<h ; i++){
                const float* ptr0 = channels[0].row(i);
                const float* ptr1 = channels[1].row(i) + w_padded - 1;
                const float* ptr2 = channels[2].row(h_padded - 1 - i) + w_padded - 1;
                const float* ptr3 = channels[3].row(h_padded - 1 - i);
                for (int j=0 ; j<w ; j++){
                    const float* ptr4 = channels[4].row(j) + i;
                    const float* ptr5 = channels[5].row(j) + h_padded - 1 - i;
                    const float* ptr6 = channels[6].row(w_padded - 1 - j) + h_padded - 1 - i;
                    const float* ptr7 = channels[7].row(w_padded - 1 - j) + i;
                    float v = (*ptr0++ + *ptr1-- + *ptr2-- + *ptr3++ + *ptr4 + *ptr5 + *ptr6 + *ptr7) / 8;
                    *outptr++ = v * 255.f + 0.5f;
                }
            }
        }
    }
    // 计算插值结果
    void func_calc_out(const ncnn::Net& flownet, ncnn::Mat& in0_padded, ncnn::Mat& in1_padded, ncnn::Mat& timestep_padded, ncnn::Mat& out_padded){
        ncnn::Extractor ex = flownet.create_extractor();
        ex.input("in0", in0_padded);
        ex.input("in1", in1_padded);
        ex.input("in2", timestep_padded);
        ex.extract("out0", out_padded);
    }
    // 将插值结果反归一化去除填充
    void func_postproc(ncnn::Mat& out_padded, ncnn::Mat& out){
        if(out.empty()) out.create(w, h, c, (size_t)4u, 1);
        for (int q=0 ; q<3 ; q++){
            float* outptr = out.channel(q);
            #pragma omp parallel for
            for (int i=0 ; i<out.h ; i++){
                const float* ptr = out_padded.channel(q).row(i);
                for (int j = 0; j<out.w; j++){
                    *outptr++ = *ptr++ * 255.f + 0.5f;
                }
            }
        }
    }
};

ncnn::Mat RIFE::process(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage) const
{
    if(timestep == 0.f){
        return in0image;
    }else if(timestep == 1.f){
        return in1image;
    }

    if(rife_v4){
        if(vkdev)
            process_v4_gpu(in0image, in1image, timestep, outimage);
        else
            process_v4_cpu(in0image, in1image, timestep, outimage);
    }
    else{
#ifdef RIFE_SUPPORT_V2
        if(vkdev)
            process_gpu(in0image, in1image, timestep, outimage);
        else
            process_cpu(in0image, in1image, timestep, outimage);
#else
        ThrowErr("目前不支持v2等模型补帧，仅支持v4\n");
#endif
    }
    return outimage;
}

ncnn::Mat RIFE::process_buf(const ncnn::Mat &in0image, const ncnn::Mat &in1image, float timestep, ncnn::Mat &outimage){
    if(timestep == 0.f){
        return in0image;
    }else if(timestep == 1.f){
        return in1image;
    }
#ifndef RIFE_SUPPORT_V2
    if(!rife_v4){
        ThrowErr("仅RIFE v4支持缓冲区处理");
    }
#endif

    auto pixel0data = (unsigned char*)in0image.data;
    auto pixel1data = (unsigned char*)in1image.data;
    const int& w = in0image.w;
    const int& h = in0image.h;
    const int channels = 3;

    if(outimage.empty())
        outimage.create(w, h, (size_t)channels, 1);
    
    int w_padded = (w + padding-1) / padding * padding;
    int h_padded = (h + padding-1) / padding * padding;

    if(vkdev){
        ncnn::VkAllocator* blob_vkallocator = vkdev->acquire_blob_allocator();
        ncnn::VkAllocator* staging_vkallocator = vkdev->acquire_staging_allocator();

        ncnn::Option opt = flownet.opt;
        opt.blob_vkallocator = blob_vkallocator;
        opt.workspace_vkallocator = blob_vkallocator;
        opt.staging_vkallocator = staging_vkallocator;

        const size_t in_out_tile_elemsize = opt.use_fp16_storage ? 2u : 4u;
        ncnn::VkCompute cmd(vkdev);
        GpuContext gpu_ctx(cmd, blob_vkallocator, staging_vkallocator, w_padded, h_padded, in_out_tile_elemsize);

        ncnn::VkMat in0_gpu, in1_gpu;
        if(buf0_gpu.empty()){
            ncnn::Mat in;
            if(opt.use_fp16_storage && opt.use_int8_storage)
                in = ncnn::Mat(w, h, pixel0data, (size_t)channels, 1);
            else
                in = ncnn::Mat::from_pixels(pixel0data, ncnn::Mat::PIXEL_RGB, w, h);
            cmd.record_clone(in, in0_gpu, opt);
            if(tta_mode){
                buf0_gpu.resize(8);
                gpu_ctx.func_tta_img_pad(buf0_gpu.data(), in0_gpu, rife_preproc);
            }else{
                buf0_gpu.resize(1);
                gpu_ctx.func_img_pad(buf0_gpu[0], in0_gpu, rife_preproc);
            }
        }
        if(buf1_gpu.empty()){
            ncnn::Mat in;
            if(opt.use_fp16_storage && opt.use_int8_storage)
                in = ncnn::Mat(w, h, pixel1data, (size_t)channels, 1);
            else
                in = ncnn::Mat::from_pixels(pixel1data, ncnn::Mat::PIXEL_RGB, w, h);
            cmd.record_clone(in, in1_gpu, opt);
            if(tta_mode){
                buf1_gpu.resize(8);
                gpu_ctx.func_tta_img_pad(buf1_gpu.data(), in1_gpu, rife_preproc);
            }else{
                buf1_gpu.resize(1);
                gpu_ctx.func_img_pad(buf1_gpu[0], in1_gpu, rife_preproc);
            }
        }

        if(tta_mode){
            if(tta_temporal_mode){
                workflow_v4_tta_temporal_gpu(buf0_gpu.data(), buf1_gpu.data(), timestep, outimage, opt, gpu_ctx);
            }else{
                workflow_v4_tta_gpu(buf0_gpu.data(), buf1_gpu.data(), timestep, outimage, opt, gpu_ctx);
            }
        }else{
            if(tta_temporal_mode){
                workflow_v4_temporal_gpu(buf0_gpu[0], buf1_gpu[0], timestep, outimage, opt, gpu_ctx);
            }else{
                workflow_v4_gpu(buf0_gpu[0], buf1_gpu[0], timestep, outimage, opt, gpu_ctx);
            }
        }
        vkdev->reclaim_blob_allocator(blob_vkallocator);
        vkdev->reclaim_staging_allocator(staging_vkallocator);
    }else{
        CpuContext cpu_ctx(w, h, channels, w_padded, h_padded);
        if(buf0.empty()){
            ncnn::Mat in;
            in = ncnn::Mat::from_pixels(pixel0data, ncnn::Mat::PIXEL_RGB, w, h);
            if(tta_mode){
                buf0.resize(8);
                cpu_ctx.func_tta_img_pad(buf0.data(), in);
            }else{
                buf0.resize(1);
                cpu_ctx.func_img_pad(buf0[0], in);
            }
        }
        if(buf1.empty()){
            ncnn::Mat in;
            in = ncnn::Mat::from_pixels(pixel1data, ncnn::Mat::PIXEL_RGB, w, h);
            if(tta_mode){
                buf1.resize(8);
                cpu_ctx.func_tta_img_pad(buf1.data(), in);
            }else{
                buf1.resize(1);
                cpu_ctx.func_img_pad(buf1[0], in);
            }
        }
        
        if(tta_mode){
            if(tta_temporal_mode){
                workflow_v4_tta_temporal_cpu(buf0.data(), buf1.data(), timestep, outimage, cpu_ctx);
            }else{
                workflow_v4_tta_cpu(buf0.data(), buf1.data(), timestep, outimage, cpu_ctx);
            }
        }else{
            if(tta_temporal_mode){
                workflow_v4_temporal_cpu(buf0[0], buf1[0], timestep, outimage, cpu_ctx);
            }else{
                workflow_v4_cpu(buf0[0], buf1[0], timestep, outimage, cpu_ctx);
            }
        }
    }
    return outimage;
}

void RIFE::buf_next(){
    buf0 = std::move(buf1);
    buf0_gpu = std::move(buf1_gpu);
}

int RIFE::process_v4_gpu(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage) const
{
    auto pixel0data = (unsigned char*)in0image.data;
    auto pixel1data = (unsigned char*)in1image.data;
    const int w = in0image.w;
    const int h = in0image.h;
    const int channels = 3;

    ncnn::VkAllocator* blob_vkallocator = vkdev->acquire_blob_allocator();
    ncnn::VkAllocator* staging_vkallocator = vkdev->acquire_staging_allocator();

    ncnn::Option opt = flownet.opt;
    opt.blob_vkallocator = blob_vkallocator;
    opt.workspace_vkallocator = blob_vkallocator;
    opt.staging_vkallocator = staging_vkallocator;

    // pad to 32n
    int w_padded = (w + padding - 1) / padding * padding;
    int h_padded = (h + padding - 1) / padding * padding;

    const size_t in_out_tile_elemsize = opt.use_fp16_storage ? 2u : 4u;
    ncnn::VkCompute cmd(vkdev);
    GpuContext gpu_ctx(cmd, blob_vkallocator, staging_vkallocator, w_padded, h_padded, in_out_tile_elemsize);

    // upload
    ncnn::VkMat in0_gpu, in1_gpu;
    {
        ncnn::Mat in;
        if(opt.use_fp16_storage && opt.use_int8_storage){
            in = ncnn::Mat(w, h, pixel0data, (size_t)channels, 1);
            cmd.record_clone(in, in0_gpu, opt);
            in = ncnn::Mat(w, h, pixel1data, (size_t)channels, 1);
            cmd.record_clone(in, in1_gpu, opt);
        }else{
            in = ncnn::Mat::from_pixels(pixel0data, ncnn::Mat::PIXEL_RGB, w, h);
            cmd.record_clone(in, in0_gpu, opt);
            in = ncnn::Mat::from_pixels(pixel1data, ncnn::Mat::PIXEL_RGB, w, h);
            cmd.record_clone(in, in1_gpu, opt);
        }
    }

    if(tta_mode){
        ncnn::VkMat in0_gpu_padded[8], in1_gpu_padded[8];
        gpu_ctx.func_tta_img_pad(in0_gpu_padded, in0_gpu, rife_preproc);
        gpu_ctx.func_tta_img_pad(in1_gpu_padded, in1_gpu, rife_preproc);
        if(tta_temporal_mode){
            workflow_v4_tta_temporal_gpu(in0_gpu_padded, in1_gpu_padded, timestep, outimage, opt, gpu_ctx);
        }else{
            workflow_v4_tta_gpu(in0_gpu_padded, in1_gpu_padded, timestep, outimage, opt, gpu_ctx);
        }
    }else{
        ncnn::VkMat in0_gpu_padded, in1_gpu_padded;
        gpu_ctx.func_img_pad(in0_gpu_padded, in0_gpu, rife_preproc);
        gpu_ctx.func_img_pad(in1_gpu_padded, in1_gpu, rife_preproc);
        if(tta_temporal_mode){
            workflow_v4_temporal_gpu(in0_gpu_padded, in1_gpu_padded, timestep, outimage, opt, gpu_ctx);
        }else{
            workflow_v4_gpu(in0_gpu_padded, in1_gpu_padded, timestep, outimage, opt, gpu_ctx);
        }
    }
    vkdev->reclaim_blob_allocator(blob_vkallocator);
    vkdev->reclaim_staging_allocator(staging_vkallocator);

    return 0;
}

int RIFE::process_v4_cpu(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage) const
{
    const unsigned char* pixel0data = (const unsigned char*)in0image.data;
    const unsigned char* pixel1data = (const unsigned char*)in1image.data;
    const int w = in0image.w;
    const int h = in0image.h;
    const int channels = 3;

    // pad to 32n
    int w_padded, h_padded;
    w_padded = (w + padding - 1) / padding * padding;
    h_padded = (h + padding - 1) / padding * padding;

    CpuContext cpu_ctx(w, h, channels, w_padded, h_padded);

    if (tta_mode){
        // preproc and border padding
        ncnn::Mat in0_padded[8], in1_padded[8], in;
        in = ncnn::Mat::from_pixels(pixel0data, ncnn::Mat::PIXEL_RGB, w, h);
        cpu_ctx.func_tta_img_pad(in0_padded, in);
        in = ncnn::Mat::from_pixels(pixel1data, ncnn::Mat::PIXEL_RGB, w, h);
        cpu_ctx.func_tta_img_pad(in1_padded, in);
        in.release();
        if(tta_temporal_mode){
            workflow_v4_tta_temporal_cpu(in0_padded, in1_padded, timestep, outimage, cpu_ctx);
        }else{
            workflow_v4_tta_cpu(in0_padded, in1_padded, timestep, outimage, cpu_ctx);
        }
    }else{
        // preproc and border padding
        ncnn::Mat in0_padded, in1_padded, in;
        in = ncnn::Mat::from_pixels(pixel0data, ncnn::Mat::PIXEL_RGB, w, h);
        cpu_ctx.func_img_pad(in0_padded, in);
        in = ncnn::Mat::from_pixels(pixel1data, ncnn::Mat::PIXEL_RGB, w, h);
        cpu_ctx.func_img_pad(in1_padded, in);
        in.release();
        if(tta_temporal_mode)
            workflow_v4_temporal_cpu(in0_padded, in1_padded, timestep, outimage, cpu_ctx);
        else
            workflow_v4_cpu(in0_padded, in1_padded, timestep, outimage, cpu_ctx);
    }
    return 0;
}

int RIFE::workflow_v4_gpu(ncnn::VkMat &in0_gpu_padded, ncnn::VkMat &in1_gpu_padded, float timestep, ncnn::Mat &out, ncnn::Option &opt, GpuContext &gpu_ctx) const{
    ncnn::VkMat timestep_gpu_padded, out_gpu_padded, out_gpu;
    ncnn::Mat out_tmp;
    gpu_ctx.func_time_pad(timestep_gpu_padded, timestep, rife_v4_timestep);
    gpu_ctx.func_calc_out(flownet, in0_gpu_padded, in1_gpu_padded, timestep_gpu_padded, out_gpu_padded);
    if(opt.use_fp16_storage && opt.use_int8_storage){
        out_gpu.create(out.w, out.h, (size_t)(out.c*out.elemsize), 1, gpu_ctx.blob_vkallocator);
        out_tmp = out;
    }else{
        out_gpu.create(out.w, out.h, (int)(out.c*out.elemsize), (size_t)4u, 1, gpu_ctx.blob_vkallocator);
    }
    gpu_ctx.func_postproc(out_gpu_padded, out_gpu, rife_postproc);
    gpu_ctx.cmd.record_clone(out_gpu, out_tmp, opt);
    gpu_ctx.cmd.submit_and_wait();
    if(!(opt.use_fp16_storage && opt.use_int8_storage)){
        out_tmp.to_pixels((unsigned char*)out.data, ncnn::Mat::PIXEL_RGB);
    }
    return 0;
}
int RIFE::workflow_v4_temporal_gpu(ncnn::VkMat &in0_gpu_padded, ncnn::VkMat &in1_gpu_padded, float timestep, ncnn::Mat &out, ncnn::Option &opt, GpuContext &gpu_ctx) const{
    ncnn::VkMat timestep_gpu_padded, timestep_gpu_padded_reversed;
    ncnn::VkMat flow[4], flow_reversed[4];
    ncnn::VkMat out_gpu_padded, out_gpu_padded_reversed, out_gpu;
    ncnn::Mat out_tmp;
    gpu_ctx.func_time_pad(timestep_gpu_padded, timestep, rife_v4_timestep);
    gpu_ctx.func_time_pad(timestep_gpu_padded_reversed, timestep, rife_v4_timestep);
    for(int fi=0 ; fi<4 ; fi++){
        gpu_ctx.func_calc_flow_n(flownet, in0_gpu_padded, in1_gpu_padded, timestep_gpu_padded, flow, fi);
        gpu_ctx.func_calc_flow_n(flownet, in1_gpu_padded, in0_gpu_padded, timestep_gpu_padded_reversed, flow_reversed, fi);
        gpu_ctx.func_merge_flows(flow[fi], flow_reversed[fi], rife_flow_tta_temporal_avg);
    }
    gpu_ctx.func_calc_out_by_flow(flownet, in0_gpu_padded, in1_gpu_padded, timestep_gpu_padded, flow, out_gpu_padded);
    gpu_ctx.func_calc_out_by_flow(flownet, in1_gpu_padded, in0_gpu_padded, timestep_gpu_padded_reversed, flow_reversed, out_gpu_padded_reversed);
    gpu_ctx.func_merge_out(out_gpu_padded, out_gpu_padded_reversed, rife_out_tta_temporal_avg);
    if(opt.use_fp16_storage && opt.use_int8_storage){
        out_gpu.create(out.w, out.h, (size_t)(out.c*out.elemsize), 1, gpu_ctx.blob_vkallocator);
        out_tmp = out;
    }else{
        out_gpu.create(out.w, out.h, (int)(out.c*out.elemsize), (size_t)4u, 1, gpu_ctx.blob_vkallocator);
    }
    gpu_ctx.func_postproc(out_gpu_padded, out_gpu, rife_postproc);
    gpu_ctx.cmd.record_clone(out_gpu, out_tmp, opt);
    gpu_ctx.cmd.submit_and_wait();
    if(!(opt.use_fp16_storage && opt.use_int8_storage)){
        out_tmp.to_pixels((unsigned char*)out.data, ncnn::Mat::PIXEL_RGB);
    }
    return 0;
}
int RIFE::workflow_v4_tta_gpu(ncnn::VkMat in0_gpu_padded[8], ncnn::VkMat in1_gpu_padded[8], float timestep, ncnn::Mat &out, ncnn::Option &opt, GpuContext &gpu_ctx) const{
    ncnn::VkMat timestep_gpu_padded[2];
    ncnn::VkMat flow[8][4];
    ncnn::VkMat out_gpu_padded[8], out_gpu;
    ncnn::Mat out_tmp;
    gpu_ctx.func_tta_time_pad(timestep_gpu_padded, timestep, rife_v4_timestep);
    for(int fi=0 ; fi<4 ; fi++){
        for(int ti=0 ; ti<8 ; ti++)
            gpu_ctx.func_calc_flow_n(flownet, in0_gpu_padded[ti], in1_gpu_padded[ti], timestep_gpu_padded[ti/4], flow[ti], fi);
        gpu_ctx.func_tta_avg_flows_n(flow, fi, rife_flow_tta_avg);
    }
    for(int ti=0 ; ti<8 ; ti++)
        gpu_ctx.func_calc_out_by_flow(flownet, in0_gpu_padded[ti], in1_gpu_padded[ti], timestep_gpu_padded[ti/4], flow[ti], out_gpu_padded[ti]);
    if(opt.use_fp16_storage && opt.use_int8_storage){
        out_gpu.create(out.w, out.h, (size_t)(out.c*out.elemsize), 1, gpu_ctx.blob_vkallocator);
        out_tmp = out;
    }else{
        out_gpu.create(out.w, out.h, (int)(out.c*out.elemsize), (size_t)4u, 1, gpu_ctx.blob_vkallocator);
    }
    gpu_ctx.func_tta_postproc(out_gpu_padded, out_gpu, rife_postproc);
    gpu_ctx.cmd.record_clone(out_gpu, out_tmp, opt);
    gpu_ctx.cmd.submit_and_wait();
    if(!(opt.use_fp16_storage && opt.use_int8_storage)){
        out_tmp.to_pixels((unsigned char*)out.data, ncnn::Mat::PIXEL_RGB);
    }
    return 0;
}
int RIFE::workflow_v4_tta_temporal_gpu(ncnn::VkMat in0_gpu_padded[8], ncnn::VkMat in1_gpu_padded[8], float timestep, ncnn::Mat &out, ncnn::Option &opt, GpuContext &gpu_ctx) const{
    ncnn::VkMat timestep_gpu_padded[2], timestep_gpu_padded_reversed[2];
    ncnn::VkMat flow[8][4], flow_reversed[8][4];
    ncnn::VkMat out_gpu_padded[8], out_gpu_padded_reversed[8], out_gpu;
    ncnn::Mat out_tmp;
    gpu_ctx.func_tta_time_pad(timestep_gpu_padded, timestep, rife_v4_timestep);
    gpu_ctx.func_tta_time_pad(timestep_gpu_padded_reversed, 1.f-timestep, rife_v4_timestep);
    for(int fi=0 ; fi<4 ; fi++){
        for(int ti=0 ; ti<8 ; ti++){
            gpu_ctx.func_calc_flow_n(flownet, in0_gpu_padded[ti], in1_gpu_padded[ti], timestep_gpu_padded[ti/4], flow[ti], fi);
            gpu_ctx.func_calc_flow_n(flownet, in1_gpu_padded[ti], in0_gpu_padded[ti], timestep_gpu_padded_reversed[ti/4], flow_reversed[ti], fi);
            gpu_ctx.func_merge_flows(flow[ti][fi], flow_reversed[ti][fi], rife_flow_tta_temporal_avg);
        }
        gpu_ctx.func_tta_avg_flows_n(flow, fi, rife_flow_tta_avg);
        gpu_ctx.func_tta_avg_flows_n(flow_reversed, fi, rife_flow_tta_avg);
    }
    for(int ti=0 ; ti<8 ; ti++){
        gpu_ctx.func_calc_out_by_flow(flownet, in0_gpu_padded[ti], in1_gpu_padded[ti], timestep_gpu_padded[ti/4], flow[ti], out_gpu_padded[ti]);
        gpu_ctx.func_calc_out_by_flow(flownet, in1_gpu_padded[ti], in0_gpu_padded[ti], timestep_gpu_padded_reversed[ti/4], flow_reversed[ti], out_gpu_padded_reversed[ti]);
        gpu_ctx.func_merge_out(out_gpu_padded[ti], out_gpu_padded_reversed[ti], rife_out_tta_temporal_avg);
    }
    if(opt.use_fp16_storage && opt.use_int8_storage){
        out_gpu.create(out.w, out.h, (size_t)(out.c*out.elemsize), 1, gpu_ctx.blob_vkallocator);
        out_tmp = out;
    }else{
        out_gpu.create(out.w, out.h, (int)(out.c*out.elemsize), (size_t)4u, 1, gpu_ctx.blob_vkallocator);
    }
    gpu_ctx.func_tta_postproc(out_gpu_padded, out_gpu, rife_postproc);
    gpu_ctx.cmd.record_clone(out_gpu, out_tmp, opt);
    gpu_ctx.cmd.submit_and_wait();
    if(!(opt.use_fp16_storage && opt.use_int8_storage)){
        out_tmp.to_pixels((unsigned char*)out.data, ncnn::Mat::PIXEL_RGB);
    }
    return 0;
}

int RIFE::workflow_v4_cpu(ncnn::Mat &in0_padded, ncnn::Mat &in1_padded, float timestep, ncnn::Mat &out,CpuContext &cpu_ctx) const{
    ncnn::Mat timestep_padded, out_padded, out_tmp;
    timestep_padded.create(cpu_ctx.w_padded, cpu_ctx.h_padded, 1);
    timestep_padded.fill(timestep);
    cpu_ctx.func_calc_out(flownet, in0_padded, in1_padded, timestep_padded, out_padded);
    cpu_ctx.func_postproc(out_padded, out_tmp);
    out_tmp.to_pixels((unsigned char*)out.data, ncnn::Mat::PIXEL_RGB);
    return 0;
}
int RIFE::workflow_v4_temporal_cpu(ncnn::Mat &in0_padded, ncnn::Mat &in1_padded, float timestep, ncnn::Mat &out,CpuContext &cpu_ctx) const{
    ncnn::Mat timestep_padded, timestep_padded_reversed;
    ncnn::Mat flow[4], flow_reversed[4];
    ncnn::Mat out_padded, out_padded_reversed, out_tmp;
    timestep_padded.create(cpu_ctx.w_padded, cpu_ctx.h_padded, 1);
    timestep_padded_reversed.create(cpu_ctx.w_padded, cpu_ctx.h_padded, 1);
    timestep_padded.fill(timestep);
    timestep_padded_reversed.fill(1.f-timestep);
    for(int fi=0; fi<4 ; fi++){
        cpu_ctx.func_calc_flow_n(flownet, in0_padded, in1_padded, timestep_padded, flow, fi);
        cpu_ctx.func_calc_flow_n(flownet, in1_padded, in0_padded, timestep_padded_reversed, flow_reversed, fi);
        cpu_ctx.func_merge_flows(flow[fi], flow_reversed[fi]);
    }
    cpu_ctx.func_calc_out_by_flow(flownet, in0_padded, in1_padded, timestep_padded, flow, out_padded);
    cpu_ctx.func_calc_out_by_flow(flownet, in1_padded, in0_padded, timestep_padded_reversed, flow_reversed, out_padded_reversed);
    cpu_ctx.func_merge_out(out_padded, out_padded_reversed, out_tmp);
    out_tmp.to_pixels((unsigned char*)out.data, ncnn::Mat::PIXEL_RGB);
    return 0;

}
int RIFE::workflow_v4_tta_cpu(ncnn::Mat in0_padded[8], ncnn::Mat in1_padded[8], float timestep, ncnn::Mat &out,CpuContext &cpu_ctx) const{
    ncnn::Mat timestep_padded[2], flow[8][4];
    ncnn::Mat out_padded[8], out_tmp;
    timestep_padded[0].create(cpu_ctx.w_padded, cpu_ctx.h_padded, 1);
    timestep_padded[1].create(cpu_ctx.h_padded, cpu_ctx.w_padded, 1);
    timestep_padded[0].fill(timestep);
    timestep_padded[1].fill(timestep);
    for(int fi=0 ; fi<4 ; fi++){
        for(int ti=0 ; ti<8 ; ti++)
            cpu_ctx.func_calc_flow_n(flownet, in0_padded[ti], in1_padded[ti], timestep_padded[ti/4], flow[ti], fi);
        cpu_ctx.func_tta_avg_flows_n(flow, fi);
    }
    for(int ti=0 ; ti<8 ; ti++)
        cpu_ctx.func_calc_out_by_flow(flownet, in0_padded[ti], in1_padded[ti], timestep_padded[ti/4], flow[ti], out_padded[ti]);
    cpu_ctx.func_tta_postproc(out_padded, out_tmp);
    out_tmp.to_pixels((unsigned char*)out.data, ncnn::Mat::PIXEL_RGB);
    return 0;
}
int RIFE::workflow_v4_tta_temporal_cpu(ncnn::Mat in0_padded[8], ncnn::Mat in1_padded[8], float timestep, ncnn::Mat &out,CpuContext &cpu_ctx) const{
    ncnn::Mat timestep_padded[2], timestep_padded_reversed[2];
    ncnn::Mat flow[8][4], flow_reversed[8][4];
    ncnn::Mat out_padded[8], out_padded_reversed[8], out_tmp;
    timestep_padded[0].create(cpu_ctx.w_padded, cpu_ctx.h_padded, 1);
    timestep_padded[1].create(cpu_ctx.h_padded, cpu_ctx.w_padded, 1);
    timestep_padded[0].fill(timestep);
    timestep_padded[1].fill(timestep);
    timestep_padded_reversed[0].create(cpu_ctx.w_padded, cpu_ctx.h_padded, 1);
    timestep_padded_reversed[1].create(cpu_ctx.h_padded, cpu_ctx.w_padded, 1);
    timestep_padded_reversed[0].fill(1.f-timestep);
    timestep_padded_reversed[1].fill(1.f-timestep);
    for(int fi=0 ; fi<4 ; fi++){
        for(int ti=0 ; ti<8 ; ti++){
            cpu_ctx.func_calc_flow_n(flownet, in0_padded[ti], in1_padded[ti], timestep_padded[ti/4], flow[ti], fi);
            cpu_ctx.func_calc_flow_n(flownet, in1_padded[ti], in0_padded[ti], timestep_padded_reversed[ti/4], flow_reversed[ti], fi);
            cpu_ctx.func_merge_flows(flow[ti][fi], flow_reversed[ti][fi]);
        }
        cpu_ctx.func_tta_avg_flows_n(flow, fi);
        cpu_ctx.func_tta_avg_flows_n(flow_reversed, fi);
    }
    for(int ti=0 ; ti<8 ; ti++){
        cpu_ctx.func_calc_out_by_flow(flownet, in0_padded[ti], in1_padded[ti], timestep_padded[ti/4], flow[ti], out_padded[ti]);
        cpu_ctx.func_calc_out_by_flow(flownet, in1_padded[ti], in0_padded[ti], timestep_padded_reversed[ti/4], flow_reversed[ti], out_padded_reversed[ti]);
    }
    cpu_ctx.func_tta_tpr_postproc(out_padded, out_padded_reversed, out_tmp);
    out_tmp.to_pixels((unsigned char*)out.data, ncnn::Mat::PIXEL_RGB);
    return 0;
}
