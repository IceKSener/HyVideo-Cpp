#ifndef LOGGER_HPP
#define LOGGER_HPP 1

extern "C" {
    #include "libavutil/avutil.h"
}

#ifdef AVUTIL_LOG_H
#define AvLog(fmt_str,...) av_log(NULL,AV_LOG_INFO,fmt_str,##__VA_ARGS__)
#else // AVUTIL_LOG_H
#include <cstdio>
#define AvLog(fmt_str,...) fprintf(stderr,fmt_str,##__VA_ARGS__)
#endif // AVUTIL_LOG_H

#endif //LOGGER_HPP