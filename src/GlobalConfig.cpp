#include "GlobalConfig.hpp"

#include <csignal>

#include "utils/Logger.hpp"

_GlobalConfig _GlobalConfig::instance;
_GlobalConfig& GlobalConfig = _GlobalConfig::getInstance();

static void int_handler(int sig) {
    if (sig != SIGINT) return;
    AvLog("\n\n\n\n");
    AvLog("<<<<<<<<<<<<<<<<<<<<用户中断>>>>>>>>>>>>>>>>>>>>\n");
    AvLog("\n\n\n\n");
    GlobalConfig.interrupted = true;
}

_GlobalConfig& _GlobalConfig::setCpuNum(int cpu_num)  {
    if (cpu_num <= 0) ThrowErr("CPU线程数无效:"+std::to_string(cpu_num));
    this->cpu_num = cpu_num;
    omp_set_num_threads(cpu_num);
    return *this;
}

_GlobalConfig::_GlobalConfig() {
    cpu_num = std::thread::hardware_concurrency();
    omp_set_num_threads(cpu_num);
    signal(SIGINT, int_handler);
}