#include "utils/Clocker.hpp"

#ifdef DEBUG

#include <sstream>
#include <iomanip>

using namespace std;
using namespace chrono;

string Clocker::format_system_time(const system_clock::time_point &tp) {
    time_t s = system_clock::to_time_t(tp);
    tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &s);
#else
    localtime_r(&s, &tm);
#endif
    ostringstream oss;
    oss << put_time(&tm, "%Y-%m-%d %H:%M:%S");
    // 秒级以下
    auto since_epoch = tp.time_since_epoch();
    auto secs = duration_cast<seconds>(since_epoch);
    auto subsecs = duration_cast<nanoseconds>(since_epoch - secs).count();
    // 将精度填到纳秒级别
    oss << "." << setw(9) << setfill('0') << subsecs;
    return oss.str();
}

Clocker::~Clocker() {
    lock_guard<mutex> lk(mtx);
    for(int i = 0; i < 100; ++i){
        if(!used[i]) continue;
        long long ns = use_times[i].count();
        double seconds = ns / 1e9;
        string timestr = "-";
        if (last_end_system[i].time_since_epoch().count() != 0){
            timestr = format_system_time(last_end_system[i]);
        }
        // print index, total nanoseconds, seconds (double), and last end wall-clock timestamp
        fprintf(stderr, "clocker[%d] total: %lld ns (%.6f s), last_end: %s\n", i, ns, seconds, timestr.c_str());
    }
}

    #endif // DEBUG