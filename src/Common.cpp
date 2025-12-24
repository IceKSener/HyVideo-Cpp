#include "Common.hpp"
#include <thread>
#include <omp.h>

using namespace std;
namespace fs=filesystem;


#if _MSC_VER
char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)
#endif

void init_configs(){
    GlobalConfig.cpu_num = thread::hardware_concurrency();
    omp_set_num_threads(GlobalConfig.cpu_num);
}

void _MakeErr(const string &msg, const char *file, int line)
{
    static const char* fmt= "| 文件:%s | 行号:%d |\n发生错误: %s";
    static char strbuf[2048];
    snprintf(strbuf, sizeof(strbuf), fmt, file, line, msg.c_str());
    throw string(strbuf);
}

// 0为成功否则失败
int _assert(int rtn, const char* file, int line){
    if(rtn){ _MakeErr(av_err2str(rtn),file,line); }
    return rtn;
}
// 0或正数为成功否则失败
int _assertI(int rtn, const char* file, int line){
    if(rtn<0){ _MakeErr(av_err2str(rtn),file,line); }
    return rtn;
}
// 非NULL为成功否则失败
void* _assertP(void* p, const char* file, int line){
    _assertP((const void*)p, file, line);
    return p;
}
// 非NULL为成功否则失败
const void* _assertP(const void* p, const char* file, int line){
    static string _prefix = "分配内存失败";
    if(!p){ _MakeErr(_prefix,file,line); }
    return p;
}

string tolower(string str){
    for(int i=0 ; i<str.length() ; ++i) str[i]=tolower(str[i]);
    return str;
}
string replacechar(string str, char from, char to){
    for(int i=0 ; i<str.length() ; ++i) if(str[i]==from)str[i]=to;
    return str;
}
vector<string> strsplit(string str, const string& c){
    vector<string> results;
    if(str.empty()) return results;
    int index;
    while((index=str.find(c))>=0){
        results.push_back(str.substr(0,index));
        str=str.substr(index+c.length());
    }
    results.push_back(str);
    return results;
}
string withsuffix(const string& file, const string& suffix){
    int index=replacechar(file,'\\','/').rfind('/')+1;
    string path=file.substr(0,index), filename=file.substr(index);
    if((index=filename.rfind('.'))>=0) filename=filename.substr(0,index+1);
    return path.append(filename).append(suffix);
}
bool isfile(const fs::path& path){
    return fs::is_regular_file(path);
}
bool findConf(fs::path& out, const string &name, const fs::path& dir){
    if(!fs::is_directory(dir)) return false;
    if(isfile(dir/name)){
        out=dir/name;
        return true;
    }else if(isfile(dir/(name+".json"))){
        out=dir/(name+".json");
        return true;
    }
    for(const auto& entry: fs::recursive_directory_iterator(dir)){
        if(!entry.is_directory()) continue;
        auto& dir=entry.path();
        if(isfile(dir/name)){
            out=dir/name;
            return true;
        }else if(isfile(dir/(name+".json"))){
            out=dir/(name+".json");
            return true;
        }
    }
    return false;
}
const AVCodec* searchEncoder(const std::string &codec_name){
    const AVCodec *codec = avcodec_find_encoder_by_name(codec_name.c_str());
    if(!codec) codec = avcodec_find_decoder_by_name(codec_name.c_str());
    if(!codec) ThrowErr("找不到编码器"+codec_name);
    return avcodec_find_encoder(codec->id);
}
