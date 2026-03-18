#ifndef ASSERT_HPP
#define ASSERT_HPP 1

#include <string>

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
const void* _assertP(const void* p, const char* file, int line);

#endif //ASSERT_HPP