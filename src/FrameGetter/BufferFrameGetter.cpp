#include "FrameGetter/BufferFrameGetter.hpp"

#include "GlobalConfig.hpp"

using namespace std;

BufferFrameGetter::BufferFrameGetter(const shared_ptr<IFreamGetter>& getter)
    : getter(getter)
{
    buf_frs.resize(GlobalConfig.buf_sz);
}

bool BufferFrameGetter::nextFrame(HvFrame fr){
    return false;
}

bool BufferFrameGetter::isEnd(){
    return false;
}
