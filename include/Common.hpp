#ifndef COMMON_HPP
#define COMMON_HPP 1

#include <string>
#include <vector>
#include <fstream>
extern "C"{
#include "libavutil/avutil.h"
#include "libavcodec/avcodec.h"
}

#define AvLog(fmt_str,...) av_log(NULL,AV_LOG_INFO,fmt_str,##__VA_ARGS__)

#define ThrowErr(msg) (_MakeErr(msg,__FILE__,__LINE__))
void _MakeErr(const std::string& msg, const char* file, int line);

// 0为成功否则失败
#define Assert(rtn) (_assert(rtn,__FILE__,__LINE__))
int _assert(int rtn, const char* file, int line);

// 0或正数为成功否则失败
#define AssertI(rtn) (_assertI(rtn,__FILE__,__LINE__))
int _assertI(int rtn, const char* file, int line);

// 非NULL为成功否则失败
#define AssertP(p) (_assertP(p,__FILE__,__LINE__))
void* _assertP(void* p, const char* file, int line);

std::string tolower(std::string str);
std::vector<std::string> strsplit(std::string str, const std::string& c);
std::string withsuffix(const std::string& file, const std::string& suffix);
bool isfile(const std::string& path);
double _CalcMSE(const AVFrame *f1, const AVFrame *f2);

#endif //COMMON_HPP