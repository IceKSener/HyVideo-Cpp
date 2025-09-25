#include "Task.hpp"

using namespace std;
using json=nlohmann::json;
namespace fs=filesystem;

InputVideo& Task::addInput(string path){
    return inputs.emplace_back(path);;
}
OutputVideo& Task::addOutput(string path, string format){
    return outputs.emplace_back(path);;
}

json Task::ReadConf(const string& conf, const string& type){
    fs::path path;
    if(!findConf(path, conf, conf_dir+type)) ThrowErr("未找到配置["+conf+"]");
    json config;
    ifstream f(path);
    f>>config;
    f.close();
    return config;
}
string Task::getStr(TaskArgs args, const string key, const string& def){
    return args[key].value_or(def);
}
int Task::getInt(TaskArgs args, const string key, int def){
    if(args[key].has_value()) return atoi(args[key].value().c_str());
    return def;
}
double Task::getReal(TaskArgs args, const string key, double def){
    if(args[key].has_value()) return atof(args[key].value().c_str());
    return def;
}

bool Task::Run(){
    if(type==CALC_SCORES){
        string score_type=args["type"].value_or("mse");
        vector<Score> scores;
        if(score_type=="mse") scores=CalcScores(ScoreType::MSE);
        else ThrowErr("未知指数类型");
        bool coverAll=false;
        char *data=nullptr;
        int datalen;
        ofstream of;
        for(int i=0; i<inputs.size() ; ++i){
            string path=withsuffix(inputs[i].path,"scob");
            if(isfile(path)&&!coverAll){
                AvLog("\"%s\"已存在，是否覆盖[y/a(全部覆盖)/n]:",path.c_str());
                fflush(stdin);
                switch(tolower(getchar())){
                case 'a': coverAll=true;
                case 'y': break;
                case 'n':
                default: continue;;
                }
            }
            of.open(path,ios::binary);
            if(args["static"].has_value()) scores[i].setStatic(getReal(args,"static"));
            if(args["cut"].has_value()) scores[i].setStatic(getReal(args,"cut"));
            datalen=scores[i].Dump();
            data=new char[datalen];
            scores[i].Dump((uint8_t*)data);
            of.write(data,datalen);
            delete[] data;
            of.close();
        }
    }else if(type==TRANSCODE){
        struct{
            struct _upscale{
                bool use_gpu=false;
                bool process=true;
                string engine="rife";
                string model="rife-v4.22-lite";
                double target_fps=60;
            };
            struct _interpolation{
                bool use_gpu=false;
                bool process=true;
                string engine="real-cugan";
                string model="models-nose";
                int upscale=2;
                int denoise=0;
            };
            optional<_upscale> upscale;
            optional<_interpolation> interpolation;
        }processes;
        struct _target{
            string ext, format, coder, pix_fmt;
            int maxw,maxh,crf;
            bool faststart;
            optional<double> size_limit;
        };
        vector<_target> targets;
        string _buf;
        if(!(_buf=getStr(args,"conf_in")).empty())
            for(auto& conf:strsplit(_buf,";")){
                AvLog("输入配置:%s\n",conf.c_str());
                json cfg=ReadConf(conf,"in");
                AvLog("%s",cfg.dump(2).c_str());
                try{
                    if(cfg["type"]!="in") throw "配置"+conf+"不是输入配置";
                    if(cfg["upscale"]["enable"]==true){
                        AvLog("打开超分辨率\n");
                    }
                    if(cfg["interpolation"]["enable"]==true){
                        AvLog("打开补帧\n");
                    }
                }catch(string err){
                    ThrowErr(err);
                }catch(exception err){
                    ThrowErr(err.what());
                }
            }
        if(!(_buf=getStr(args,"conf_out")).empty())
            for(auto& conf:strsplit(_buf,";")){
                AvLog("输入配置:%s\n",conf.c_str());
            }
        for(int i=0; i<inputs.size() ; ++i){
            ;//TODO ReadFrame ProcessFrames
            for(int j=0; j<targets.size(); ++j){
                ;//TODO ConfigOutput WriteOutput
            }
        }
    }
    return true;
}

vector<Score> Task::CalcScores(ScoreType type){
    vector<Score> scores;
    for(InputVideo& vd:inputs){
        switch(type){
        case ScoreType::MSE:
            scores.push_back(CalcMSE(vd).setFPS(av_q2d(vd.fps)));
            break;
        default:
            ThrowErr("未知指数类型");
            break;
        }
    }
    return scores;
}
Score Task::CalcMSE(InputVideo& vd){
    vector<double> result;
    result.push_back(0);
    VideoFrameReader vfr(vd);
    AVFrame *f1, *f2, *tmp;
    AssertP(f1=av_frame_alloc());
    AssertP(f2=av_frame_alloc());
    if(!vfr.NextFrame(f1)) ThrowErr("无法读取视频帧");
    while(vfr.NextFrame(f2)){
        result.push_back(_CalcMSE(f1,f2));
        av_frame_unref(f1);
        tmp=f1, f1=f2, f2=tmp;
    }
    av_frame_free(&f1);
    av_frame_free(&f2);
    return Score(result, ScoreType::MSE);
}