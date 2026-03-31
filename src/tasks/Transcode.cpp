#include "Task.hpp"

#include <stack>
#include <fstream>
#include <filesystem>
#include <memory>
#include <chrono>

extern "C"{
    #include "libavutil/pixdesc.h"
}
#include "nlohmann/json.hpp"

#include "OutputVideo.hpp"
#include "FrameGetter/VideoFrameReader.hpp"
#include "FrameGetter/RifeFrameGetter.hpp"
#include "FrameGetter/RealCUGANFrameGetter.hpp"
#include "FrameGetter/RealESRGANFrameGetter.hpp"
#include "FrameGetter/BufferFrameGetter.hpp"
#include "GlobalConfig.hpp"
#include "PacketReader.hpp"
#include "PacketWriter.hpp"
#include "utils/Assert.hpp"
#include "utils/Logger.hpp"
#include "utils/FileStr.hpp"
#include "utils/Pause.hpp"

using namespace std;
using namespace filesystem;
using json=nlohmann::json;
namespace fs = filesystem;

// 扫描的配置目录
static const string _conf_dir="./configs/";

// 视频输出配置
struct _target{
    string ext, format, coder, pix_fmt;
    int maxw, maxh, crf;
    bool faststart;
    optional<double> size_limit;
};

// 视频帧处理配置
struct _process{
    struct _upscale{
        bool use_gpu=false;
        string engine;
        void *args;
        void clearArg() {
            if (!args) return;
            if (engine == "real-cugan") delete (RealCUGANFrameGetter::Args*)args;
            if (engine == "real-esrgan") delete (RealESRGANFrameGetter::Args*)args;
        }
        ~_upscale() {
            clearArg();
        }
    };
    struct _interpolation{
        string engine;
        bool process;
        double target_fps;
        void *args;
        void clearArg() {
            if (!args) return;
            if (engine == "rife") delete (RifeFrameGetter::Args*)args;
        }
        ~_interpolation() {
            clearArg();
        }
    };
    // 超分辨率配置
    optional<_upscale> upscale;
    // 插帧配置
    optional<_interpolation> interpolation;
};

// 从配置文件夹中读取配置
static json ReadConf(const string& conf, const string& conf_dir=_conf_dir) {
    fs::path path;
    if (!findConf(path, conf, conf_dir)) ThrowErr("未找到配置["+conf+"]");
    json config;
    ifstream f(path);
    if (!f.is_open()) ThrowErr("配置文件打开失败["+path.string()+"]");
    f >> config;
    return config;
}

// 将带import的配置文件展开成数组
static vector<json> FlatImports(const vector<string>& conf_names){
    vector<json> confs;
    unordered_map<string,bool> conf_open;
    stack<string> conf_to_read;

    // 反向入栈
    for (auto it=conf_names.rbegin() ; it!=conf_names.rend() ; ++it)
        conf_to_read.push(*it);
    
    while (!conf_to_read.empty()) {
        string conf_name = conf_to_read.top();
        conf_to_read.pop();
        // 已读取的配置不再重复读取
        if (conf_open[conf_name]) continue;
        json cfg = ReadConf(conf_name);
        conf_open[conf_name] = true;
        try {
            if (cfg["import"].is_array()) {
                auto& imports = cfg["import"];
                for (auto it=imports.rbegin(); it!=imports.rend() ; ++it)
                    conf_to_read.push((string)*it);
            }
            cfg["_name"] = conf_name;
            confs.push_back(cfg);
        } catch (string err) {
            ThrowErr(err);
        } catch (exception err) {
            ThrowErr(err.what());
        }
    }
    return confs;
}

// 读取输出配置，格式: "conf1;conf2;con3"
static vector<_target> ReadTargets(const string& conf_str){
    vector<json> confs = FlatImports(strsplit(conf_str,";"));
    vector<_target> targets;

    for (auto& conf:confs) {
        try {
            string name=conf["_name"];
            // AvLog("加载配置:%s\n",name.c_str());  //DEBUG
            // AvLog("%s\n",conf.dump(2).c_str());  //DEBUG
            if (conf["type"] != "out") throw "配置"+name+"不是输出配置";
            if (conf["targets"].is_array()){
                for(auto& target: conf["targets"]){
                    auto& target_v = targets.emplace_back();
                    target_v.coder = target.value("coder", "h264");
                    target_v.crf = target.value("crf", 23);
                    target_v.maxw = target.value("maxw", -1);
                    target_v.maxh = target.value("maxh", -1);
                    target_v.format = target.value("format", "mp4");
                    target_v.pix_fmt = target.value("pix_fmt", "yuv420p");
                    target_v.ext = target.value("ext", "mp4");
                    target_v.faststart = target.value("faststart", true);
                    target_v.size_limit = target.value("limit_size", -1.0);
                }
            }
        } catch(string err) {
            ThrowErr(err);
        } catch(exception err) {
            ThrowErr(err.what());
        }
    }
    return targets;
}

// 读取帧处理配置
static _process ReadProcess(const string& conf_str){
    vector<json> confs = FlatImports(strsplit(conf_str,";"));
    _process process;
    for (auto it=confs.rbegin() ; it!=confs.rend() ; ++it) {
        auto& conf = *it;
        try {
            string name = conf["_name"];
            // AvLog("加载配置:%s",name.c_str());  //DEBUG
            // AvLog("%s",conf.dump(2).c_str());  //DEBUG
            if (conf["type"] != "in") throw "配置"+name+"不是输入配置";
            //TODO 读取超分处理配置
            if (conf["upscale"]["enable"].is_boolean()){
                auto& json_v = conf["upscale"];
                if (json_v["enable"]) {
                    auto& pro_v = process.upscale.emplace();
                    pro_v.clearArg();
                    pro_v.engine = tolower((string)json_v["engine"]);
                    if (pro_v.engine == "real-cugan") {
                        RealCUGANFrameGetter::Args *args = new RealCUGANFrameGetter::Args;
                        args->use_gpu = json_v.value("use_gpu", true);
                        args->model = json_v.value("model", "models-se");
                        args->scale = json_v.value("scale", 2);
                        args->noise = json_v.value("noise", -1);
                        args->syncgap = json_v.value("syncgap", 3);
                        args->tilesize = json_v.value("tilesize", 0);
                        if (args->use_gpu) args->gpu_index = json_v.value("gpu_index", 0);
                        pro_v.args = args;
                    } else if (pro_v.engine == "real-esrgan") {
                        RealESRGANFrameGetter::Args *args = new RealESRGANFrameGetter::Args;
                        args->use_gpu = json_v.value("use_gpu", true);
                        args->model = json_v.value("model", "realesr-animevideov3");
                        args->scale = json_v.value("scale", 4);
                        args->tilesize = json_v.value("tilesize", 0);
                        if (args->use_gpu) args->gpu_index = json_v.value("gpu_index", 0);
                        pro_v.args = args;
                    }
                } else {
                    process.upscale.reset();
                }
            }
            if (conf["interpolation"]["enable"].is_boolean()) {
                auto& json_v = conf["interpolation"];
                if (json_v["enable"]) {
                    auto& pro_v = process.interpolation.emplace();
                    pro_v.clearArg();
                    pro_v.engine = tolower((string)json_v["engine"]);
                    pro_v.process = json_v.value("process", true);
                    pro_v.target_fps = json_v.value("target_fps", 60.0);
                    if (pro_v.engine == "rife") {
                        RifeFrameGetter::Args *args = new RifeFrameGetter::Args;
                        args->use_gpu = json_v.value("use_gpu", true);
                        args->model = json_v.value("model", "rife-v4.22-lite");
                        if (args->use_gpu) args->gpu_index = json_v.value("gpu_index", 0);
                        pro_v.args = args;
                    }
                } else {
                    process.interpolation.reset();
                }
            }
        } catch (string err) {
            ThrowErr(err);
        } catch (exception err) {
            ThrowErr(err.what());
        }
    }
    return process;
}

// 根据编码器名称找到编码器
static const AVCodec* searchEncoder(const std::string &codec_name){
    const AVCodec *codec = avcodec_find_encoder_by_name(codec_name.c_str());
    if(!codec) codec = avcodec_find_decoder_by_name(codec_name.c_str());
    if(!codec) ThrowErr("找不到编码器"+codec_name);
    return avcodec_find_encoder(codec->id);
}

bool Task::_taskTranscode() {
    const int TOTAL_INPUTS = inputs.size();
    // 设置输出目标配置
    vector<_target> targets = ReadTargets(getStr("conf_out"));
    if (targets.empty()) ThrowErr("请指定视频输出配置");
    const int TOTAL_TARGETS = targets.size();

    // 读取处理配置
    _process process_cfg = ReadProcess(getStr("conf_in"));

    AvLog("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
    AvLog("Task: Transcode\n");
    AvLog("Inputs: %d\n", TOTAL_INPUTS);
    AvLog("Targets: %d\n", TOTAL_TARGETS);
    if (process_cfg.upscale) AvLog("  + upscale\n");
    if (process_cfg.interpolation) AvLog("  + interpolation\n");
    AvLog("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");

    // 构建输出目标并执行
    // 输入视频序号
    int input_index = -1;
    while (!inputs.empty() && !GlobalConfig.interrupted) {
        InputVideo vd_in = move(inputs.front());
        inputs.pop_front();
        ++input_index;
        vd_in.print();

        auto& info = vd_in.getInfo();
        int outw=info.width, outh=info.height;
        AVRational outfps = info.fps;

        shared_ptr<VideoFrameReader> vfr = make_shared<VideoFrameReader>(vd_in, true);
        shared_ptr<IFreamGetter> frameReader = vfr;
        
        // 初始化帧处理程序
        if (process_cfg.upscale) {
            auto& cfg = process_cfg.upscale.value();
            if (cfg.engine == "real-cugan") {
                RealCUGANFrameGetter::Args& args = *(RealCUGANFrameGetter::Args*)cfg.args;
                // 若不超分也不降噪，直接跳过这步处理
                if (args.scale!=1 || args.noise!=-1) {
                    if (args.model.find("models-nose") != string::npos) args.syncgap = 0;
                    auto realcuganGetter = make_shared<RealCUGANFrameGetter>(frameReader, args);
                    frameReader = realcuganGetter;
                    outw *= args.scale;
                    outh *= args.scale;
                }
            } else if (cfg.engine == "real-esrgan") {
                RealESRGANFrameGetter::Args& args = *(RealESRGANFrameGetter::Args*)cfg.args;
                // 若不超分，直接跳过这步处理
                if (args.scale != 1) {
                    auto realesrganGetter = make_shared<RealESRGANFrameGetter>(frameReader, args);
                    frameReader = realesrganGetter;
                    outw *= args.scale;
                    outh *= args.scale;
                }
            } else {
                if (GlobalConfig.exit_on_error) ThrowErr("未知补帧模型[" + cfg.engine + "]");
                AvLog("未知超分辨率引擎[%s]，已跳过超分辨率", cfg.engine.c_str());
            }
        }
        if (process_cfg.interpolation) {
            auto& cfg = process_cfg.interpolation.value();
            if (cfg.engine == "rife") {
                auto rifeGetter = make_shared<RifeFrameGetter>(frameReader, *(RifeFrameGetter::Args*)cfg.args);
                // 计算插帧倍率和最终帧率，原帧率四舍五入（23.976 -> 24）
                AVRational fpsx = av_div_q(
                    av_d2q(cfg.target_fps, 1000000)
                    , {(int)round(av_q2d(info.fps)),1}
                );
                outfps = av_mul_q(outfps,fpsx);
                rifeGetter->setFPSX(fpsx);
                if(cfg.process){
                    string scob_path=vd_in.getPath()+".scob";
                    try{
                        Score s = Score::LoadScob(scob_path);
                        rifeGetter->setProcess(true, &s);
                    }catch(...){
                        AvLog("文件%s打开失败，跳过帧处理\n",scob_path.c_str());
                    }
                }
                frameReader = rifeGetter;
            } else {
                if (GlobalConfig.exit_on_error) ThrowErr("未知补帧模型[" + cfg.engine + "]");
                AvLog("未知补帧引擎[%s]，已跳过补帧", cfg.engine.c_str());
            }
        }

        // 计算当前任务每个输出文件的路径
        unordered_map<fs::path,bool> exist_files;
        vector<OutputVideo> outputs;
        vector<PacketWriter> writers;
        vector<FrameConvert> converters;
        for(auto& target: targets){
            fs::path path = vd_in.getPath();
            path.replace_extension(target.ext);
            // 防止重名输出
            {
                string originName = path.stem().string();
                for(int suffix_num=0 ; fs::exists(path)||exist_files[path] ; ++suffix_num){
                    path.replace_filename(originName+"-"+to_string(suffix_num)+".0").replace_extension(target.ext);
                }
                exist_files[path]=true;
            }
            // 生成输出视频文件
            auto& vd_out = outputs.emplace_back(path.string());
            vd_out.setFormat(target.format)
                .setEncoder(searchEncoder(target.coder))
                .setPixelFormat((AVPixelFormat)AssertI(av_get_pix_fmt(target.pix_fmt.c_str())))
                .setFPS(outfps)
                .setVSTimebase(vd_in.getVS()->time_base)
                .setOption("crf", target.crf);
            if (target.faststart) vd_out.setOption("movflags", "faststart");
            // 计算输出宽高
            {
                const int ALIGN=2;  // 宽高为2的倍数
                int _tmpi;
                double _tmpd=av_q2d(info.sar), w=outw, h=outh;
                // 还原真实像素大小
                if (_tmpd <  1) w*=_tmpd;
                else if (_tmpd > 1) h/=_tmpd;
                // 统一为"宽>高"方便处理
                int& maxw=target.maxw, maxh=target.maxh;
                if (maxw < maxh) { _tmpi=maxw, maxw=maxh, maxh=_tmpi; }
                // 竖屏视频
                const bool vertical = outw<=outh;
                if (vertical) _tmpd=w, w=h, h=_tmpd;
                if (maxw>0 && w>maxw) {   // 宽度超限制
                    h = maxw*h/w;
                    w = maxw;
                }
                if (maxh>0 && h>maxh) {   // 高度超限制
                    w = maxh*w/h;
                    h = maxh;
                }
                if (vertical) _tmpd=w, w=h, h=_tmpd;
                // 转化为2的倍数
                w = round(w/ALIGN)*ALIGN;
                h = round(h/ALIGN)*ALIGN;

                vd_out.setWxH(w, h);
                converters.emplace_back(w, h, vd_out.getInfo().pix_fmt);
            }
            // 为每个流配置映射
            unordered_map<int,int> mapping;
            // 复制音频流
            for(auto& a_stream: vd_in.getASs()) {
                AVStream* new_audio = vd_out.addAudio(a_stream);
                mapping[a_stream->index] = new_audio->index;
            }
            mapping[vd_in.getVS()->index] = vd_out.getVS()->index;
            // 创建PacketWriter
            writers.emplace_back(vd_out)
                .setMapping(mapping);
        }

        // 转码
        int frame_num = 0;
        auto& nowTime = chrono::steady_clock::now;
        auto timeSince = [&nowTime](chrono::steady_clock::time_point start_time){
            return chrono::duration_cast<chrono::milliseconds>(nowTime()-start_time).count();
        };
        
        const int PROGRESS_SIZE = 10;
        struct _progress{
            int64_t time_cost;
            int frame_gen;
            _progress():time_cost(0),frame_gen(0){}
        } progress[PROGRESS_SIZE];
        int progress_index = 0;
        int64_t time_cost = 0;
        int frame_gen = 0;
        auto last_print_time = nowTime();
        int last_frame_num = 0;
        
        const double TotalTime = vd_in.getVS()->duration * av_q2d(vd_in.getVS()->time_base);
        const string TotalTimeStr = getTimeStr(TotalTime);
        const int64_t& IN_BYTES = vd_in.getFormatContext()->pb->bytes_read;

        PacketReader pkt_reader(vd_in);
        PacketWriter *pw = writers.data();
        FrameConvert *fc = converters.data();
        AVPacket* pkt = nullptr;
        HvFrame fr_raw, fr_out;
        AVStream** IN_STREAMS = vd_in.getFormatContext()->streams;
        AVStream* _stream;
        AVMediaType type;
        while ((pkt = pkt_reader.NextPacket()) && !GlobalConfig.interrupted) {
            _stream = IN_STREAMS[pkt->stream_index];
            type = _stream->codecpar->codec_type;
            if (type == AVMEDIA_TYPE_VIDEO) {
                vfr->addPacket(pkt);    // 解析输入的视频packet
                while (frameReader->nextFrame(fr_raw)) {
                    fr_raw.fr->pict_type = AVPictureType::AV_PICTURE_TYPE_NONE;
                    for (int j=0 ; j<TOTAL_TARGETS ; ++j)  // 为每个输出文件写入转换后对应的帧
                        pw[j].sendVideoFrame(fc[j].convert(fr_raw, fr_out));
                    ++frame_num;
                }
            } else if (type == AVMEDIA_TYPE_AUDIO) {
                AVRational* a_in_timebase = &(_stream->time_base);
                for (int j=0 ; j<TOTAL_TARGETS ; ++j) {
                    pw[j].sendPacket(pkt, a_in_timebase);
                }
            }
            int64_t time_delta = timeSince(last_print_time);
#ifdef DEBUG
            if (time_delta > 1000) {
#else // DEBUG
            if (time_delta > 5000) {
                // 每至少5秒打印一次输出
#endif // DEBUG
                auto& p  = progress[(progress_index++)%PROGRESS_SIZE];
                time_cost += time_delta - p.time_cost;
                frame_gen += (frame_num-last_frame_num) - p.frame_gen;
                p.time_cost = time_delta;
                p.frame_gen = frame_num-last_frame_num;
                
                const double CurrentTime = pkt->pts * av_q2d(_stream->time_base);
                const string CurrentTimeStr = getTimeStr(CurrentTime);
                AvLog("Frame[%5d] : %s / %s", frame_num, CurrentTimeStr.c_str(), TotalTimeStr.c_str());
                if (CurrentTime > 0) {
                    AvLog("\t%8.2lfkbps ->", IN_BYTES/(125*CurrentTime));
                    for (auto& output: outputs)
                        AvLog(" %8.2lfkbps", output.getFormatContext()->pb->bytes_written/(125*CurrentTime));
                }
                if (frame_gen > 0) {
                    AvLog("  \tLeft Time: %s", getTimeStr((info.num_frames-frame_num)*time_cost/(1000.0*frame_gen), true).c_str());
                }
                AvLog("\n");
                last_frame_num = frame_num;
                last_print_time = nowTime();
                time_delta = 0;
            }
            if (isKeyPressed('P')) {
                if (time_delta) {
                    auto& p  = progress[(progress_index++)%PROGRESS_SIZE];
                    time_cost += time_delta - p.time_cost;
                    frame_gen += (frame_num-last_frame_num) - p.frame_gen;
                    p.time_cost = time_delta;
                    p.frame_gen = frame_num-last_frame_num;
                }
                // 暂停转码
                AvLog("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n\n");
                AvLog("转码暂停，按下C键继续\n");
                AvLog("当前输入：[%d/%d] %s\n", input_index, TOTAL_INPUTS, vd_in.getPath().c_str());
                AvLog("当前进度：[%d / %d] (%s / %s)\n", frame_num, info.num_frames
                    , getTimeStr(pkt->pts, _stream->time_base).c_str(), TotalTimeStr.c_str());
                AvLog("\n<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
                waitForKey('C');
                AvLog("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n\n");
                AvLog("暂停结束\n");
                AvLog("\n<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
                last_frame_num = frame_num;
                last_print_time = nowTime();
            }
            av_packet_unref(pkt);
        }
        // 处理剩余帧
        vfr->addPacket(NULL);
        while (frameReader->nextFrame(fr_raw) && !GlobalConfig.interrupted) {
            fr_raw.fr->pict_type = AVPictureType::AV_PICTURE_TYPE_NONE;
            for (int j=0 ; j<TOTAL_TARGETS ; ++j)
                pw[j].sendVideoFrame(fc[j].convert(fr_raw, fr_out));
            ++frame_num;
        }
        for (int j=0 ; j<TOTAL_TARGETS ; ++j)
            pw[j].writeEnd(); // 清除缓冲区
    }
    return true;
}