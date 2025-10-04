#include "Task.hpp"
#include "Common.hpp"
#include "RifeFrameGetter.hpp"
#include "VideoFrameReader.hpp"
#include "OutputVideo.hpp"
#include "nlohmann\json.hpp"
#include <stack>
#include <fstream>
#include <filesystem>

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
        // AvLog("%s",conf["_name"].c_str());  //DEBUG
        AvLog("%s",conf.dump(2).c_str());  //DEBUG
        try{
            if(conf["type"]!="out") throw "配置"+(string)conf["_name"]+"不是输出配置";
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

bool Task::_taskTranscode(){
    //TODO 设置处理参数
    struct{
        struct _upscale{
            bool use_gpu=false;
            bool process=true;
            string engine="real-cugan";
            string model="models-nose";
            int upscale=2;
            int denoise=0;
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

    //设置输出目标配置
    vector<_target> targets = ReadTargets(getStr(args,"conf_out"));
    if(targets.empty()) ThrowErr("请指定视频输出配置");

    string _buf;
    //TODO 读取处理配置
    if(!(_buf=getStr(args,"conf_in")).empty()){
        vector<string> confs=strsplit(_buf,";");
        map<string,bool> conf_open;
        stack<string> conf_toread;
        for(auto it=confs.rbegin() ; it!=confs.rend() ; ++it)
            conf_toread.push(*it);
        while(!conf_toread.empty()){
            string conf=conf_toread.top();
            conf_toread.pop();
            if(conf_open[conf]) continue;
            AvLog("输入配置:%s\n",conf.c_str());  //DEBUG
            json cfg=ReadConf(conf);
            AvLog("%s",cfg.dump(2).c_str());  //DEBUG
            conf_open[conf]=true;
            try{
                if(cfg["type"]!="in") throw "配置"+conf+"不是输入配置";
                if(cfg["import"].is_array()){
                    auto& imports=cfg["import"];
                    for(auto it=imports.rbegin(); it!=imports.rend() ; ++it)
                        conf_toread.push(*it);
                }
                //TODO 读取处理配置
                if(cfg["upscale"]["enable"]==true){
                    AvLog("打开超分辨率\n");  //DEBUG
                    ThrowErr("暂不支持");
                    auto& pro_v=processes.upscale.emplace();
                    auto& json_v=cfg["upscale"];
                    if(json_v["engine"].is_string())    pro_v.engine  =json_v["engine"];
                    if(json_v["use_gpu"].is_boolean())  pro_v.use_gpu =json_v["use_gpu"];
                    if(json_v["model"].is_string())     pro_v.model   =json_v["engine"];
                    if(json_v["upscale"].is_number_integer())   pro_v.upscale =json_v["upscale"];
                    if(json_v["denoise"].is_number_integer())   pro_v.denoise =json_v["denoise"];
                }
                if(cfg["interpolation"]["enable"]==true){
                    AvLog("打开补帧\n");  //DEBUG
                    ThrowErr("暂不支持");
                    auto& pro_v=processes.interpolation.emplace();
                    auto& json_v=cfg["interpolation"];
                    pro_v.engine=tolower((string)json_v["engine"]);
                    pro_v.process=json_v.value("process", true);
                    pro_v.target_fps=json_v.value("target_fps", 60);
                    if(pro_v.engine=="rife"){
                        RifeArgs *args=new RifeArgs;
                        args->use_gpu=json_v.value("use_gpu", true);
                        args->model=json_v.value("model", "rife-v4.22-lite");
                    }
                }
            }catch(string err){
                ThrowErr(err);
            }catch(exception err){
                ThrowErr(err.what());
            }
        }
    }

    // 构建输出目标并执行
    for(int i=0; i<inputs.size() ; ++i){
        auto& vd_in=inputs[i];

        IFreamGetter *frameReader= new VideoFrameReader(vd_in);
        //TODO 添加帧处理
        if(processes.upscale){
            ;
        }
        if(processes.interpolation){
            ;
        }

        //TODO 构造输出视频
        map<fs::path,bool> exist_files;
        vector<OutputVideo> outputs;
        for(auto& target:targets){
            // 计算输出路径
            fs::path path=vd_in.path;
            path.replace_extension(target.ext);
            {
                string originName=path.filename().string();
                for(int i=0; fs::exists(path)||exist_files[path] ; ++i){
                    path.replace_filename(originName+"-"+to_string(i));
                }
                exist_files[path]=true;
            }

            // 生成输出视频文件
            auto& vd_out=outputs.emplace_back(path.string());
            vd_out.setEncoder((const AVCodec*)AssertP(avcodec_find_encoder_by_name(target.coder.c_str())));
        }
        for(int j=0; j<targets.size(); ++j){
            ;//TODO ConfigOutput WriteOutput
        }
    }
    return true;
}