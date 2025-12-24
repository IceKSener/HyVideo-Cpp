#include "FrameGetter/BufferFrameGetter.hpp"
#include "Common.hpp"

using namespace std;

BufferFrameGetter::BufferFrameGetter(const shared_ptr<IFreamGetter>& getter):getter(getter){
    buf_frs.resize(GlobalConfig.buf_sz);
    for(int i=0 ; i<buf_frs.size() ; ++i)
        AssertP(buf_frs[i]=av_frame_alloc());
}

BufferFrameGetter::~BufferFrameGetter(){
    for(int i=0 ; i<buf_frs.size() ; ++i)
        av_frame_free(&buf_frs[i]);
}

AVFrame* BufferFrameGetter::NextFrame(AVFrame *fr){
    return nullptr;
}

bool BufferFrameGetter::IsEnd(){
    return false;
}
