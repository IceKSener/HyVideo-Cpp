#include "Common.hpp"
#include <string>
#include <cstdio>
extern "C" {
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavutil/imgutils.h"
    #include "libswscale/swscale.h"
}

using namespace std;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "用法: %s <输入视频> <输出视频>\n", argv[0]);
        return -1;
    }
    
    string input_path = argv[1];
    string output_path = argv[2];
    
    try {
        // ========== 打开输入文件 ==========
        AVFormatContext *input_fmt_ctx = nullptr;
        Assert(avformat_open_input(&input_fmt_ctx, input_path.c_str(), NULL, NULL));
        Assert(avformat_find_stream_info(input_fmt_ctx, NULL));
        
        // 查找视频流
        int video_stream_index = -1;
        const AVCodec *input_codec = nullptr;
        AVCodecContext *decoder_ctx = nullptr;
        
        for (int i = 0; i < input_fmt_ctx->nb_streams; i++) {
            if (input_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream_index = i;
                AVCodecParameters *codecpar = input_fmt_ctx->streams[i]->codecpar;
                input_codec = avcodec_find_decoder(codecpar->codec_id);
                AssertP(input_codec);
                
                AssertP(decoder_ctx = avcodec_alloc_context3(input_codec));
                Assert(avcodec_parameters_to_context(decoder_ctx, codecpar));
                Assert(avcodec_open2(decoder_ctx, input_codec, NULL));
                break;
            }
        }
        
        if (video_stream_index < 0) {
            ThrowErr("未找到视频流");
        }
        
        AVStream *input_stream = input_fmt_ctx->streams[video_stream_index];
        int width = decoder_ctx->width;
        int height = decoder_ctx->height;
        AVRational fps = input_stream->avg_frame_rate;
        AVRational time_base = input_stream->time_base;
        
        AvLog("输入视频: %dx%d, FPS: %.3f, 编码器: %s\n", 
              width, height, av_q2d(fps), input_codec->name);
        
        // ========== 创建输出文件 ==========
        AVFormatContext *output_fmt_ctx = nullptr;
        Assert(avformat_alloc_output_context2(&output_fmt_ctx, NULL, "mp4", output_path.c_str()));
        
        // 创建视频流
        AVStream *output_stream = avformat_new_stream(output_fmt_ctx, NULL);
        AssertP(output_stream);
        
        // 查找HEVC编码器
        const AVCodec *output_codec = avcodec_find_encoder_by_name("libx265");
        if (!output_codec) {
            output_codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
        }
        AssertP(output_codec);
        
        AVCodecContext *encoder_ctx = nullptr;
        AssertP(encoder_ctx = avcodec_alloc_context3(output_codec));
        
        // 设置编码器参数
        encoder_ctx->codec_id = AV_CODEC_ID_HEVC;
        encoder_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
        encoder_ctx->width = width;
        encoder_ctx->height = height;
        encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        encoder_ctx->time_base = time_base;
        encoder_ctx->framerate = fps;
        encoder_ctx->bit_rate = 0;  // CRF模式不需要设置bitrate
        
        // 设置CRF=18
        AVDictionary *opts = nullptr;
        av_dict_set_int(&opts, "crf", 18, 0);
        
        if (output_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            encoder_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        
        Assert(avcodec_open2(encoder_ctx, output_codec, &opts));
        av_dict_free(&opts);
        
        // 复制编码器参数到输出流
        Assert(avcodec_parameters_from_context(output_stream->codecpar, encoder_ctx));
        output_stream->time_base = encoder_ctx->time_base;
        output_stream->avg_frame_rate = fps;
        
        // 打开输出文件
        if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            Assert(avio_open2(&output_fmt_ctx->pb, output_path.c_str(), AVIO_FLAG_WRITE, NULL, NULL));
        }
        
        Assert(avformat_write_header(output_fmt_ctx, NULL));
        
        AvLog("输出视频: %dx%d, FPS: %.3f, 编码器: %s (CRF=18)\n", 
              width, height, av_q2d(fps), output_codec->name);
        
        // ========== 转码循环 ==========
        AVPacket *packet = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        AVFrame *frame_yuv = nullptr;
        SwsContext *sws_ctx = nullptr;
        
        // 如果需要格式转换，创建SWS上下文
        if (decoder_ctx->pix_fmt != AV_PIX_FMT_YUV420P) {
            AssertP(frame_yuv = av_frame_alloc());
            frame_yuv->format = AV_PIX_FMT_YUV420P;
            frame_yuv->width = width;
            frame_yuv->height = height;
            Assert(av_frame_get_buffer(frame_yuv, 0));
            
            sws_ctx = sws_getContext(
                width, height, decoder_ctx->pix_fmt,
                width, height, AV_PIX_FMT_YUV420P,
                SWS_BILINEAR, NULL, NULL, NULL
            );
            AssertP(sws_ctx);
        }
        
        int frame_count = 0;
        bool eof_reached = false;
        
        // 主循环：读取并解码
        while (!eof_reached) {
            // 读取数据包
            int ret = av_read_frame(input_fmt_ctx, packet);
            if (ret == AVERROR_EOF) {
                // 到达文件末尾，发送NULL到解码器以刷新
                Assert(avcodec_send_packet(decoder_ctx, NULL));
                eof_reached = true;
            } else if (ret < 0) {
                Assert(ret);
            } else {
                if (packet->stream_index == video_stream_index) {
                    // 发送数据包到解码器
                    Assert(avcodec_send_packet(decoder_ctx, packet));
                }
                av_packet_unref(packet);
            }
            
            // 解码帧
            while (true) {
                ret = avcodec_receive_frame(decoder_ctx, frame);
                if (ret == AVERROR(EAGAIN)) {
                    break;  // 需要更多输入
                } else if (ret == AVERROR_EOF) {
                    break;  // 解码完成
                } else if (ret < 0) {
                    Assert(ret);
                }
                
                // 格式转换（如果需要）
                AVFrame *frame_to_encode = frame;
                if (frame_yuv) {
                    sws_scale(sws_ctx,
                        (const uint8_t* const*)frame->data, frame->linesize, 0, height,
                        frame_yuv->data, frame_yuv->linesize);
                    frame_yuv->pts = frame->pts;
                    frame_yuv->pkt_dts = frame->pkt_dts;
                    frame_yuv->duration = frame->duration;
                    frame_to_encode = frame_yuv;
                }
                
                // 设置时间戳
                frame_to_encode->pts = av_rescale_q(frame->pts, time_base, encoder_ctx->time_base);
                
                // 发送帧到编码器
                Assert(avcodec_send_frame(encoder_ctx, frame_to_encode));
                
                // 接收编码后的数据包
                AVPacket *enc_packet = av_packet_alloc();
                while (true) {
                    ret = avcodec_receive_packet(encoder_ctx, enc_packet);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        Assert(ret);
                    }
                    
                    // 写入输出文件
                    enc_packet->stream_index = output_stream->index;
                    av_packet_rescale_ts(enc_packet, encoder_ctx->time_base, output_stream->time_base);
                    Assert(av_interleaved_write_frame(output_fmt_ctx, enc_packet));
                    
                    av_packet_unref(enc_packet);
                }
                av_packet_free(&enc_packet);
                
                frame_count++;
                if (frame_count % 100 == 0) {
                    AvLog("已处理 %d 帧\n", frame_count);
                }
            }
        }
        
        // 刷新编码器：发送NULL帧并接收所有剩余的编码包
        Assert(avcodec_send_frame(encoder_ctx, NULL));
        while (true) {
            AVPacket *enc_packet = av_packet_alloc();
            int ret = avcodec_receive_packet(encoder_ctx, enc_packet);
            if (ret == AVERROR_EOF) {
                av_packet_free(&enc_packet);
                break;  // 编码完成
            } else if (ret == AVERROR(EAGAIN)) {
                av_packet_free(&enc_packet);
                break;
            } else if (ret < 0) {
                av_packet_free(&enc_packet);
                Assert(ret);
            }
            
            // 写入输出文件
            enc_packet->stream_index = output_stream->index;
            av_packet_rescale_ts(enc_packet, encoder_ctx->time_base, output_stream->time_base);
            Assert(av_interleaved_write_frame(output_fmt_ctx, enc_packet));
            
            av_packet_unref(enc_packet);
            av_packet_free(&enc_packet);
        }
        
        // 写入trailer
        av_write_trailer(output_fmt_ctx);
        
        AvLog("转码完成！共处理 %d 帧\n", frame_count);
        
        // ========== 清理资源 ==========
        if (sws_ctx) sws_freeContext(sws_ctx);
        if (frame_yuv) av_frame_free(&frame_yuv);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        if (output_fmt_ctx && !(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_fmt_ctx->pb);
        }
        avformat_free_context(output_fmt_ctx);
        avformat_close_input(&input_fmt_ctx);
        
    } catch (string errMsg) {
        av_log(NULL, AV_LOG_ERROR, "%s\n", errMsg.c_str());
        return -1;
    } catch (exception e) {
        av_log(NULL, AV_LOG_ERROR, "异常: %s\n", e.what());
        return -1;
    }
    
    return 0;
}

