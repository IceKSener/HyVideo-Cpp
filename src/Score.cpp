#include "Score.hpp"

using namespace std;

Score::Score(const std::vector<double>& scores, ScoreType type):scores(scores),type(type){ }
Score Score::LoadScob(std::string path){
    ifstream infile(path,ios::binary);
    if(!infile.is_open()) throw string("无法打开文件: ")+path;
    uint32_t magic;
    infile.read((char*)&magic, 4);
    if(magic!=MAGIC.i) throw string("非ScoreBin文件: ")+path;

    vector<double> s;
    Score score(s);
    uint8_t klen;
    char buf[256];
    string key;
    uint32_t datasize;
    while(!infile.read((char*)&klen,1).eof()){
        infile.read(buf,klen);
        key=string(buf,0,klen);
        infile.read((char*)&datasize, 4);

        if(key=="type"){
            uint32_t typelen=min(datasize,(uint32_t)sizeof(buf));
            infile.read(buf, typelen);
            string type(buf,0,typelen);
            if(type=="mse") score.type=ScoreType::MSE;
            else if(type=="ssim") score.type=ScoreType::SSIM;
            infile.seekg(datasize-typelen, ios::cur);
        }
        else if(key=="fps"){
            infile.read((char*)&score.fps, sizeof(double));
        }
        else if(key=="scores"){
            score.scores.resize(datasize/sizeof(double));
            infile.read((char*)score.scores.data(), datasize);
        }
        else if(key=="STATIC"){
            score.Static.enable=true;
            infile.read((char*)&score.Static.value, sizeof(double));
        }
        else if(key=="CUT"){
            score.Cut.enable=true;
            infile.read((char*)&score.Cut.value, sizeof(double));
        }
        else infile.seekg(datasize, ios::cur);
    }
    return score;
}

inline uint8_t* _write(uint8_t* output, const char *key, const void *data, uint32_t datasize){
    uint8_t klen=strlen(key);
    *output=klen;   output+=1;
    memcpy(output,key,klen);  output+=klen;
    *(uint32_t*)output=datasize; output+=4;
    memcpy(output,data,datasize);
    return output+datasize;
};
int Score::Dump(uint8_t* output){
    uint32_t typelen = typestr.at(type).length();
    uint32_t scoresize = scores.size()*sizeof(double);
    uint32_t StaticSize = Static.enable?11+sizeof(double):0;
    uint32_t CutSize = Cut.enable?8+sizeof(double):0;
    int size=4+                 //MAGIC size 4
        9+typelen+              //\x04type\(typename_size[4B])(type name)
        16+                     //\x03fps\x00000008(STATIC_VALUE)
        11+scoresize+           //\x06scores\(scores_size[4B])(scores)
        StaticSize+             //{\x06STATIC\x00000008(STATIC_VALUE)}
        CutSize                 //{\x03CUT\x00000008(CUT_VALUE)}
    ;
    if(output){
        *(uint32_t*)output=MAGIC.i; output+=4;
        output = _write(output,"type",typestr.at(type).c_str(),typelen);
        output = _write(output,"fps",&fps,sizeof(double));
        output = _write(output,"scores",scores.data(),scoresize);
        if(StaticSize) output = _write(output,"STATIC",&Static.value,sizeof(double));
        if(CutSize) output = _write(output,"CUT",&Cut.value,sizeof(double));
    }
    return size;
}