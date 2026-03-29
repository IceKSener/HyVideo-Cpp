#include "data/Score.hpp"

#include <fstream>
#include <cstring>

#include "utils/Assert.hpp"

using namespace std;

#define MSE_STATIC_DEF 10
#define MSE_CUT_DEF 1000
#define SSIM_STATIC_DEF 0.995
#define SSIM_CUT_DEF -0.2

Score Score::LoadScob(std::string path) {
    /**
     * 结构：
     * [4B:MAGIC]
     * [1B:字段名长][?B:字段名][4B:数据长][?B:数据]
     * ...
     */
    ifstream infile(path,ios::binary);
    if (!infile.is_open()) ThrowErr("无法打开文件: "+path);
    uint32_t magic;
    infile.read((char*)&magic, 4);
    if (magic != MAGIC.i) ThrowErr("非ScoreBin文件: "+path);

    Score score(vector<double>(0));
    uint8_t klen;
    char buf[256];
    string key;
    uint32_t datasize;
    while (!infile.read((char*)&klen,1).eof()) {
        infile.read(buf, klen);
        key = string(buf, 0, klen);
        infile.read((char*)&datasize, 4);

        if (key == "type") {
            uint32_t typelen = min(datasize,(uint32_t)sizeof(buf));
            infile.read(buf, typelen);
            string type(buf, 0, typelen);
            if (type == "mse") score.type = ScoreType::MSE;
            else if (type == "ssim") score.type = ScoreType::SSIM;
            infile.seekg(datasize-typelen, ios::cur);
        } else if (key == "fps") {
            infile.read((char*)&score.fps, sizeof(double));
            infile.seekg(datasize-sizeof(double), ios::cur);
        } else if (key == "scores") {
            score.scores.resize(datasize/sizeof(double));
            infile.read((char*)score.scores.data(), score.scores.size()*sizeof(double));
            infile.seekg(datasize%sizeof(double), ios::cur);
        } else if (key == "STATIC") {
            score.Static.emplace();
            infile.read((char*)&score.Static.value(), sizeof(double));
            infile.seekg(datasize-sizeof(double), ios::cur);
        } else if (key == "CUT") {
            score.Cut.emplace();
            infile.read((char*)&score.Cut.value(), sizeof(double));
            infile.seekg(datasize-sizeof(double), ios::cur);
        } else infile.seekg(datasize, ios::cur);
    }
    return score;
}

static uint8_t* _write(uint8_t* output, const char *key, const void *data, uint32_t datasize) {
    uint8_t klen = strlen(key);
    *output++ = klen;
    memcpy(output, key, klen);
    output += klen;
    *(uint32_t*)output = datasize;
    output += sizeof(uint32_t);
    memcpy(output, data, datasize);
    return output + datasize;
};

vector<int8_t> Score::CalcProcess() const{
    int len = scores.size();
    vector<int8_t> _process(len, 0);
    if (len <= 1) return _process;
    int8_t *process = _process.data();
    const double *score = scores.data();
    double newScore[3];
    newScore[0] = score[0];
    newScore[1] = score[1];
    #define pS (newScore[i%3])
    #define cS (newScore[(i+1)%3])
    #define nS (newScore[(i+2)%3])
    if (type == MSE) {
        double STATIC = Static.value_or(MSE_STATIC_DEF);
        double CUT = Cut.value_or(MSE_CUT_DEF);
        int i = 0;
        for(; i<len-2 ; ++i){
            nS = score[i+2];
            if (cS<=STATIC && pS>STATIC && nS>STATIC) {
                cS=pS; process[i+1]=-1;// 静帧
            } else if (cS-pS > CUT) process[i+1]=1;// 转场
        }
        if (cS-pS > CUT)
            process[i+1] = 1;// 转场
    }
    #undef pS
    #undef cS
    #undef nS
    return _process;
}

int Score::Dump(uint8_t *output) const{
    uint32_t typelen = typestr.at(type).length();
    uint32_t scoresize = scores.size() * sizeof(double);
    uint32_t StaticSize = Static ? 11+sizeof(double) : 0;
    uint32_t CutSize = Cut ? 8+sizeof(double) : 0;
    int size = 4 +              //MAGIC size 4
        9 + typelen +           //\x04type\(typename_size[4B])(type name)
        16 +                    //\x03fps\x00000008(STATIC_VALUE)
        11 + scoresize +        //\x06scores\(scores_size[4B])(scores)
        StaticSize +            //{\x06STATIC\x00000008(STATIC_VALUE)}
        CutSize                 //{\x03CUT\x00000008(CUT_VALUE)}
    ;
    if (output) {
        *(uint32_t*)output=MAGIC.i; output+=4;
        output = _write(output,"type",typestr.at(type).c_str(),typelen);
        output = _write(output,"fps",&fps,sizeof(double));
        output = _write(output,"scores",scores.data(),scoresize);
        if(StaticSize) output = _write(output,"STATIC",&Static.value(),sizeof(double));
        if(CutSize) output = _write(output,"CUT",&Cut.value(),sizeof(double));
    }
    return size;
}