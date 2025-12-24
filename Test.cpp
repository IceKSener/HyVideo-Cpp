#include "net.h"
#include <iostream>

using namespace std;
using namespace ncnn;

#define WIDTH 200
#define HEIGHT 300

int main() {
    Mat out;
    VkMat testv;

    if (create_gpu_instance() != 0) {
        cerr << "Failed to create GPU instance!" << endl;
        return -1;
    }

    auto dev = get_gpu_device();
    if (!dev) {
        cerr << "Failed to get GPU device!" << endl;
        destroy_gpu_instance();
        return -1;
    }

    auto ba = dev->acquire_blob_allocator();
    auto sa = dev->acquire_staging_allocator();
    cout<<sa->mappable<<endl;

    if (!ba || !sa) {
        cerr << "Failed to acquire allocators!" << endl;
        destroy_gpu_instance();
        return -1;
    }

    Option opt;
    opt.use_vulkan_compute = true;
    opt.blob_vkallocator = ba;
    opt.workspace_vkallocator = ba;
    opt.staging_vkallocator = sa;
    opt.use_fp16_storage = false;
    opt.use_fp16_packed = false;

    // 使用两个独立的命令队列
    VkCompute cmd_upload(dev);
    VkCompute cmd_download(dev);

    {
        Mat test(WIDTH, HEIGHT, 3);
        test.fill(114514.f);
        cmd_upload.record_upload(test, testv, opt);
    }
    // 上传
    cmd_upload.submit_and_wait();

    // 下载
    cmd_download.record_download(testv, out, opt);
    cmd_download.submit_and_wait();

    cout << "First pixel value: " << *(float*)out.data << endl;

    // 回收资源
    dev->reclaim_blob_allocator(ba);
    dev->reclaim_staging_allocator(sa);
    destroy_gpu_instance();

    return 0;
}

// #include "Common.hpp"
// #include "VideoFrameReader.hpp"
// #include <iostream>

// using namespace std;

// int main(){
//     InputVideo v("W:/test.vo");
//     VideoFrameReader r(v);
//     AVFrame *fr = av_frame_alloc();

//     int x = 0, y = 0;
//     AvLog("输入x，y坐标:");
//     cin>>x>>y;
//     const int offset = y*v.getVS()->codecpar->width;
//     int num=0;
//     while(r.NextFrame(fr)){
//         if(fr->format!=AV_PIX_FMT_YUV420P){
//             cout<<"Not YUV!"<<endl;
//             return -1;
//         }
//         if(((num++)&3)==3) AvLog("%02X\n", fr->data[0][offset]);
//         else AvLog("%02X ", fr->data[0][offset]);
//     }
//     av_frame_free(&fr);
    
// }