#ifndef BUFFERFRAMEGETTER

#include "IFrameGetter.hpp"
extern "C"{
    #include "libavutil/avutil.h"
}
#include <vector>
#include <mutex>
#include <thread>
#include <memory>

class BufferFrameGetter: public IFreamGetter{
private:
    std::thread read_thread;
    std::mutex mux;
    std::vector<HvFrame> buf_frs;
    int fr_pos = 0;
    bool need_packet = false;
    std::shared_ptr<IFreamGetter> getter;
public:
    BufferFrameGetter(const std::shared_ptr<IFreamGetter>& getter);
    BufferFrameGetter(BufferFrameGetter&&) = default;

    bool nextFrame(HvFrame fr);
    bool isEnd();
};

#endif