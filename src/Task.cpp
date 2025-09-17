#include "Task.hpp"

using namespace std;

InputVideo& Task::addInput(string path){
    return inputs.emplace_back(path);;
}
OutputVideo& Task::addOutput(string path, string format){
    return outputs.emplace_back(path);;
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
            datalen=scores[i].Dump();
            data=new char[datalen];
            scores[i].Dump((uint8_t*)data);
            of.write(data,datalen);
            delete[] data;
            of.close();
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