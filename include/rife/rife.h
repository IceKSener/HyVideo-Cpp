// rife implemented with ncnn library

#ifndef RIFE_H
#define RIFE_H

#include <string>

// ncnn
#include "net.h"

namespace RifeInnerContext{
class GpuContext;
class CpuContext;
};
class RIFE
{
public:
    RIFE(int gpuid, bool tta_mode = false, bool tta_temporal_mode = false, bool uhd_mode = false, int num_threads = 1, bool rife_v2 = false, bool rife_v4 = false, int padding = 32);
    ~RIFE();

#if _WIN32
    int load(const std::wstring& modeldir);
#else
    int load(const std::string& modeldir);
#endif
    // 接收RGBRGBRGB输入
    ncnn::Mat process(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage) const;

    ncnn::Mat process_buf(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage);
    void buf_next();

private:
    int process_v4_cpu(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage) const;
    int process_v4_gpu(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage) const;
    int process_cpu(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage) const;
    int process_gpu(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage) const;

    int workflow_v4_gpu(ncnn::VkMat& in0_gpu_padded, ncnn::VkMat& in1_gpu_padded, float timestep, ncnn::Mat& out, ncnn::Option& opt, RifeInnerContext::GpuContext& gpu_ctx) const;
    int workflow_v4_temporal_gpu(ncnn::VkMat& in0_gpu_padded, ncnn::VkMat& in1_gpu_padded, float timestep, ncnn::Mat& out, ncnn::Option& opt, RifeInnerContext::GpuContext& gpu_ctx) const;
    int workflow_v4_tta_gpu(ncnn::VkMat in0_gpu_padded[8], ncnn::VkMat in1_gpu_padded[8], float timestep, ncnn::Mat& out, ncnn::Option& opt, RifeInnerContext::GpuContext& gpu_ctx) const;
    int workflow_v4_tta_temporal_gpu(ncnn::VkMat in0_gpu_padded[8], ncnn::VkMat in1_gpu_padded[8], float timestep, ncnn::Mat& out, ncnn::Option& opt, RifeInnerContext::GpuContext& gpu_ctx) const;

    int workflow_v4_cpu(ncnn::Mat& in0_padded, ncnn::Mat& in1_padded, float timestep, ncnn::Mat& out, RifeInnerContext::CpuContext& cpu_ctx) const;
    int workflow_v4_temporal_cpu(ncnn::Mat& in0_padded, ncnn::Mat& in1_padded, float timestep, ncnn::Mat& out, RifeInnerContext::CpuContext& cpu_ctx) const;
    int workflow_v4_tta_cpu(ncnn::Mat in0_padded[8], ncnn::Mat in1_padded[8], float timestep, ncnn::Mat& out, RifeInnerContext::CpuContext& cpu_ctx) const;
    int workflow_v4_tta_temporal_cpu(ncnn::Mat in0_padded[8], ncnn::Mat in1_padded[8], float timestep, ncnn::Mat& out, RifeInnerContext::CpuContext& cpu_ctx) const;

    ncnn::VulkanDevice* vkdev;
    ncnn::Net flownet;
    ncnn::Net contextnet;
    ncnn::Net fusionnet;
    ncnn::Pipeline* rife_preproc;
    ncnn::Pipeline* rife_postproc;
    ncnn::Pipeline* rife_flow_tta_avg;
    ncnn::Pipeline* rife_flow_tta_temporal_avg;
    ncnn::Pipeline* rife_out_tta_temporal_avg;
    ncnn::Pipeline* rife_v4_timestep;
    ncnn::Layer* rife_uhd_downscale_image;
    ncnn::Layer* rife_uhd_upscale_flow;
    ncnn::Layer* rife_uhd_double_flow;
    ncnn::Layer* rife_v2_slice_flow;
    bool tta_mode;
    bool tta_temporal_mode;
    bool uhd_mode;
    int num_threads;
    bool rife_v2;
    bool rife_v4;
    int padding;
    
    //为连续补帧做的缓存
    std::vector<ncnn::Mat> buf0;
    std::vector<ncnn::Mat> buf1;
    std::vector<ncnn::VkMat> buf0_gpu;
    std::vector<ncnn::VkMat> buf1_gpu;
    int width,height,channels;
};

#endif // RIFE_H
