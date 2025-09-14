#include "Task.hpp"

using namespace std;

Video& Task::addInput(string path){
    return inputs.emplace_back(path);;
}
Video& Task::addOutput(string path, string format){
    Video& vd=outputs.emplace_back(path);
    return vd;
}

vector<Score> Task::CalcScores(ScoreType type){
    vector<Score> scores;
    for(Video& vd:inputs){
        switch(type){
        case ScoreType::MSE:
            scores.push_back(CalcMSE(vd).setFPS(av_q2d(vd.fps)));
            break;
        default:
            throw string("未知指数类型");
            break;
        }
    }
    return scores;
}
Score Task::CalcMSE(Video& vd){
    vector<double> result;
    result.push_back(0);
    VideoFrameReader vfr(vd);
    AVFrame *f1, *f2, *tmp;
    AssertP(f1=av_frame_alloc());
    AssertP(f2=av_frame_alloc());
    if(!vfr.NextFrame(f1)) throw (string)"无法读取视频帧";
    while(vfr.NextFrame(f2)){
        result.push_back(_CalcMSE(f1,f2));
        av_frame_unref(f1);
        tmp=f1, f1=f2, f2=tmp;
    }
    av_frame_free(&f1);
    av_frame_free(&f2);
    return Score(result, ScoreType::MSE);
}