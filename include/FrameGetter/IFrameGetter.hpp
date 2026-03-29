#ifndef IFRAMEGETTER_HPP
#define IFRAMEGETTER_HPP 1

#include "data/HvFrame.hpp"


class IFreamGetter{
public:
    // 获取下一帧，如果到达结尾或需要输入新的packet，则返回false
    virtual bool nextFrame(HvFrame& fr) = 0;
    // 获取下一帧为null时使用IsEnd查看是否结束
    virtual bool isEnd() = 0;
};

#endif //IFRAMEGETTER_HPP