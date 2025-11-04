#ifndef COMMON_HPP
#define COMMON_HPP 1

#include <cstdint>
extern "C"{
    #include "libavutil/avutil.h"
    #include "libavcodec/avcodec.h"
}
#include <vector>
#include <filesystem>

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
const void* _assertP(const void* p, const char* file, int line);

std::string tolower(std::string str);
std::vector<std::string> strsplit(std::string str, const std::string& c);
std::string withsuffix(const std::string& file, const std::string& suffix);
bool isfile(const std::filesystem::path& path);
bool findConf(std::filesystem::path& out, const std::string& name,const std::filesystem::path& dir);
const AVCodec* searchEncoder(const std::string& codec_name);

#ifdef DEBUG
// high-resolution and thread-safe clocker
#include <chrono>
#include <mutex>
#include <string>
#include <sstream>
#include <iomanip>

class Clocker{
    std::chrono::steady_clock::time_point start_time[100];
    std::chrono::nanoseconds use_times[100];
    std::chrono::system_clock::time_point last_end_system[100];
    bool used[100] = {false};
    std::mutex mtx;
public:
    // start timing for index
    inline void start(int index){
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(mtx);
        start_time[index] = now;
        used[index] = true;
    }

    // end timing for index and accumulate duration; also record wall-clock timestamp
    inline void end(int index){
        auto now_steady = std::chrono::steady_clock::now();
        auto now_system = std::chrono::system_clock::now();
        std::lock_guard<std::mutex> lk(mtx);
        if (!used[index]){
            // if end called without start, mark as used and ignore
            used[index] = true;
            last_end_system[index] = now_system;
            return;
        }
        auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(now_steady - start_time[index]);
        use_times[index] += delta;
        last_end_system[index] = now_system;
    }

    // format system_clock time_point with subsecond precision
    static std::string format_system_time(const std::chrono::system_clock::time_point& tp){
        using namespace std::chrono;
        auto s = time_t(system_clock::to_time_t(tp));
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &s);
#else
        localtime_r(&s, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        // subseconds
        auto since_epoch = tp.time_since_epoch();
        auto secs = duration_cast<seconds>(since_epoch);
        auto subsecs = duration_cast<nanoseconds>(since_epoch - secs).count();
        // append fractional seconds up to nanoseconds
        oss << "." << std::setw(9) << std::setfill('0') << subsecs;
        return oss.str();
    }

    // print summary on destruction
    ~Clocker(){
        std::lock_guard<std::mutex> lk(mtx);
        for(int i = 0; i < 100; ++i){
            if(!used[i]) continue;
            long long ns = use_times[i].count();
            double seconds = ns / 1e9;
            std::string timestr = "-";
            if (last_end_system[i].time_since_epoch().count() != 0){
                timestr = format_system_time(last_end_system[i]);
            }
            // print index, total nanoseconds, seconds (double), and last end wall-clock timestamp
            AvLog("clocker[%d] total: %lld ns (%.6f s), last_end: %s\n", i, ns, seconds, timestr.c_str());
        }
    }
};
static Clocker clocker;
#else
struct Clocker{
    void start(int index){}
    void end(int index){}
} clocker;
#endif

#endif //COMMON_HPP