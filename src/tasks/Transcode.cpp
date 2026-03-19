#include "Task.hpp"

#include <stack>
#include <fstream>
#include <filesystem>
#include <memory>
// #include <unordered_map>

extern "C"{
    #include "libavutil/pixdesc.h"
}
#include "nlohmann\json.hpp"

#include "OutputVideo.hpp"
#include "FrameGetter/VideoFrameReader.hpp"
#include "FrameGetter/RifeFrameGetter.hpp"
#include "FrameGetter/BufferFrameGetter.hpp"
#include "GlobalConfig.hpp"
#include "PacketReader.hpp"
#include "PacketWriter.hpp"
#include "utils/Assert.hpp"
#include "utils/Logger.hpp"
#include "utils/FileStr.hpp"

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
        string model;
        int upscale;
        int denoise;
        void *args;
        ~_upscale(){
            if(args) return;
        }
    };
    struct _interpolation{
        string engine;
        bool process;
        double target_fps;
        void *args;
        ~_interpolation(){
            if(!args) return;
            if(engine=="rife") delete (RifeFrameGetter::Args*)args;
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
                    conf_to_read.push(tolower((string)*it));
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
            //TODO 读取处理配置
            if (conf["upscale"]["enable"].is_boolean()){
                auto& json_v = conf["upscale"];
                if (json_v["enable"]) {
                    auto& pro_v = process.upscale.emplace();
                    pro_v.engine = json_v.value("engine", "real-cugan");
                    pro_v.use_gpu = json_v.value("use_gpu", false);
                    pro_v.model = json_v.value("model", "models-nose");
                    pro_v.upscale = json_v.value("upscale", 2);
                    pro_v.denoise = json_v.value("denoise", 0);
                } else {
                    process.upscale.reset();
                }
            }
            if (conf["interpolation"]["enable"].is_boolean()) {
                auto& json_v = conf["interpolation"];
                if (json_v["enable"]) {
                    auto& pro_v = process.interpolation.emplace();
                    pro_v.engine = tolower((string)json_v["engine"]);
                    pro_v.process = json_v.value("process", true);
                    pro_v.target_fps = json_v.value("target_fps", 60.0);
                    if (pro_v.engine == "rife") {
                        RifeFrameGetter::Args *args = new RifeFrameGetter::Args;
                        args->use_gpu = json_v.value("use_gpu", true);
                        args->model = json_v.value("model", "rife-v4.22-lite");
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

bool Task::_taskTranscode(){
    // 设置输出目标配置
    vector<_target> targets = ReadTargets(getStr("conf_out"));
    if (targets.empty()) ThrowErr("请指定视频输出配置");
    const int num_out = targets.size();

    // 读取处理配置
    _process process_cfg = ReadProcess(getStr("conf_in"));

    AvLog("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
    AvLog("Task: Transcode\n");
    AvLog("Targets: %d\n", num_out);
    if (process_cfg.upscale) AvLog("  + upscale\n");
    if (process_cfg.interpolation) AvLog("  + interpolation\n");
    AvLog("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");

    // 构建输出目标并执行
    // i：输入视频序号
    for (int i=0 ; i<inputs.size() ; ++i) {
        auto& vd_in = inputs[i];
        vd_in.print();
        auto& info = vd_in.getInfo();

        int outw=info.width, outh=info.height;
        AVRational outfps = info.fps;

        shared_ptr<VideoFrameReader> vfr = make_shared<VideoFrameReader>(vd_in, true);
        shared_ptr<IFreamGetter> frameReader = vfr;
        
        // 初始化帧处理程序
        if (process_cfg.upscale) {
            // TODO 支持超分处理
            ThrowErr("暂不支持超分");
        }
        if (process_cfg.interpolation) {
            auto& cfg = process_cfg.interpolation.value();
            if (cfg.engine == "rife") {
                auto rifeGetter = make_shared<RifeFrameGetter>(frameReader,*(RifeFrameGetter::Args*)cfg.args);
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
                for(int i=0 ; fs::exists(path)||exist_files[path] ; ++i){
                    path.replace_filename(originName+"-"+to_string(i)+".0").replace_extension(target.ext);
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
            // 复制音频流
            for(auto& a_stream: vd_in.getASs())
                vd_out.addAudio(a_stream);
            // 创建PacketWriter并为每个流配置映射
            writers.emplace_back(vd_out, vd_in);
        }

        // 转码
        int frame_num = 0;
        int64_t& bytesRead = vd_in.getFormatContext()->pb->bytes_read;
        int64_t bytesProcessed = 0;
        const string TotalTime = getTimeStr(vd_in.getVS()->duration, vd_in.getVS()->time_base);

        PacketReader pkt_reader(vd_in);
        PacketWriter *pw = writers.data();
        FrameConvert *fc = converters.data();
        AVPacket* pkt = nullptr;
        AVFrame* fr = nullptr;
        AVStream** IN_STREAMS = vd_in.getFormatContext()->streams;
        AVStream* _stream;
        AVMediaType type;
        while ((pkt = pkt_reader.NextPacket()) && !GlobalConfig.interrupted) {
            _stream = IN_STREAMS[pkt->stream_index];
            type = _stream->codecpar->codec_type;
            if (type == AVMEDIA_TYPE_VIDEO) {
                vfr->addPacket(pkt);    // 解析输入的视频packet
                while (fr = frameReader->nextFrame()) {
                    fr->pict_type = AVPictureType::AV_PICTURE_TYPE_NONE;
                    for (int j=0 ; j<num_out ; ++j)  // 为每个输出文件写入转换后对应的帧
                        pw[j].sendVideoFrame(fc[j].convert(fr));
                    ++frame_num;
                }
            } else if (type == AVMEDIA_TYPE_AUDIO) {
                AVRational* a_in_timebase = &(_stream->time_base);
                for (int j=0 ; j<num_out ; ++j) {
                    pw[j].sendPacket(pkt, a_in_timebase);
                }
            }
            if (bytesRead-bytesProcessed > 2*1024*1024) {
                bytesProcessed = bytesRead;
                AvLog("Frame[%5d] : %s / %s\n", frame_num, getTimeStr(pkt->pts, _stream->time_base).c_str(), TotalTime.c_str());
            }
            av_packet_unref(pkt);
        }
        // 处理剩余帧
        vfr->addPacket(NULL);
        while ((fr = frameReader->nextFrame()) && !GlobalConfig.interrupted) {
            fr->pict_type = AVPictureType::AV_PICTURE_TYPE_NONE;
            for (int j=0 ; j<num_out ; ++j)
                pw[j].sendVideoFrame(fc[j].convert(fr));
            ++frame_num;
        }
        for (int j=0 ; j<num_out ; ++j)
            pw[j].writeEnd(); // 清除缓冲区
    }
    return true;
}