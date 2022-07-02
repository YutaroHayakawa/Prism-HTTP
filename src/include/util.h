#pragma once

#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <uv.h>

#ifdef PHTTP_DEBUG
#define DEBUG(_fmt, ...)                                                       \
  do {                                                                         \
    struct timeval _t0;                                                        \
    gettimeofday(&_t0, NULL);                                                  \
    fprintf(stderr, "%03d.%06d %s [%d] " _fmt, (int)(_t0.tv_sec % 1000),       \
            (int)_t0.tv_usec, __FUNCTION__, __LINE__, ##__VA_ARGS__);          \
  } while (0)
#else
#define DEBUG(_fmt, ...)
#endif

static void
uv_perror(const char *msg, int error)
{
  fprintf(stderr, "%s: %s\n", msg, uv_strerror(error));
}
