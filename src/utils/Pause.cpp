#include "utils/Pause.hpp"

#ifdef WIN32
#include <windows.h>
#include <conio.h>
#else // WIN32
#include <unistd.h>
#endif // WIN32

#include "GlobalConfig.hpp"

void HvSleep(double time_s) {
#ifdef WIN32
    Sleep(time_s*1000);
#else // WIN32
    usleep(time_s*1000000);
#endif // WIN32
}

static int _read_key() {
#ifdef WIN32
    if (_kbhit()) {
        return _getch();
    }
#else // WIN32
    // 以下代码来自ffmpeg的reak_key函数
    int n = 1;
    timeval tv;
    fd_set rfds;

    FD_ZERO(&rfds);
    FD_SET(0, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    n = select(1, &rfds, NULL, NULL, &tv);
    if (n > 0) {
        unsigned char ch;
        n = read(0, &ch, 1);
        if (n == 1)
            return ch;

        return n;
    }
#endif // WIN32
    return -1;
}

int read_key() {
    int key = _read_key();      //获取首个字符
    while (_read_key() != -1);  //后续清空
    return key;
}

bool isKeyPressed(char x) {
    int key = read_key();
    // 确保大小写适配
    return key!=-1 && tolower(x)==tolower(key);
}

void waitForKey(char key) {
    // TODO 使用getchar代替？
    while (!GlobalConfig.interrupted && tolower(read_key())!=tolower(key)) {
        HvSleep(0.3);
    }
}