#ifndef GLOBALCONFIG_HPP
#define GLOBALCONFIG_HPP 1

#include <thread>
#include <omp.h>

#include "utils/Assert.hpp"

// 全局设置
class _GlobalConfig{
public:
    int cpu_num = 1;
    int buf_sz = 4;
    bool exit_on_error = true;
    bool interrupted = false;
    _GlobalConfig& setCpuNum(int cpu_num);
    static _GlobalConfig instance;
    static _GlobalConfig& getInstance(){ return instance; }
private:
    _GlobalConfig();
    _GlobalConfig(_GlobalConfig&&) = default;
};
extern _GlobalConfig& GlobalConfig;

#endif // GLOBALCONFIG_HPP