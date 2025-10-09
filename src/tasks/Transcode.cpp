#include "Task.hpp"
#include "Common.hpp"
#include "RifeFrameGetter.hpp"
#include "OutputVideo.hpp"
#include "VideoFrameReader.hpp"
#include "PacketReader.hpp"
#include "PacketWriter.hpp"
extern "C"{
    #include "libavutil/pixdesc.h"
}
#include "nlohmann\json.hpp"
#include <stack>
#include <fstream>
#include <filesystem>
#include <memory>

using namespace std;
using json=nlohmann::json;
namespace fs = filesystem;

static const std::string conf_dir="./configs/";
struct _target{
    string ext, format, coder, pix_fmt;
    int maxw,maxh,crf;
    bool faststart;
    optional<double> size_limit;
};
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
            if(engine=="rife") delete (RifeArgs*)args;
        }
    };
    optional<_upscale> upscale;
    optional<_interpolation> interpolation;
}processes;

// 从配置文件夹中读取配置
json ReadConf(const string& conf, const string& conf_dir=conf_dir){
    fs::path path;
    if(!findConf(path, conf, conf_dir)) ThrowErr("未找到配置["+conf+"]");
    json config;
    ifstream f(path);
    f>>config;
    f.close();
    return config;
}
// 将带import的配置文件展开成数组
static vector<json> FlatImports(const vector<string>& conf_strs){
    vector<json> confs;
    map<string,bool> conf_open;
    stack<string> conf_toread;

    for(auto it=conf_strs.rbegin() ; it!=conf_strs.rend() ; ++it)
        conf_toread.push(*it);
    while(!conf_toread.empty()){
        string conf_str=conf_toread.top();
        conf_toread.pop();
        if(conf_open[conf_str]) continue;
        json cfg=ReadConf(conf_str);
        conf_open[conf_str]=true;
        try{
            if(cfg["import"].is_array()){
                auto& imports=cfg["import"];
                for(auto it=imports.rbegin(); it!=imports.rend() ; ++it)
                    conf_toread.push(*it);
            }
            cfg["_name"]=conf_str;
            confs.push_back(cfg);
        }catch(string err){
            ThrowErr(err);
        }catch(exception err){
            ThrowErr(err.what());
        }
    }
    return confs;
}

// 读取输出配置
static vector<_target> ReadTargets(const string& conf_str){
    vector<json> confs = FlatImports(strsplit(conf_str,";"));
    vector<_target> targets;

    for(auto& conf:confs){
        try{
            string name=conf["_name"];
            // AvLog("加载配置:%s\n",name.c_str());  //DEBUG
            // AvLog("%s\n",conf.dump(2).c_str());  //DEBUG
            if(conf["type"]!="out") throw "配置"+name+"不是输出配置";
            if(conf["targets"].is_array()){
                for(auto& target:conf["targets"]){
                    auto& target_v=targets.emplace_back();
                    target_v.coder=target.value("coder","h264");
                    target_v.crf=target.value("crf",23);
                    target_v.maxw=target.value("maxw",-1);
                    target_v.maxh=target.value("maxh",-1);
                    target_v.format=target.value("format","mp4");
                    target_v.pix_fmt=target.value("pix_fmt","mp4");
                    target_v.ext=target.value("ext","mp4");
                    target_v.faststart=target.value("faststart",true);
                    target_v.size_limit=target.value("limit_size",-1.0);
                }
            }
        }catch(string err){
            ThrowErr(err);
        }catch(exception err){
            ThrowErr(err.what());
        }
    }
    return targets;
}
// 读取帧处理配置
static _process ReadProcess(const string& conf_str){
    vector<json> confs = FlatImports(strsplit(conf_str,";"));
    _process process;
    for(auto& conf:confs){
        try{
            string name=conf["_name"];
            AvLog("加载配置:%s",name.c_str());  //DEBUG
            AvLog("%s",conf.dump(2).c_str());  //DEBUG
            if(conf["type"]!="in") throw "配置"+name+"不是输入配置";
            //TODO 读取处理配置
            if(conf["upscale"]["enable"].is_boolean()){
                auto& json_v=conf["upscale"];
                if(json_v["enable"]){
                    auto& pro_v=processes.upscale.emplace();
                    pro_v.engine=json_v.value("engine", "real-cugan");
                    pro_v.use_gpu=json_v.value("use_gpu", false);
                    pro_v.model=json_v.value("model", "models-nose");
                    pro_v.upscale=json_v.value("upscale", 2);
                    pro_v.denoise=json_v.value("denoise", 0);
                }else{
                    processes.upscale.reset();
                }
            }
            if(conf["interpolation"]["enable"].is_boolean()){
                auto& json_v=conf["interpolation"];
                if(json_v["enable"]){
                    auto& pro_v=processes.interpolation.emplace();
                    pro_v.engine=tolower((string)json_v["engine"]);
                    pro_v.process=json_v.value("process", true);
                    pro_v.target_fps=json_v.value("target_fps", 60);
                    if(pro_v.engine=="rife"){
                        RifeArgs *args=new RifeArgs;
                        args->use_gpu=json_v.value("use_gpu", true);
                        args->model=json_v.value("model", "rife-v4.22-lite");
                    }
                }else{
                    processes.interpolation.reset();
                }
            }
        }catch(string err){
            ThrowErr(err);
        }catch(exception err){
            ThrowErr(err.what());
        }
    }
    return process;
}

bool Task::_taskTranscode(){
    //设置输出目标配置
    vector<_target> targets = ReadTargets(getStr(args,"conf_out"));
    if(targets.empty()) ThrowErr("请指定视频输出配置");
    const int num_out=targets.size();

    //TODO 读取处理配置
    _process process_cfg=ReadProcess(getStr(args,"conf_in"));

    // 构建输出目标并执行
    for(int i=0; i<inputs.size() ; ++i){
        auto& vd_in=inputs[i];

        int outw=vd_in.width, outh=vd_in.height;
        AVRational outfps=vd_in.fps;

        shared_ptr<VideoFrameReader> vfr = make_shared<VideoFrameReader>(vd_in, true);
        shared_ptr<IFreamGetter> frameReader = vfr;
        
        //TODO 添加帧处理
        if(process_cfg.upscale){
            ThrowErr("暂不支持超分");
        }
        if(process_cfg.interpolation){
            ThrowErr("暂不支持补帧");
        }

        map<fs::path,bool> exist_files;
        vector<OutputVideo> outputs;
        vector<PacketWriter> writers;
        for(auto& target:targets){
            // 计算输出路径
            fs::path path=vd_in.path;
            path.replace_extension(target.ext);
            {
                string originName=path.stem().string();
                for(int i=0; fs::exists(path)||exist_files[path] ; ++i){
                    path.replace_filename(originName+"-"+to_string(i)+".0").replace_extension(target.ext);
                }
                exist_files[path]=true;
            }

            // 生成输出视频文件
            auto& vd_out=outputs.emplace_back(path.string());
            vd_out.SetFormat(target.format)
                .SetEncoder((const AVCodec*)AssertP(searchEncoder(target.coder)))
                .SetPixelFormat((AVPixelFormat)AssertI(av_get_pix_fmt(target.pix_fmt.c_str())))
                .SetFPS(vd_in.fps)
                .SetVSTimebase(vd_in.v_stream->time_base)
                .SetOption("crf", target.crf);
            // 计算输出宽高
            {
                const int ALIGN=2;
                int _tmpi;
                double _tmpd=av_q2d(vd_in.sar), w=outw, h=outh;
                if(_tmpd<1) w*=_tmpd;
                else if(_tmpd>1) h/=_tmpd;
                int& maxw=target.maxw, maxh=target.maxh;
                if(maxw<maxh){ _tmpi=maxw, maxw=maxh, maxh=_tmpi; }
                const bool vertical=outw<=outh;

                if(vertical) _tmpd=w, w=h, h=_tmpd;
                if(maxw>0 && w>maxw){
                    h=maxw*h/w;
                    w=maxw;
                }
                if(maxh>0 && h>maxh){
                    w=maxh*w/h;
                    h=maxh;
                }
                if(vertical) _tmpd=w, w=h, h=_tmpd;
                w=round(w/ALIGN)*ALIGN;
                h=round(h/ALIGN)*ALIGN;

                vd_out.SetWxH(w, h);
            }

            for(auto& a_stream:vd_in.a_streams)
                vd_out.AddAudio(a_stream);

            writers.emplace_back(vd_out, vd_in);
        }

        // 转码
        PacketReader pkt_reader(vd_in);
        AVPacket* pkt=nullptr;
        AVFrame* fr=nullptr;
        PacketWriter *pw=writers.data();
        int frame_num=0;
        AVMediaType type;
        while(pkt=pkt_reader.NextPacket()){
            type=vd_in.fmt_ctx->streams[pkt->stream_index]->codecpar->codec_type;
            if(type==AVMEDIA_TYPE_VIDEO){
                vfr->AddPacket(pkt);
                while(fr=frameReader->NextFrame(fr)){
                    for(int j=0 ; j<num_out ; ++j)
                        pw[j].SendVideoFrame(fr);
                    ++frame_num;
                }
            }else if(type==AVMEDIA_TYPE_AUDIO){
                for(int j=0 ; j<num_out ; ++j){
                    pw[j].SendPacket(pkt);
                }
            }
            av_packet_unref(pkt);
        }
        vfr->AddPacket(NULL);
        while(fr=frameReader->NextFrame(fr)){
            for(int j=0 ; j<num_out ; ++j)
                pw[j].SendVideoFrame(fr);
            ++frame_num;
        }
        for(int j=0 ; j<num_out ; ++j)
            pw[j].WriteEnd(); // 清除缓冲区
    }
    return true;
}