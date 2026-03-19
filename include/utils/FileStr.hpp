#ifndef FILESTR_HPP
#define FILESTR_HPP 1

#include <string>
#include <vector>
#include <filesystem>

extern "C" {
    #include "libavutil/rational.h"
}

// 将字符串转为小写
std::string tolower(std::string str);
// 将字符串分割
std::vector<std::string> strsplit(std::string str, const std::string& c);
// 替换后缀名
std::string withsuffix(const std::string& file, const std::string& suffix);
// 根据配置名找到配置文件路径
bool findConf(std::filesystem::path& out, const std::string& name,const std::filesystem::path& dir);
// 根据pts、timebase计算时间字符串
std::string getTimeStr(int64_t pts, AVRational timebase);

#endif // FILESTR_HPP