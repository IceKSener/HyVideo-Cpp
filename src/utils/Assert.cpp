#include "utils/Assert.hpp"

#include <sstream>
#include <stdint.h>

extern "C"{
#include "libavutil/avutil.h"
}

using namespace std;

void _MakeErr(const string &msg, const char *file, int line) {
#ifndef DEBUG
    file = strstr(file, "src\\");
#endif //DEBUG
    ostringstream oss;
    oss << "========================================\n\n";
    oss << "| 位置: " << file << '(' << line << ") |\n| 错误: "<< msg;
    oss << "\n\n========================================";
    throw oss.str();
}

// 0为成功否则失败
int _assert(int rtn, const char* file, int line) {
    if (rtn) {
        char err_msg[AV_ERROR_MAX_STRING_SIZE];
        _MakeErr(av_make_error_string(err_msg, sizeof(err_msg), rtn), file, line);
    }
    return rtn;
}
// 0或正数为成功否则失败
int _assertI(int rtn, const char* file, int line) {
    if (rtn < 0) {
        char err_msg[AV_ERROR_MAX_STRING_SIZE];
        _MakeErr(av_make_error_string(err_msg, sizeof(err_msg), rtn), file, line);
    }
    return rtn;
}
// 非NULL为成功否则失败
void* _assertP(void* p, const char* file, int line) {
    _assertP((const void*)p, file, line);
    return p;
}
// 非NULL为成功否则失败
const void* _assertP(const void* p, const char* file, int line) {
    static string _prefix = "分配内存失败";
    if(!p){ _MakeErr(_prefix,file,line); }
    return p;
}