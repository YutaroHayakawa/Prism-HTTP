#pragma once

#include <uv.h>

struct uv_tcp_monitor_s;
typedef void (*uv_tcp_monitor_cb)(struct uv_tcp_monitor_s *);

typedef struct uv_tcp_monitor_s {
  uv_poll_t super;
#ifdef TCP_MONITOR_USE_CREME
  int creme_fd;
#endif
  uv_tcp_t *tcp;
  uv_tcp_monitor_cb saved_close;
} uv_tcp_monitor_t;

int uv_tcp_monitor_init(uv_loop_t *loop, uv_tcp_monitor_t *monitor,
                        uv_tcp_t *tcp);
int uv_tcp_monitor_deinit(uv_tcp_monitor_t *monitor);
/*
int uv_tcp_monitor_schedule_close(uv_tcp_monitor_t *monitor,
                                  uv_tcp_monitor_cb cb);
                                  */
int uv_tcp_monitor_wait_close(uv_tcp_monitor_t *monitor,
                                  uv_tcp_monitor_cb cb);
