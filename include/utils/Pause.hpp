#ifndef PAUSE_HPP
#define PAUSE_HPP 1

// 进程暂停time_s秒
void HvSleep(double time_s);
// 获取首个键盘输入，后续内容清空，无输入返回 -1
int read_key();
// 判断字母x是否被按下
bool isKeyPressed(char x);
// 等待按下字母x
void waitForKey(char x);

#endif //PAUSE_HPP