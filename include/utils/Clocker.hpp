#ifndef CLOCKER_HPP
#define CLOCKER_HPP

#ifdef DEBUG
#include <chrono>
#include <mutex>
#include <string>

// 高精度线程安全计时器

class Clocker{
public:
    // 启动index编号计时(0-99)
    inline void start(int index) {
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(mtx);
        start_time[index] = now;
        used[index] = true;
    }
    // 结束一次计时并累加总用时，并记录最后结束时间
    inline void end(int index) {
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
    // 结束后打印所有计时器用时
    ~Clocker();
private:
    // 将时间点以纳秒精度转化为字符串
    static std::string format_system_time(const std::chrono::system_clock::time_point& tp);
    std::chrono::steady_clock::time_point start_time[100];
    std::chrono::nanoseconds use_times[100];
    std::chrono::system_clock::time_point last_end_system[100];
    bool used[100] = {false};
    std::mutex mtx;
};
static Clocker clocker;

#else // DEBUG

struct Clocker{
    void start(int i){}
    void end(int i){}
};
static Clocker clocker;

#endif // DEBUG

#endif // CLOCKER_HPP