// TODO rife v2
#ifdef RIFE_SUPPORT_V2
#include "Common.hpp"
#include "rife/rife.h"

#include <algorithm>
#include <vector>

int RIFE::process_gpu(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage) const
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

    ncnn::VkMat out_gpu;

    if (tta_mode){
        // preproc
        ncnn::VkMat in0_gpu_padded[8], in1_gpu_padded[8];
        gpu_ctx.func_tta_img_pad(in0_gpu_padded, in0_gpu, rife_preproc);
        gpu_ctx.func_tta_img_pad(in1_gpu_padded, in1_gpu, rife_preproc);

        ncnn::VkMat flow[8];
        for(int ti = 0; ti < 8; ti++){
            // flownet
            ncnn::Extractor ex = flownet.create_extractor();
            ex.set_blob_vkallocator(blob_vkallocator);
            ex.set_workspace_vkallocator(blob_vkallocator);
            ex.set_staging_vkallocator(staging_vkallocator);

            if (uhd_mode)
            {
                ncnn::VkMat in0_gpu_padded_downscaled;
                ncnn::VkMat in1_gpu_padded_downscaled;
                rife_uhd_downscale_image->forward(in0_gpu_padded[ti], in0_gpu_padded_downscaled, cmd, opt);
                rife_uhd_downscale_image->forward(in1_gpu_padded[ti], in1_gpu_padded_downscaled, cmd, opt);

                ex.input("input0", in0_gpu_padded_downscaled);
                ex.input("input1", in1_gpu_padded_downscaled);

                ncnn::VkMat flow_downscaled;
                ex.extract("flow", flow_downscaled, cmd);

                ncnn::VkMat flow_half;
                rife_uhd_upscale_flow->forward(flow_downscaled, flow_half, cmd, opt);

                rife_uhd_double_flow->forward(flow_half, flow[ti], cmd, opt);
            }
            else
            {
                ex.input("input0", in0_gpu_padded[ti]);
                ex.input("input1", in1_gpu_padded[ti]);
                ex.extract("flow", flow[ti], cmd);
            }
        }

        ncnn::VkMat flow_reversed[8];
        if (tta_temporal_mode)
        {
            for (int ti = 0; ti < 8; ti++)
            {
                // flownet
                ncnn::Extractor ex = flownet.create_extractor();
                ex.set_blob_vkallocator(blob_vkallocator);
                ex.set_workspace_vkallocator(blob_vkallocator);
                ex.set_staging_vkallocator(staging_vkallocator);

                if (uhd_mode)
                {
                    ncnn::VkMat in0_gpu_padded_downscaled;
                    ncnn::VkMat in1_gpu_padded_downscaled;
                    rife_uhd_downscale_image->forward(in0_gpu_padded[ti], in0_gpu_padded_downscaled, cmd, opt);
                    rife_uhd_downscale_image->forward(in1_gpu_padded[ti], in1_gpu_padded_downscaled, cmd, opt);

                    ex.input("input0", in1_gpu_padded_downscaled);
                    ex.input("input1", in0_gpu_padded_downscaled);

                    ncnn::VkMat flow_downscaled;
                    ex.extract("flow", flow_downscaled, cmd);

                    ncnn::VkMat flow_half;
                    rife_uhd_upscale_flow->forward(flow_downscaled, flow_half, cmd, opt);

                    rife_uhd_double_flow->forward(flow_half, flow_reversed[ti], cmd, opt);
                }
                else
                {
                    ex.input("input0", in1_gpu_padded[ti]);
                    ex.input("input1", in0_gpu_padded[ti]);
                    ex.extract("flow", flow_reversed[ti], cmd);
                }
            }
        }

        // avg flow
        ncnn::VkMat flow0[8];
        ncnn::VkMat flow1[8];
        {
            std::vector<ncnn::VkMat> bindings(8);
            bindings[0] = flow[0];
            bindings[1] = flow[1];
            bindings[2] = flow[2];
            bindings[3] = flow[3];
            bindings[4] = flow[4];
            bindings[5] = flow[5];
            bindings[6] = flow[6];
            bindings[7] = flow[7];

            std::vector<ncnn::vk_constant_type> constants(3);
            constants[0].i = flow[0].w;
            constants[1].i = flow[0].h;
            constants[2].i = flow[0].cstep;

            ncnn::VkMat dispatcher;
            dispatcher.w = flow[0].w;
            dispatcher.h = flow[0].h;
            dispatcher.c = 1;
            cmd.record_pipeline(rife_flow_tta_avg, bindings, constants, dispatcher);
        }

        if (tta_temporal_mode)
        {
            std::vector<ncnn::VkMat> bindings(8);
            bindings[0] = flow_reversed[0];
            bindings[1] = flow_reversed[1];
            bindings[2] = flow_reversed[2];
            bindings[3] = flow_reversed[3];
            bindings[4] = flow_reversed[4];
            bindings[5] = flow_reversed[5];
            bindings[6] = flow_reversed[6];
            bindings[7] = flow_reversed[7];

            std::vector<ncnn::vk_constant_type> constants(3);
            constants[0].i = flow_reversed[0].w;
            constants[1].i = flow_reversed[0].h;
            constants[2].i = flow_reversed[0].cstep;

            ncnn::VkMat dispatcher;
            dispatcher.w = flow_reversed[0].w;
            dispatcher.h = flow_reversed[0].h;
            dispatcher.c = 1;
            cmd.record_pipeline(rife_flow_tta_avg, bindings, constants, dispatcher);

            // merge flow and flow_reversed
            for (int ti = 0; ti < 8; ti++)
            {
                std::vector<ncnn::VkMat> bindings(2);
                bindings[0] = flow[ti];
                bindings[1] = flow_reversed[ti];

                std::vector<ncnn::vk_constant_type> constants(3);
                constants[0].i = flow[ti].w;
                constants[1].i = flow[ti].h;
                constants[2].i = flow[ti].cstep;

                ncnn::VkMat dispatcher;
                dispatcher.w = flow[ti].w;
                dispatcher.h = flow[ti].h;
                dispatcher.c = 1;

                cmd.record_pipeline(rife_flow_tta_temporal_avg, bindings, constants, dispatcher);
            }
        }

        if (rife_v2)
        {
            for (int ti = 0; ti < 8; ti++)
            {
                std::vector<ncnn::VkMat> inputs(1);
                inputs[0] = flow[ti];
                std::vector<ncnn::VkMat> outputs(2);
                rife_v2_slice_flow->forward(inputs, outputs, cmd, opt);
                flow0[ti] = outputs[0];
                flow1[ti] = outputs[1];
            }
        }

        ncnn::VkMat out_gpu_padded[8];
        for (int ti = 0; ti < 8; ti++)
        {
            // contextnet
            ncnn::VkMat ctx0[4];
            ncnn::VkMat ctx1[4];
            {
                ncnn::Extractor ex = contextnet.create_extractor();
                ex.set_blob_vkallocator(blob_vkallocator);
                ex.set_workspace_vkallocator(blob_vkallocator);
                ex.set_staging_vkallocator(staging_vkallocator);

                ex.input("input.1", in0_gpu_padded[ti]);
                if (rife_v2)
                {
                    ex.input("flow.0", flow0[ti]);
                }
                else
                {
                    ex.input("flow.0", flow[ti]);
                }
                ex.extract("f1", ctx0[0], cmd);
                ex.extract("f2", ctx0[1], cmd);
                ex.extract("f3", ctx0[2], cmd);
                ex.extract("f4", ctx0[3], cmd);
            }
            {
                ncnn::Extractor ex = contextnet.create_extractor();
                ex.set_blob_vkallocator(blob_vkallocator);
                ex.set_workspace_vkallocator(blob_vkallocator);
                ex.set_staging_vkallocator(staging_vkallocator);

                ex.input("input.1", in1_gpu_padded[ti]);
                if (rife_v2)
                {
                    ex.input("flow.0", flow1[ti]);
                }
                else
                {
                    ex.input("flow.1", flow[ti]);
                }
                ex.extract("f1", ctx1[0], cmd);
                ex.extract("f2", ctx1[1], cmd);
                ex.extract("f3", ctx1[2], cmd);
                ex.extract("f4", ctx1[3], cmd);
            }

            // fusionnet
            {
                ncnn::Extractor ex = fusionnet.create_extractor();
                ex.set_blob_vkallocator(blob_vkallocator);
                ex.set_workspace_vkallocator(blob_vkallocator);
                ex.set_staging_vkallocator(staging_vkallocator);

                ex.input("img0", in0_gpu_padded[ti]);
                ex.input("img1", in1_gpu_padded[ti]);
                ex.input("flow", flow[ti]);
                ex.input("3", ctx0[0]);
                ex.input("4", ctx0[1]);
                ex.input("5", ctx0[2]);
                ex.input("6", ctx0[3]);
                ex.input("7", ctx1[0]);
                ex.input("8", ctx1[1]);
                ex.input("9", ctx1[2]);
                ex.input("10", ctx1[3]);

                // save some memory
                if (!tta_temporal_mode)
                {
                    if (ti == 0)
                    {
                        in0_gpu.release();
                        in1_gpu.release();
                    }
                    else
                    {
                        in0_gpu_padded[ti - 1].release();
                        in1_gpu_padded[ti - 1].release();
                    }
                    ctx0[0].release();
                    ctx0[1].release();
                    ctx0[2].release();
                    ctx0[3].release();
                    ctx1[0].release();
                    ctx1[1].release();
                    ctx1[2].release();
                    ctx1[3].release();
                }
                if (ti != 0)
                {
                    flow[ti - 1].release();
                }

                ex.extract("output", out_gpu_padded[ti], cmd);
            }

            if (tta_temporal_mode)
            {
                // fusionnet
                ncnn::VkMat out_gpu_padded_reversed;
                {
                    ncnn::Extractor ex = fusionnet.create_extractor();
                    ex.set_blob_vkallocator(blob_vkallocator);
                    ex.set_workspace_vkallocator(blob_vkallocator);
                    ex.set_staging_vkallocator(staging_vkallocator);

                    ex.input("img0", in1_gpu_padded[ti]);
                    ex.input("img1", in0_gpu_padded[ti]);
                    ex.input("flow", flow_reversed[ti]);
                    ex.input("3", ctx1[0]);
                    ex.input("4", ctx1[1]);
                    ex.input("5", ctx1[2]);
                    ex.input("6", ctx1[3]);
                    ex.input("7", ctx0[0]);
                    ex.input("8", ctx0[1]);
                    ex.input("9", ctx0[2]);
                    ex.input("10", ctx0[3]);

                    // save some memory
                    if (ti == 0)
                    {
                        in0_gpu.release();
                        in1_gpu.release();
                    }
                    else
                    {
                        in0_gpu_padded[ti - 1].release();
                        in1_gpu_padded[ti - 1].release();
                        flow_reversed[ti - 1].release();
                    }
                    ctx0[0].release();
                    ctx0[1].release();
                    ctx0[2].release();
                    ctx0[3].release();
                    ctx1[0].release();
                    ctx1[1].release();
                    ctx1[2].release();
                    ctx1[3].release();

                    ex.extract("output", out_gpu_padded_reversed, cmd);
                }

                // merge output
                {
                    std::vector<ncnn::VkMat> bindings(2);
                    bindings[0] = out_gpu_padded[ti];
                    bindings[1] = out_gpu_padded_reversed;

                    std::vector<ncnn::vk_constant_type> constants(3);
                    constants[0].i = out_gpu_padded[ti].w;
                    constants[1].i = out_gpu_padded[ti].h;
                    constants[2].i = out_gpu_padded[ti].cstep;

                    ncnn::VkMat dispatcher;
                    dispatcher.w = out_gpu_padded[ti].w;
                    dispatcher.h = out_gpu_padded[ti].h;
                    dispatcher.c = 3;
                    cmd.record_pipeline(rife_out_tta_temporal_avg, bindings, constants, dispatcher);
                }
            }
        }

        if (opt.use_fp16_storage && opt.use_int8_storage)
        {
            out_gpu.create(w, h, (size_t)channels, 1, blob_vkallocator);
        }
        else
        {
            out_gpu.create(w, h, channels, (size_t)4u, 1, blob_vkallocator);
        }

        // postproc
        {
            std::vector<ncnn::VkMat> bindings(9);
            bindings[0] = out_gpu_padded[0];
            bindings[1] = out_gpu_padded[1];
            bindings[2] = out_gpu_padded[2];
            bindings[3] = out_gpu_padded[3];
            bindings[4] = out_gpu_padded[4];
            bindings[5] = out_gpu_padded[5];
            bindings[6] = out_gpu_padded[6];
            bindings[7] = out_gpu_padded[7];
            bindings[8] = out_gpu;

            std::vector<ncnn::vk_constant_type> constants(6);
            constants[0].i = out_gpu_padded[0].w;
            constants[1].i = out_gpu_padded[0].h;
            constants[2].i = out_gpu_padded[0].cstep;
            constants[3].i = out_gpu.w;
            constants[4].i = out_gpu.h;
            constants[5].i = out_gpu.cstep;

            cmd.record_pipeline(rife_postproc, bindings, constants, out_gpu);
        }
    }
    else
    {
        // preproc
        ncnn::VkMat in0_gpu_padded;
        ncnn::VkMat in1_gpu_padded;
        gpu_ctx.func_img_pad(in0_gpu_padded, in0_gpu, rife_preproc);
        gpu_ctx.func_img_pad(in1_gpu_padded, in1_gpu, rife_preproc);
        // flownet
        ncnn::VkMat flow;
        ncnn::VkMat flow0;
        ncnn::VkMat flow1;
        {
            ncnn::Extractor ex = flownet.create_extractor();
            ex.set_blob_vkallocator(blob_vkallocator);
            ex.set_workspace_vkallocator(blob_vkallocator);
            ex.set_staging_vkallocator(staging_vkallocator);

            if (uhd_mode)
            {
                ncnn::VkMat in0_gpu_padded_downscaled;
                ncnn::VkMat in1_gpu_padded_downscaled;
                rife_uhd_downscale_image->forward(in0_gpu_padded, in0_gpu_padded_downscaled, cmd, opt);
                rife_uhd_downscale_image->forward(in1_gpu_padded, in1_gpu_padded_downscaled, cmd, opt);

                ex.input("input0", in0_gpu_padded_downscaled);
                ex.input("input1", in1_gpu_padded_downscaled);

                ncnn::VkMat flow_downscaled;
                ex.extract("flow", flow_downscaled, cmd);

                ncnn::VkMat flow_half;
                rife_uhd_upscale_flow->forward(flow_downscaled, flow_half, cmd, opt);

                rife_uhd_double_flow->forward(flow_half, flow, cmd, opt);
            }
            else
            {
                ex.input("input0", in0_gpu_padded);
                ex.input("input1", in1_gpu_padded);
                ex.extract("flow", flow, cmd);
            }
        }

        ncnn::VkMat flow_reversed;
        if (tta_temporal_mode)
        {
            // flownet
            ncnn::Extractor ex = flownet.create_extractor();
            ex.set_blob_vkallocator(blob_vkallocator);
            ex.set_workspace_vkallocator(blob_vkallocator);
            ex.set_staging_vkallocator(staging_vkallocator);

            if (uhd_mode)
            {
                ncnn::VkMat in0_gpu_padded_downscaled;
                ncnn::VkMat in1_gpu_padded_downscaled;
                rife_uhd_downscale_image->forward(in0_gpu_padded, in0_gpu_padded_downscaled, cmd, opt);
                rife_uhd_downscale_image->forward(in1_gpu_padded, in1_gpu_padded_downscaled, cmd, opt);

                ex.input("input0", in1_gpu_padded_downscaled);
                ex.input("input1", in0_gpu_padded_downscaled);

                ncnn::VkMat flow_downscaled;
                ex.extract("flow", flow_downscaled, cmd);

                ncnn::VkMat flow_half;
                rife_uhd_upscale_flow->forward(flow_downscaled, flow_half, cmd, opt);

                rife_uhd_double_flow->forward(flow_half, flow_reversed, cmd, opt);
            }
            else
            {
                ex.input("input0", in1_gpu_padded);
                ex.input("input1", in0_gpu_padded);
                ex.extract("flow", flow_reversed, cmd);
            }

            // merge flow and flow_reversed
            {
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
        }

        if (rife_v2)
        {
            std::vector<ncnn::VkMat> inputs(1);
            inputs[0] = flow;
            std::vector<ncnn::VkMat> outputs(2);
            rife_v2_slice_flow->forward(inputs, outputs, cmd, opt);
            flow0 = outputs[0];
            flow1 = outputs[1];
        }

        // contextnet
        ncnn::VkMat ctx0[4];
        ncnn::VkMat ctx1[4];
        {   
            ncnn::Extractor ex = contextnet.create_extractor();
            ex.set_blob_vkallocator(blob_vkallocator);
            ex.set_workspace_vkallocator(blob_vkallocator);
            ex.set_staging_vkallocator(staging_vkallocator);

            ex.input("input.1", in0_gpu_padded);
            if (rife_v2)
            {
                ex.input("flow.0", flow0);
            }
            else
            {
                ex.input("flow.0", flow);
            }
            ex.extract("f1", ctx0[0], cmd);
            ex.extract("f2", ctx0[1], cmd);
            ex.extract("f3", ctx0[2], cmd);
            ex.extract("f4", ctx0[3], cmd);
        }
        {
            ncnn::Extractor ex = contextnet.create_extractor();
            ex.set_blob_vkallocator(blob_vkallocator);
            ex.set_workspace_vkallocator(blob_vkallocator);
            ex.set_staging_vkallocator(staging_vkallocator);

            ex.input("input.1", in1_gpu_padded);
            if (rife_v2)
            {
                ex.input("flow.0", flow1);
            }
            else
            {
                ex.input("flow.1", flow);
            }
            ex.extract("f1", ctx1[0], cmd);
            ex.extract("f2", ctx1[1], cmd);
            ex.extract("f3", ctx1[2], cmd);
            ex.extract("f4", ctx1[3], cmd);
        }

        // fusionnet
        ncnn::VkMat out_gpu_padded;
        {
            ncnn::Extractor ex = fusionnet.create_extractor();
            ex.set_blob_vkallocator(blob_vkallocator);
            ex.set_workspace_vkallocator(blob_vkallocator);
            ex.set_staging_vkallocator(staging_vkallocator);

            ex.input("img0", in0_gpu_padded);
            ex.input("img1", in1_gpu_padded);
            ex.input("flow", flow);
            ex.input("3", ctx0[0]);
            ex.input("4", ctx0[1]);
            ex.input("5", ctx0[2]);
            ex.input("6", ctx0[3]);
            ex.input("7", ctx1[0]);
            ex.input("8", ctx1[1]);
            ex.input("9", ctx1[2]);
            ex.input("10", ctx1[3]);

            if (!tta_temporal_mode)
            {
                // save some memory
                in0_gpu.release();
                in1_gpu.release();
                ctx0[0].release();
                ctx0[1].release();
                ctx0[2].release();
                ctx0[3].release();
                ctx1[0].release();
                ctx1[1].release();
                ctx1[2].release();
                ctx1[3].release();
            }
            flow.release();

            ex.extract("output", out_gpu_padded, cmd);
        }

        if (tta_temporal_mode)
        {
            // fusionnet
            ncnn::VkMat out_gpu_padded_reversed;
            {
                ncnn::Extractor ex = fusionnet.create_extractor();
                ex.set_blob_vkallocator(blob_vkallocator);
                ex.set_workspace_vkallocator(blob_vkallocator);
                ex.set_staging_vkallocator(staging_vkallocator);

                ex.input("img0", in1_gpu_padded);
                ex.input("img1", in0_gpu_padded);
                ex.input("flow", flow_reversed);
                ex.input("3", ctx1[0]);
                ex.input("4", ctx1[1]);
                ex.input("5", ctx1[2]);
                ex.input("6", ctx1[3]);
                ex.input("7", ctx0[0]);
                ex.input("8", ctx0[1]);
                ex.input("9", ctx0[2]);
                ex.input("10", ctx0[3]);

                // save some memory
                in0_gpu.release();
                in1_gpu.release();
                ctx0[0].release();
                ctx0[1].release();
                ctx0[2].release();
                ctx0[3].release();
                ctx1[0].release();
                ctx1[1].release();
                ctx1[2].release();
                ctx1[3].release();
                flow_reversed.release();

                ex.extract("output", out_gpu_padded_reversed, cmd);
            }

            // merge output
            {
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
        }

        if (opt.use_fp16_storage && opt.use_int8_storage)
        {
            out_gpu.create(w, h, (size_t)channels, 1, blob_vkallocator);
        }
        else
        {
            out_gpu.create(w, h, channels, (size_t)4u, 1, blob_vkallocator);
        }

        // postproc
        {
            std::vector<ncnn::VkMat> bindings(2);
            bindings[0] = out_gpu_padded;
            bindings[1] = out_gpu;

            std::vector<ncnn::vk_constant_type> constants(6);
            constants[0].i = out_gpu_padded.w;
            constants[1].i = out_gpu_padded.h;
            constants[2].i = out_gpu_padded.cstep;
            constants[3].i = out_gpu.w;
            constants[4].i = out_gpu.h;
            constants[5].i = out_gpu.cstep;

            cmd.record_pipeline(rife_postproc, bindings, constants, out_gpu);
        }
    }

    // download
    {
        ncnn::Mat out;

        if (opt.use_fp16_storage && opt.use_int8_storage)
        {
            out = ncnn::Mat(out_gpu.w, out_gpu.h, (unsigned char*)outimage.data, (size_t)channels, 1);
        }

        cmd.record_clone(out_gpu, out, opt);

        cmd.submit_and_wait();

        if (!(opt.use_fp16_storage && opt.use_int8_storage))
        {
            out.to_pixels((unsigned char*)outimage.data, ncnn::Mat::PIXEL_RGB);
        }
    }

    vkdev->reclaim_blob_allocator(blob_vkallocator);
    vkdev->reclaim_staging_allocator(staging_vkallocator);

    return 0;
}

int RIFE::process_cpu(const ncnn::Mat& in0image, const ncnn::Mat& in1image, float timestep, ncnn::Mat& outimage) const
{
    const unsigned char* pixel0data = (const unsigned char*)in0image.data;
    const unsigned char* pixel1data = (const unsigned char*)in1image.data;
    const int w = in0image.w;
    const int h = in0image.h;
    const int channels = 3;//in0image.elempack;

//     fprintf(stderr, "%d x %d\n", w, h);

    ncnn::Option opt = flownet.opt;

    // pad to 32n
    int w_padded = (w + 31) / 32 * 32;
    int h_padded = (h + 31) / 32 * 32;

    ncnn::Mat in0;
    ncnn::Mat in1;
    {
#if _WIN32
        in0 = ncnn::Mat::from_pixels(pixel0data, ncnn::Mat::PIXEL_BGR2RGB, w, h);
        in1 = ncnn::Mat::from_pixels(pixel1data, ncnn::Mat::PIXEL_BGR2RGB, w, h);
#else
        in0 = ncnn::Mat::from_pixels(pixel0data, ncnn::Mat::PIXEL_RGB, w, h);
        in1 = ncnn::Mat::from_pixels(pixel1data, ncnn::Mat::PIXEL_RGB, w, h);
#endif
    }

    ncnn::Mat out;

    if (tta_mode)
    {
        // preproc and border padding
        ncnn::Mat in0_padded[8];
        ncnn::Mat in1_padded[8];
        {
            in0_padded[0].create(w_padded, h_padded, 3);
            for (int q = 0; q < 3; q++)
            {
                float* outptr = in0_padded[0].channel(q);

                int i = 0;
                for (; i < h; i++)
                {
                    const float* ptr = in0.channel(q).row(i);

                    int j = 0;
                    for (; j < w; j++)
                    {
                        *outptr++ = *ptr++ * (1 / 255.f);
                    }
                    for (; j < w_padded; j++)
                    {
                        *outptr++ = 0.f;
                    }
                }
                for (; i < h_padded; i++)
                {
                    for (int j = 0; j < w_padded; j++)
                    {
                        *outptr++ = 0.f;
                    }
                }
            }
        }
        {
            in1_padded[0].create(w_padded, h_padded, 3);
            for (int q = 0; q < 3; q++)
            {
                float* outptr = in1_padded[0].channel(q);

                int i = 0;
                for (; i < h; i++)
                {
                    const float* ptr = in1.channel(q).row(i);

                    int j = 0;
                    for (; j < w; j++)
                    {
                        *outptr++ = *ptr++ * (1 / 255.f);
                    }
                    for (; j < w_padded; j++)
                    {
                        *outptr++ = 0.f;
                    }
                }
                for (; i < h_padded; i++)
                {
                    for (int j = 0; j < w_padded; j++)
                    {
                        *outptr++ = 0.f;
                    }
                }
            }
        }

        // the other 7 directions
        {
            in0_padded[1].create(w_padded, h_padded, 3);
            in0_padded[2].create(w_padded, h_padded, 3);
            in0_padded[3].create(w_padded, h_padded, 3);
            in0_padded[4].create(h_padded, w_padded, 3);
            in0_padded[5].create(h_padded, w_padded, 3);
            in0_padded[6].create(h_padded, w_padded, 3);
            in0_padded[7].create(h_padded, w_padded, 3);

            for (int q = 0; q < 3; q++)
            {
                const ncnn::Mat in0_padded_0 = in0_padded[0].channel(q);
                ncnn::Mat in0_padded_1 = in0_padded[1].channel(q);
                ncnn::Mat in0_padded_2 = in0_padded[2].channel(q);
                ncnn::Mat in0_padded_3 = in0_padded[3].channel(q);
                ncnn::Mat in0_padded_4 = in0_padded[4].channel(q);
                ncnn::Mat in0_padded_5 = in0_padded[5].channel(q);
                ncnn::Mat in0_padded_6 = in0_padded[6].channel(q);
                ncnn::Mat in0_padded_7 = in0_padded[7].channel(q);

                for (int i = 0; i < h_padded; i++)
                {
                    const float* outptr0 = in0_padded_0.row(i);
                    float* outptr1 = in0_padded_1.row(i) + w_padded - 1;
                    float* outptr2 = in0_padded_2.row(h_padded - 1 - i) + w_padded - 1;
                    float* outptr3 = in0_padded_3.row(h_padded - 1 - i);

                    for (int j = 0; j < w_padded; j++)
                    {
                        float* outptr4 = in0_padded_4.row(j) + i;
                        float* outptr5 = in0_padded_5.row(j) + h_padded - 1 - i;
                        float* outptr6 = in0_padded_6.row(w_padded - 1 - j) + h_padded - 1 - i;
                        float* outptr7 = in0_padded_7.row(w_padded - 1 - j) + i;

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
        {
            in1_padded[1].create(w_padded, h_padded, 3);
            in1_padded[2].create(w_padded, h_padded, 3);
            in1_padded[3].create(w_padded, h_padded, 3);
            in1_padded[4].create(h_padded, w_padded, 3);
            in1_padded[5].create(h_padded, w_padded, 3);
            in1_padded[6].create(h_padded, w_padded, 3);
            in1_padded[7].create(h_padded, w_padded, 3);

            for (int q = 0; q < 3; q++)
            {
                const ncnn::Mat in1_padded_0 = in1_padded[0].channel(q);
                ncnn::Mat in1_padded_1 = in1_padded[1].channel(q);
                ncnn::Mat in1_padded_2 = in1_padded[2].channel(q);
                ncnn::Mat in1_padded_3 = in1_padded[3].channel(q);
                ncnn::Mat in1_padded_4 = in1_padded[4].channel(q);
                ncnn::Mat in1_padded_5 = in1_padded[5].channel(q);
                ncnn::Mat in1_padded_6 = in1_padded[6].channel(q);
                ncnn::Mat in1_padded_7 = in1_padded[7].channel(q);

                for (int i = 0; i < h_padded; i++)
                {
                    const float* outptr0 = in1_padded_0.row(i);
                    float* outptr1 = in1_padded_1.row(i) + w_padded - 1;
                    float* outptr2 = in1_padded_2.row(h_padded - 1 - i) + w_padded - 1;
                    float* outptr3 = in1_padded_3.row(h_padded - 1 - i);

                    for (int j = 0; j < w_padded; j++)
                    {
                        float* outptr4 = in1_padded_4.row(j) + i;
                        float* outptr5 = in1_padded_5.row(j) + h_padded - 1 - i;
                        float* outptr6 = in1_padded_6.row(w_padded - 1 - j) + h_padded - 1 - i;
                        float* outptr7 = in1_padded_7.row(w_padded - 1 - j) + i;

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

        ncnn::Mat flow[8];
        for (int ti = 0; ti < 8; ti++)
        {
            // flownet
            {
                ncnn::Extractor ex = flownet.create_extractor();

                if (uhd_mode)
                {
                    ncnn::Mat in0_padded_downscaled;
                    ncnn::Mat in1_padded_downscaled;
                    rife_uhd_downscale_image->forward(in0_padded[ti], in0_padded_downscaled, opt);
                    rife_uhd_downscale_image->forward(in1_padded[ti], in1_padded_downscaled, opt);

                    ex.input("input0", in0_padded_downscaled);
                    ex.input("input1", in1_padded_downscaled);

                    ncnn::Mat flow_downscaled;
                    ex.extract("flow", flow_downscaled);

                    ncnn::Mat flow_half;
                    rife_uhd_upscale_flow->forward(flow_downscaled, flow_half, opt);

                    rife_uhd_double_flow->forward(flow_half, flow[ti], opt);
                }
                else
                {
                    ex.input("input0", in0_padded[ti]);
                    ex.input("input1", in1_padded[ti]);
                    ex.extract("flow", flow[ti]);
                }
            }
        }

        ncnn::Mat flow_reversed[8];
        if (tta_temporal_mode)
        {
            for (int ti = 0; ti < 8; ti++)
            {
                // flownet
                {
                    ncnn::Extractor ex = flownet.create_extractor();

                    if (uhd_mode)
                    {
                        ncnn::Mat in0_padded_downscaled;
                        ncnn::Mat in1_padded_downscaled;
                        rife_uhd_downscale_image->forward(in0_padded[ti], in0_padded_downscaled, opt);
                        rife_uhd_downscale_image->forward(in1_padded[ti], in1_padded_downscaled, opt);

                        ex.input("input0", in1_padded_downscaled);
                        ex.input("input1", in0_padded_downscaled);

                        ncnn::Mat flow_downscaled;
                        ex.extract("flow", flow_downscaled);

                        ncnn::Mat flow_half;
                        rife_uhd_upscale_flow->forward(flow_downscaled, flow_half, opt);

                        rife_uhd_double_flow->forward(flow_half, flow_reversed[ti], opt);
                    }
                    else
                    {
                        ex.input("input0", in1_padded[ti]);
                        ex.input("input1", in0_padded[ti]);
                        ex.extract("flow", flow_reversed[ti]);
                    }
                }

                // merge flow and flow_reversed
                {
                    float* flow_x = flow[ti].channel(0);
                    float* flow_y = flow[ti].channel(1);
                    float* flow_reversed_x = flow_reversed[ti].channel(0);
                    float* flow_reversed_y = flow_reversed[ti].channel(1);

                    if (rife_v2)
                    {
                        float* flow_z = flow[ti].channel(2);
                        float* flow_w = flow[ti].channel(3);
                        float* flow_reversed_z = flow_reversed[ti].channel(2);
                        float* flow_reversed_w = flow_reversed[ti].channel(3);

                        for (int i = 0; i < flow[ti].h; i++)
                        {
                            for (int j = 0; j < flow[ti].w; j++)
                            {
                                float x = (*flow_x + *flow_reversed_z) * 0.5f;
                                float y = (*flow_y + *flow_reversed_w) * 0.5f;
                                float z = (*flow_z + *flow_reversed_x) * 0.5f;
                                float w = (*flow_w + *flow_reversed_y) * 0.5f;

                                *flow_x++ = x;
                                *flow_y++ = y;
                                *flow_z++ = z;
                                *flow_w++ = w;
                                *flow_reversed_x++ = z;
                                *flow_reversed_y++ = w;
                                *flow_reversed_z++ = x;
                                *flow_reversed_w++ = y;
                            }
                        }
                    }
                    else
                    {
                        for (int i = 0; i < flow[ti].h; i++)
                        {
                            for (int j = 0; j < flow[ti].w; j++)
                            {
                                float x = (*flow_x - *flow_reversed_x) * 0.5f;
                                float y = (*flow_y - *flow_reversed_y) * 0.5f;

                                *flow_x++ = x;
                                *flow_y++ = y;
                                *flow_reversed_x++ = -x;
                                *flow_reversed_y++ = -y;
                            }
                        }
                    }
                }
            }
        }

        // avg flow
        ncnn::Mat flow0[8];
        ncnn::Mat flow1[8];
        {
            ncnn::Mat flow_x0 = flow[0].channel(0);
            ncnn::Mat flow_x1 = flow[1].channel(0);
            ncnn::Mat flow_x2 = flow[2].channel(0);
            ncnn::Mat flow_x3 = flow[3].channel(0);
            ncnn::Mat flow_x4 = flow[4].channel(0);
            ncnn::Mat flow_x5 = flow[5].channel(0);
            ncnn::Mat flow_x6 = flow[6].channel(0);
            ncnn::Mat flow_x7 = flow[7].channel(0);

            ncnn::Mat flow_y0 = flow[0].channel(1);
            ncnn::Mat flow_y1 = flow[1].channel(1);
            ncnn::Mat flow_y2 = flow[2].channel(1);
            ncnn::Mat flow_y3 = flow[3].channel(1);
            ncnn::Mat flow_y4 = flow[4].channel(1);
            ncnn::Mat flow_y5 = flow[5].channel(1);
            ncnn::Mat flow_y6 = flow[6].channel(1);
            ncnn::Mat flow_y7 = flow[7].channel(1);

            if (rife_v2)
            {
                ncnn::Mat flow_z0 = flow[0].channel(2);
                ncnn::Mat flow_z1 = flow[1].channel(2);
                ncnn::Mat flow_z2 = flow[2].channel(2);
                ncnn::Mat flow_z3 = flow[3].channel(2);
                ncnn::Mat flow_z4 = flow[4].channel(2);
                ncnn::Mat flow_z5 = flow[5].channel(2);
                ncnn::Mat flow_z6 = flow[6].channel(2);
                ncnn::Mat flow_z7 = flow[7].channel(2);

                ncnn::Mat flow_w0 = flow[0].channel(3);
                ncnn::Mat flow_w1 = flow[1].channel(3);
                ncnn::Mat flow_w2 = flow[2].channel(3);
                ncnn::Mat flow_w3 = flow[3].channel(3);
                ncnn::Mat flow_w4 = flow[4].channel(3);
                ncnn::Mat flow_w5 = flow[5].channel(3);
                ncnn::Mat flow_w6 = flow[6].channel(3);
                ncnn::Mat flow_w7 = flow[7].channel(3);

                for (int i = 0; i < flow_x0.h; i++)
                {
                    float* x0 = flow_x0.row(i);
                    float* x1 = flow_x1.row(i) + flow_x0.w - 1;
                    float* x2 = flow_x2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* x3 = flow_x3.row(flow_x0.h - 1 - i);

                    float* y0 = flow_y0.row(i);
                    float* y1 = flow_y1.row(i) + flow_x0.w - 1;
                    float* y2 = flow_y2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* y3 = flow_y3.row(flow_x0.h - 1 - i);

                    float* z0 = flow_z0.row(i);
                    float* z1 = flow_z1.row(i) + flow_x0.w - 1;
                    float* z2 = flow_z2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* z3 = flow_z3.row(flow_x0.h - 1 - i);

                    float* w0 = flow_w0.row(i);
                    float* w1 = flow_w1.row(i) + flow_x0.w - 1;
                    float* w2 = flow_w2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* w3 = flow_w3.row(flow_x0.h - 1 - i);

                    for (int j = 0; j < flow_x0.w; j++)
                    {
                        float* x4 = flow_x4.row(j) + i;
                        float* x5 = flow_x5.row(j) + flow_x0.h - 1 - i;
                        float* x6 = flow_x6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* x7 = flow_x7.row(flow_x0.w - 1 - j) + i;

                        float* y4 = flow_y4.row(j) + i;
                        float* y5 = flow_y5.row(j) + flow_x0.h - 1 - i;
                        float* y6 = flow_y6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* y7 = flow_y7.row(flow_x0.w - 1 - j) + i;

                        float* z4 = flow_z4.row(j) + i;
                        float* z5 = flow_z5.row(j) + flow_x0.h - 1 - i;
                        float* z6 = flow_z6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* z7 = flow_z7.row(flow_x0.w - 1 - j) + i;

                        float* w4 = flow_w4.row(j) + i;
                        float* w5 = flow_w5.row(j) + flow_x0.h - 1 - i;
                        float* w6 = flow_w6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* w7 = flow_w7.row(flow_x0.w - 1 - j) + i;

                        float x = (*x0 + -*x1 + -*x2 + *x3 + *y4 + *y5 + -*y6 + -*y7) * 0.125f;
                        float y = (*y0 + *y1 + -*y2 + -*y3 + *x4 + -*x5 + -*x6 + *x7) * 0.125f;
                        float z = (*z0 + -*z1 + -*z2 + *z3 + *w4 + *w5 + -*w6 + -*w7) * 0.125f;
                        float w = (*w0 + *w1 + -*w2 + -*w3 + *z4 + -*z5 + -*z6 + *z7) * 0.125f;

                        *x0++ = x;
                        *x1-- = -x;
                        *x2-- = -x;
                        *x3++ = x;
                        *x4 = y;
                        *x5 = -y;
                        *x6 = -y;
                        *x7 = y;

                        *y0++ = y;
                        *y1-- = y;
                        *y2-- = -y;
                        *y3++ = -y;
                        *y4 = x;
                        *y5 = x;
                        *y6 = -x;
                        *y7 = -x;

                        *z0++ = z;
                        *z1-- = -z;
                        *z2-- = -z;
                        *z3++ = z;
                        *z4 = w;
                        *z5 = -w;
                        *z6 = -w;
                        *z7 = w;

                        *w0++ = w;
                        *w1-- = w;
                        *w2-- = -w;
                        *w3++ = -w;
                        *w4 = z;
                        *w5 = z;
                        *w6 = -z;
                        *w7 = -z;
                    }
                }
            }
            else
            {
                for (int i = 0; i < flow_x0.h; i++)
                {
                    float* x0 = flow_x0.row(i);
                    float* x1 = flow_x1.row(i) + flow_x0.w - 1;
                    float* x2 = flow_x2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* x3 = flow_x3.row(flow_x0.h - 1 - i);

                    float* y0 = flow_y0.row(i);
                    float* y1 = flow_y1.row(i) + flow_x0.w - 1;
                    float* y2 = flow_y2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* y3 = flow_y3.row(flow_x0.h - 1 - i);

                    for (int j = 0; j < flow_x0.w; j++)
                    {
                        float* x4 = flow_x4.row(j) + i;
                        float* x5 = flow_x5.row(j) + flow_x0.h - 1 - i;
                        float* x6 = flow_x6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* x7 = flow_x7.row(flow_x0.w - 1 - j) + i;

                        float* y4 = flow_y4.row(j) + i;
                        float* y5 = flow_y5.row(j) + flow_x0.h - 1 - i;
                        float* y6 = flow_y6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* y7 = flow_y7.row(flow_x0.w - 1 - j) + i;

                        float x = (*x0 + -*x1 + -*x2 + *x3 + *y4 + *y5 + -*y6 + -*y7) * 0.125f;
                        float y = (*y0 + *y1 + -*y2 + -*y3 + *x4 + -*x5 + -*x6 + *x7) * 0.125f;

                        *x0++ = x;
                        *x1-- = -x;
                        *x2-- = -x;
                        *x3++ = x;
                        *x4 = y;
                        *x5 = -y;
                        *x6 = -y;
                        *x7 = y;

                        *y0++ = y;
                        *y1-- = y;
                        *y2-- = -y;
                        *y3++ = -y;
                        *y4 = x;
                        *y5 = x;
                        *y6 = -x;
                        *y7 = -x;
                    }
                }
            }
        }

        if (tta_temporal_mode)
        {
            ncnn::Mat flow_x0 = flow_reversed[0].channel(0);
            ncnn::Mat flow_x1 = flow_reversed[1].channel(0);
            ncnn::Mat flow_x2 = flow_reversed[2].channel(0);
            ncnn::Mat flow_x3 = flow_reversed[3].channel(0);
            ncnn::Mat flow_x4 = flow_reversed[4].channel(0);
            ncnn::Mat flow_x5 = flow_reversed[5].channel(0);
            ncnn::Mat flow_x6 = flow_reversed[6].channel(0);
            ncnn::Mat flow_x7 = flow_reversed[7].channel(0);

            ncnn::Mat flow_y0 = flow_reversed[0].channel(1);
            ncnn::Mat flow_y1 = flow_reversed[1].channel(1);
            ncnn::Mat flow_y2 = flow_reversed[2].channel(1);
            ncnn::Mat flow_y3 = flow_reversed[3].channel(1);
            ncnn::Mat flow_y4 = flow_reversed[4].channel(1);
            ncnn::Mat flow_y5 = flow_reversed[5].channel(1);
            ncnn::Mat flow_y6 = flow_reversed[6].channel(1);
            ncnn::Mat flow_y7 = flow_reversed[7].channel(1);

            if (rife_v2)
            {
                ncnn::Mat flow_z0 = flow_reversed[0].channel(2);
                ncnn::Mat flow_z1 = flow_reversed[1].channel(2);
                ncnn::Mat flow_z2 = flow_reversed[2].channel(2);
                ncnn::Mat flow_z3 = flow_reversed[3].channel(2);
                ncnn::Mat flow_z4 = flow_reversed[4].channel(2);
                ncnn::Mat flow_z5 = flow_reversed[5].channel(2);
                ncnn::Mat flow_z6 = flow_reversed[6].channel(2);
                ncnn::Mat flow_z7 = flow_reversed[7].channel(2);

                ncnn::Mat flow_w0 = flow_reversed[0].channel(3);
                ncnn::Mat flow_w1 = flow_reversed[1].channel(3);
                ncnn::Mat flow_w2 = flow_reversed[2].channel(3);
                ncnn::Mat flow_w3 = flow_reversed[3].channel(3);
                ncnn::Mat flow_w4 = flow_reversed[4].channel(3);
                ncnn::Mat flow_w5 = flow_reversed[5].channel(3);
                ncnn::Mat flow_w6 = flow_reversed[6].channel(3);
                ncnn::Mat flow_w7 = flow_reversed[7].channel(3);

                for (int i = 0; i < flow_x0.h; i++)
                {
                    float* x0 = flow_x0.row(i);
                    float* x1 = flow_x1.row(i) + flow_x0.w - 1;
                    float* x2 = flow_x2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* x3 = flow_x3.row(flow_x0.h - 1 - i);

                    float* y0 = flow_y0.row(i);
                    float* y1 = flow_y1.row(i) + flow_x0.w - 1;
                    float* y2 = flow_y2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* y3 = flow_y3.row(flow_x0.h - 1 - i);

                    float* z0 = flow_z0.row(i);
                    float* z1 = flow_z1.row(i) + flow_x0.w - 1;
                    float* z2 = flow_z2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* z3 = flow_z3.row(flow_x0.h - 1 - i);

                    float* w0 = flow_w0.row(i);
                    float* w1 = flow_w1.row(i) + flow_x0.w - 1;
                    float* w2 = flow_w2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* w3 = flow_w3.row(flow_x0.h - 1 - i);

                    for (int j = 0; j < flow_x0.w; j++)
                    {
                        float* x4 = flow_x4.row(j) + i;
                        float* x5 = flow_x5.row(j) + flow_x0.h - 1 - i;
                        float* x6 = flow_x6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* x7 = flow_x7.row(flow_x0.w - 1 - j) + i;

                        float* y4 = flow_y4.row(j) + i;
                        float* y5 = flow_y5.row(j) + flow_x0.h - 1 - i;
                        float* y6 = flow_y6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* y7 = flow_y7.row(flow_x0.w - 1 - j) + i;

                        float* z4 = flow_z4.row(j) + i;
                        float* z5 = flow_z5.row(j) + flow_x0.h - 1 - i;
                        float* z6 = flow_z6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* z7 = flow_z7.row(flow_x0.w - 1 - j) + i;

                        float* w4 = flow_w4.row(j) + i;
                        float* w5 = flow_w5.row(j) + flow_x0.h - 1 - i;
                        float* w6 = flow_w6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* w7 = flow_w7.row(flow_x0.w - 1 - j) + i;

                        float x = (*x0 + -*x1 + -*x2 + *x3 + *y4 + *y5 + -*y6 + -*y7) * 0.125f;
                        float y = (*y0 + *y1 + -*y2 + -*y3 + *x4 + -*x5 + -*x6 + *x7) * 0.125f;
                        float z = (*z0 + -*z1 + -*z2 + *z3 + *w4 + *w5 + -*w6 + -*w7) * 0.125f;
                        float w = (*w0 + *w1 + -*w2 + -*w3 + *z4 + -*z5 + -*z6 + *z7) * 0.125f;

                        *x0++ = x;
                        *x1-- = -x;
                        *x2-- = -x;
                        *x3++ = x;
                        *x4 = y;
                        *x5 = -y;
                        *x6 = -y;
                        *x7 = y;

                        *y0++ = y;
                        *y1-- = y;
                        *y2-- = -y;
                        *y3++ = -y;
                        *y4 = x;
                        *y5 = x;
                        *y6 = -x;
                        *y7 = -x;

                        *z0++ = z;
                        *z1-- = -z;
                        *z2-- = -z;
                        *z3++ = z;
                        *z4 = w;
                        *z5 = -w;
                        *z6 = -w;
                        *z7 = w;

                        *w0++ = w;
                        *w1-- = w;
                        *w2-- = -w;
                        *w3++ = -w;
                        *w4 = z;
                        *w5 = z;
                        *w6 = -z;
                        *w7 = -z;
                    }
                }
            }
            else
            {
                for (int i = 0; i < flow_x0.h; i++)
                {
                    float* x0 = flow_x0.row(i);
                    float* x1 = flow_x1.row(i) + flow_x0.w - 1;
                    float* x2 = flow_x2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* x3 = flow_x3.row(flow_x0.h - 1 - i);

                    float* y0 = flow_y0.row(i);
                    float* y1 = flow_y1.row(i) + flow_x0.w - 1;
                    float* y2 = flow_y2.row(flow_x0.h - 1 - i) + flow_x0.w - 1;
                    float* y3 = flow_y3.row(flow_x0.h - 1 - i);

                    for (int j = 0; j < flow_x0.w; j++)
                    {
                        float* x4 = flow_x4.row(j) + i;
                        float* x5 = flow_x5.row(j) + flow_x0.h - 1 - i;
                        float* x6 = flow_x6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* x7 = flow_x7.row(flow_x0.w - 1 - j) + i;

                        float* y4 = flow_y4.row(j) + i;
                        float* y5 = flow_y5.row(j) + flow_x0.h - 1 - i;
                        float* y6 = flow_y6.row(flow_x0.w - 1 - j) + flow_x0.h - 1 - i;
                        float* y7 = flow_y7.row(flow_x0.w - 1 - j) + i;

                        float x = (*x0 + -*x1 + -*x2 + *x3 + *y4 + *y5 + -*y6 + -*y7) * 0.125f;
                        float y = (*y0 + *y1 + -*y2 + -*y3 + *x4 + -*x5 + -*x6 + *x7) * 0.125f;

                        *x0++ = x;
                        *x1-- = -x;
                        *x2-- = -x;
                        *x3++ = x;
                        *x4 = y;
                        *x5 = -y;
                        *x6 = -y;
                        *x7 = y;

                        *y0++ = y;
                        *y1-- = y;
                        *y2-- = -y;
                        *y3++ = -y;
                        *y4 = x;
                        *y5 = x;
                        *y6 = -x;
                        *y7 = -x;
                    }
                }
            }

            // merge flow and flow_reversed
            for (int ti = 0; ti < 8; ti++)
            {
                float* flow_x = flow[ti].channel(0);
                float* flow_y = flow[ti].channel(1);
                float* flow_reversed_x = flow_reversed[ti].channel(0);
                float* flow_reversed_y = flow_reversed[ti].channel(1);

                if (rife_v2)
                {
                    float* flow_z = flow[ti].channel(2);
                    float* flow_w = flow[ti].channel(3);
                    float* flow_reversed_z = flow_reversed[ti].channel(2);
                    float* flow_reversed_w = flow_reversed[ti].channel(3);

                    for (int i = 0; i < flow[ti].h; i++)
                    {
                        for (int j = 0; j < flow[ti].w; j++)
                        {
                            float x = (*flow_x + *flow_reversed_z) * 0.5f;
                            float y = (*flow_y + *flow_reversed_w) * 0.5f;
                            float z = (*flow_z + *flow_reversed_x) * 0.5f;
                            float w = (*flow_w + *flow_reversed_y) * 0.5f;

                            *flow_x++ = x;
                            *flow_y++ = y;
                            *flow_z++ = z;
                            *flow_w++ = w;
                            *flow_reversed_x++ = z;
                            *flow_reversed_y++ = w;
                            *flow_reversed_z++ = x;
                            *flow_reversed_w++ = y;
                        }
                    }
                }
                else
                {
                    for (int i = 0; i < flow[ti].h; i++)
                    {
                        for (int j = 0; j < flow[ti].w; j++)
                        {
                            float x = (*flow_x - *flow_reversed_x) * 0.5f;
                            float y = (*flow_y - *flow_reversed_y) * 0.5f;

                            *flow_x++ = x;
                            *flow_y++ = y;
                            *flow_reversed_x++ = -x;
                            *flow_reversed_y++ = -y;
                        }
                    }
                }
            }
        }

        if (rife_v2)
        {
            for (int ti = 0; ti < 8; ti++)
            {
                std::vector<ncnn::Mat> inputs(1);
                inputs[0] = flow[ti];
                std::vector<ncnn::Mat> outputs(2);
                rife_v2_slice_flow->forward(inputs, outputs, opt);
                flow0[ti] = outputs[0];
                flow1[ti] = outputs[1];
            }
        }

        ncnn::Mat out_padded[8];
        ncnn::Mat out_padded_reversed[8];
        for (int ti = 0; ti < 8; ti++)
        {
            // contextnet
            ncnn::Mat ctx0[4];
            ncnn::Mat ctx1[4];
            {
                ncnn::Extractor ex = contextnet.create_extractor();

                ex.input("input.1", in0_padded[ti]);
                if (rife_v2)
                {
                    ex.input("flow.0", flow0[ti]);
                }
                else
                {
                    ex.input("flow.0", flow[ti]);
                }
                ex.extract("f1", ctx0[0]);
                ex.extract("f2", ctx0[1]);
                ex.extract("f3", ctx0[2]);
                ex.extract("f4", ctx0[3]);
            }
            {
                ncnn::Extractor ex = contextnet.create_extractor();

                ex.input("input.1", in1_padded[ti]);
                if (rife_v2)
                {
                    ex.input("flow.0", flow1[ti]);
                }
                else
                {
                    ex.input("flow.1", flow[ti]);
                }
                ex.extract("f1", ctx1[0]);
                ex.extract("f2", ctx1[1]);
                ex.extract("f3", ctx1[2]);
                ex.extract("f4", ctx1[3]);
            }

            // fusionnet
            {
                ncnn::Extractor ex = fusionnet.create_extractor();

                ex.input("img0", in0_padded[ti]);
                ex.input("img1", in1_padded[ti]);
                ex.input("flow", flow[ti]);
                ex.input("3", ctx0[0]);
                ex.input("4", ctx0[1]);
                ex.input("5", ctx0[2]);
                ex.input("6", ctx0[3]);
                ex.input("7", ctx1[0]);
                ex.input("8", ctx1[1]);
                ex.input("9", ctx1[2]);
                ex.input("10", ctx1[3]);

                ex.extract("output", out_padded[ti]);
            }

            if (tta_temporal_mode)
            {
                // fusionnet
                {
                    ncnn::Extractor ex = fusionnet.create_extractor();

                    ex.input("img0", in1_padded[ti]);
                    ex.input("img1", in0_padded[ti]);
                    ex.input("flow", flow_reversed[ti]);
                    ex.input("3", ctx1[0]);
                    ex.input("4", ctx1[1]);
                    ex.input("5", ctx1[2]);
                    ex.input("6", ctx1[3]);
                    ex.input("7", ctx0[0]);
                    ex.input("8", ctx0[1]);
                    ex.input("9", ctx0[2]);
                    ex.input("10", ctx0[3]);

                    ex.extract("output", out_padded_reversed[ti]);
                }
            }
        }

        // cut padding and postproc
        out.create(w, h, 3);
        if (tta_temporal_mode)
        {
            for (int q = 0; q < 3; q++)
            {
                const ncnn::Mat out_padded_0 = out_padded[0].channel(q);
                const ncnn::Mat out_padded_1 = out_padded[1].channel(q);
                const ncnn::Mat out_padded_2 = out_padded[2].channel(q);
                const ncnn::Mat out_padded_3 = out_padded[3].channel(q);
                const ncnn::Mat out_padded_4 = out_padded[4].channel(q);
                const ncnn::Mat out_padded_5 = out_padded[5].channel(q);
                const ncnn::Mat out_padded_6 = out_padded[6].channel(q);
                const ncnn::Mat out_padded_7 = out_padded[7].channel(q);
                const ncnn::Mat out_padded_reversed_0 = out_padded_reversed[0].channel(q);
                const ncnn::Mat out_padded_reversed_1 = out_padded_reversed[1].channel(q);
                const ncnn::Mat out_padded_reversed_2 = out_padded_reversed[2].channel(q);
                const ncnn::Mat out_padded_reversed_3 = out_padded_reversed[3].channel(q);
                const ncnn::Mat out_padded_reversed_4 = out_padded_reversed[4].channel(q);
                const ncnn::Mat out_padded_reversed_5 = out_padded_reversed[5].channel(q);
                const ncnn::Mat out_padded_reversed_6 = out_padded_reversed[6].channel(q);
                const ncnn::Mat out_padded_reversed_7 = out_padded_reversed[7].channel(q);
                float* outptr = out.channel(q);

                for (int i = 0; i < h; i++)
                {
                    const float* ptr0 = out_padded_0.row(i);
                    const float* ptr1 = out_padded_1.row(i) + w_padded - 1;
                    const float* ptr2 = out_padded_2.row(h_padded - 1 - i) + w_padded - 1;
                    const float* ptr3 = out_padded_3.row(h_padded - 1 - i);
                    const float* ptrr0 = out_padded_reversed_0.row(i);
                    const float* ptrr1 = out_padded_reversed_1.row(i) + w_padded - 1;
                    const float* ptrr2 = out_padded_reversed_2.row(h_padded - 1 - i) + w_padded - 1;
                    const float* ptrr3 = out_padded_reversed_3.row(h_padded - 1 - i);

                    for (int j = 0; j < w; j++)
                    {
                        const float* ptr4 = out_padded_4.row(j) + i;
                        const float* ptr5 = out_padded_5.row(j) + h_padded - 1 - i;
                        const float* ptr6 = out_padded_6.row(w_padded - 1 - j) + h_padded - 1 - i;
                        const float* ptr7 = out_padded_7.row(w_padded - 1 - j) + i;
                        const float* ptrr4 = out_padded_reversed_4.row(j) + i;
                        const float* ptrr5 = out_padded_reversed_5.row(j) + h_padded - 1 - i;
                        const float* ptrr6 = out_padded_reversed_6.row(w_padded - 1 - j) + h_padded - 1 - i;
                        const float* ptrr7 = out_padded_reversed_7.row(w_padded - 1 - j) + i;

                        float v = (*ptr0++ + *ptr1-- + *ptr2-- + *ptr3++ + *ptr4 + *ptr5 + *ptr6 + *ptr7) / 8;
                        float vr = (*ptrr0++ + *ptrr1-- + *ptrr2-- + *ptrr3++ + *ptrr4 + *ptrr5 + *ptrr6 + *ptrr7) / 8;

                        *outptr++ = (v + vr) * 0.5f * 255.f + 0.5f;
                    }
                }
            }
        }
        else
        {
            for (int q = 0; q < 3; q++)
            {
                const ncnn::Mat out_padded_0 = out_padded[0].channel(q);
                const ncnn::Mat out_padded_1 = out_padded[1].channel(q);
                const ncnn::Mat out_padded_2 = out_padded[2].channel(q);
                const ncnn::Mat out_padded_3 = out_padded[3].channel(q);
                const ncnn::Mat out_padded_4 = out_padded[4].channel(q);
                const ncnn::Mat out_padded_5 = out_padded[5].channel(q);
                const ncnn::Mat out_padded_6 = out_padded[6].channel(q);
                const ncnn::Mat out_padded_7 = out_padded[7].channel(q);
                float* outptr = out.channel(q);

                for (int i = 0; i < h; i++)
                {
                    const float* ptr0 = out_padded_0.row(i);
                    const float* ptr1 = out_padded_1.row(i) + w_padded - 1;
                    const float* ptr2 = out_padded_2.row(h_padded - 1 - i) + w_padded - 1;
                    const float* ptr3 = out_padded_3.row(h_padded - 1 - i);

                    for (int j = 0; j < w; j++)
                    {
                        const float* ptr4 = out_padded_4.row(j) + i;
                        const float* ptr5 = out_padded_5.row(j) + h_padded - 1 - i;
                        const float* ptr6 = out_padded_6.row(w_padded - 1 - j) + h_padded - 1 - i;
                        const float* ptr7 = out_padded_7.row(w_padded - 1 - j) + i;

                        float v = (*ptr0++ + *ptr1-- + *ptr2-- + *ptr3++ + *ptr4 + *ptr5 + *ptr6 + *ptr7) / 8;

                        *outptr++ = v * 255.f + 0.5f;
                    }
                }
            }
        }
    }
    else
    {
        // preproc and border padding
        ncnn::Mat in0_padded;
        ncnn::Mat in1_padded;
        {
            in0_padded.create(w_padded, h_padded, 3);
            for (int q = 0; q < 3; q++)
            {
                float* outptr = in0_padded.channel(q);

                int i = 0;
                for (; i < h; i++)
                {
                    const float* ptr = in0.channel(q).row(i);

                    int j = 0;
                    for (; j < w; j++)
                    {
                        *outptr++ = *ptr++ * (1 / 255.f);
                    }
                    for (; j < w_padded; j++)
                    {
                        *outptr++ = 0.f;
                    }
                }
                for (; i < h_padded; i++)
                {
                    for (int j = 0; j < w_padded; j++)
                    {
                        *outptr++ = 0.f;
                    }
                }
            }
        }
        {
            in1_padded.create(w_padded, h_padded, 3);
            for (int q = 0; q < 3; q++)
            {
                float* outptr = in1_padded.channel(q);

                int i = 0;
                for (; i < h; i++)
                {
                    const float* ptr = in1.channel(q).row(i);

                    int j = 0;
                    for (; j < w; j++)
                    {
                        *outptr++ = *ptr++ * (1 / 255.f);
                    }
                    for (; j < w_padded; j++)
                    {
                        *outptr++ = 0.f;
                    }
                }
                for (; i < h_padded; i++)
                {
                    for (int j = 0; j < w_padded; j++)
                    {
                        *outptr++ = 0.f;
                    }
                }
            }
        }

        // flownet
        ncnn::Mat flow;
        ncnn::Mat flow0;
        ncnn::Mat flow1;
        {
            ncnn::Extractor ex = flownet.create_extractor();

            if (uhd_mode)
            {
                ncnn::Mat in0_padded_downscaled;
                ncnn::Mat in1_padded_downscaled;
                rife_uhd_downscale_image->forward(in0_padded, in0_padded_downscaled, opt);
                rife_uhd_downscale_image->forward(in1_padded, in1_padded_downscaled, opt);

                ex.input("input0", in0_padded_downscaled);
                ex.input("input1", in1_padded_downscaled);

                ncnn::Mat flow_downscaled;
                ex.extract("flow", flow_downscaled);

                ncnn::Mat flow_half;
                rife_uhd_upscale_flow->forward(flow_downscaled, flow_half, opt);

                rife_uhd_double_flow->forward(flow_half, flow, opt);
            }
            else
            {
                ex.input("input0", in0_padded);
                ex.input("input1", in1_padded);
                ex.extract("flow", flow);
            }
        }

        ncnn::Mat flow_reversed;
        if (tta_temporal_mode)
        {
            // flownet
            ncnn::Extractor ex = flownet.create_extractor();

            if (uhd_mode)
            {
                ncnn::Mat in0_padded_downscaled;
                ncnn::Mat in1_padded_downscaled;
                rife_uhd_downscale_image->forward(in0_padded, in0_padded_downscaled, opt);
                rife_uhd_downscale_image->forward(in1_padded, in1_padded_downscaled, opt);

                ex.input("input0", in1_padded_downscaled);
                ex.input("input1", in0_padded_downscaled);

                ncnn::Mat flow_downscaled;
                ex.extract("flow", flow_downscaled);

                ncnn::Mat flow_half;
                rife_uhd_upscale_flow->forward(flow_downscaled, flow_half, opt);

                rife_uhd_double_flow->forward(flow_half, flow_reversed, opt);
            }
            else
            {
                ex.input("input0", in1_padded);
                ex.input("input1", in0_padded);
                ex.extract("flow", flow_reversed);
            }

            // merge flow and flow_reversed
            {
                float* flow_x = flow.channel(0);
                float* flow_y = flow.channel(1);
                float* flow_reversed_x = flow_reversed.channel(0);
                float* flow_reversed_y = flow_reversed.channel(1);

                if (rife_v2)
                {
                    float* flow_z = flow.channel(2);
                    float* flow_w = flow.channel(3);
                    float* flow_reversed_z = flow_reversed.channel(2);
                    float* flow_reversed_w = flow_reversed.channel(3);

                    for (int i = 0; i < flow.h; i++)
                    {
                        for (int j = 0; j < flow.w; j++)
                        {
                            float x = (*flow_x + *flow_reversed_z) * 0.5f;
                            float y = (*flow_y + *flow_reversed_w) * 0.5f;
                            float z = (*flow_z + *flow_reversed_x) * 0.5f;
                            float w = (*flow_w + *flow_reversed_y) * 0.5f;

                            *flow_x++ = x;
                            *flow_y++ = y;
                            *flow_z++ = z;
                            *flow_w++ = w;
                            *flow_reversed_x++ = z;
                            *flow_reversed_y++ = w;
                            *flow_reversed_z++ = x;
                            *flow_reversed_w++ = y;
                        }
                    }
                }
                else
                {
                    for (int i = 0; i < flow.h; i++)
                    {
                        for (int j = 0; j < flow.w; j++)
                        {
                            float x = (*flow_x - *flow_reversed_x) * 0.5f;
                            float y = (*flow_y - *flow_reversed_y) * 0.5f;

                            *flow_x++ = x;
                            *flow_y++ = y;
                            *flow_reversed_x++ = -x;
                            *flow_reversed_y++ = -y;
                        }
                    }
                }
            }
        }

        if (rife_v2)
        {
            std::vector<ncnn::Mat> inputs(1);
            inputs[0] = flow;
            std::vector<ncnn::Mat> outputs(2);
            rife_v2_slice_flow->forward(inputs, outputs, opt);
            flow0 = outputs[0];
            flow1 = outputs[1];
        }

        // contextnet
        ncnn::Mat ctx0[4];
        ncnn::Mat ctx1[4];
        {
            ncnn::Extractor ex = contextnet.create_extractor();

            ex.input("input.1", in0_padded);
            if (rife_v2)
            {
                ex.input("flow.0", flow0);
            }
            else
            {
                ex.input("flow.0", flow);
            }
            ex.extract("f1", ctx0[0]);
            ex.extract("f2", ctx0[1]);
            ex.extract("f3", ctx0[2]);
            ex.extract("f4", ctx0[3]);
        }
        {
            ncnn::Extractor ex = contextnet.create_extractor();

            ex.input("input.1", in1_padded);
            if (rife_v2)
            {
                ex.input("flow.0", flow1);
            }
            else
            {
                ex.input("flow.1", flow);
            }
            ex.extract("f1", ctx1[0]);
            ex.extract("f2", ctx1[1]);
            ex.extract("f3", ctx1[2]);
            ex.extract("f4", ctx1[3]);
        }

        // fusionnet
        ncnn::Mat out_padded;
        {
            ncnn::Extractor ex = fusionnet.create_extractor();

            ex.input("img0", in0_padded);
            ex.input("img1", in1_padded);
            ex.input("flow", flow);
            ex.input("3", ctx0[0]);
            ex.input("4", ctx0[1]);
            ex.input("5", ctx0[2]);
            ex.input("6", ctx0[3]);
            ex.input("7", ctx1[0]);
            ex.input("8", ctx1[1]);
            ex.input("9", ctx1[2]);
            ex.input("10", ctx1[3]);

            ex.extract("output", out_padded);
        }

        ncnn::Mat out_padded_reversed;
        if (tta_temporal_mode)
        {
            // fusionnet
            {
                ncnn::Extractor ex = fusionnet.create_extractor();

                ex.input("img0", in1_padded);
                ex.input("img1", in0_padded);
                ex.input("flow", flow_reversed);
                ex.input("3", ctx1[0]);
                ex.input("4", ctx1[1]);
                ex.input("5", ctx1[2]);
                ex.input("6", ctx1[3]);
                ex.input("7", ctx0[0]);
                ex.input("8", ctx0[1]);
                ex.input("9", ctx0[2]);
                ex.input("10", ctx0[3]);

                ex.extract("output", out_padded_reversed);
            }
        }

        // cut padding and postproc
        out.create(w, h, 3);
        if (tta_temporal_mode)
        {
            for (int q = 0; q < 3; q++)
            {
                float* outptr = out.channel(q);
                const float* ptr = out_padded.channel(q);
                const float* ptr1 = out_padded_reversed.channel(q);

                for (int i = 0; i < h; i++)
                {
                    for (int j = 0; j < w; j++)
                    {
                        *outptr++ = (*ptr++ + *ptr1++) * 0.5f * 255.f + 0.5f;
                    }
                }
            }
        }
        else
        {
            for (int q = 0; q < 3; q++)
            {
                float* outptr = out.channel(q);
                const float* ptr = out_padded.channel(q);

                for (int i = 0; i < h; i++)
                {
                    for (int j = 0; j < w; j++)
                    {
                        *outptr++ = *ptr++ * 255.f + 0.5f;
                    }
                }
            }
        }
    }

    // download
    out.to_pixels((unsigned char*)outimage.data, ncnn::Mat::PIXEL_RGB);

    return 0;
}

#endif