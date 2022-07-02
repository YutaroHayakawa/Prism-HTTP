#include <cstdio>
#include <errno.h>
#include <linux/tcp.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>

#include <uv_tcp_monitor.h>

int
uv_tcp_monitor_init(uv_loop_t *loop, uv_tcp_monitor_t *monitor, uv_tcp_t *tcp)
{
  int evfd, error, sock;

  if (loop == NULL || monitor == NULL || tcp == NULL) {
    return -EINVAL;
  }

  uv_fileno((uv_handle_t *)tcp, &sock);

  evfd = eventfd(0, 0);
  if (evfd == -1) {
    return -errno;
  }

#ifdef TCP_MONITOR_USE_CREME
  /*
   * Use creme (https://github.com/micchie/creme) to
   * detect socket destruction. Only requires kernel
   * module.
   */
  int creme_fd;
  uint64_t val = ((uint64_t)evfd << 32) | sock;

  creme_fd = open("/dev/creme", O_RDWR);
  if (creme_fd == -1) {
    error = -errno;
    goto err0;
  }

  error = ioctl(creme_fd, 0, (unsigned long)&val, sizeof(val));
  if (error == -1) {
    error = -errno;
    goto err1;
  }

  monitor->creme_fd = creme_fd;
#else
  /*
   * Use TCP_MONITOR_SET_EVENTFD setsockopt. Needs kernel
   * modification.
   */
  error = setsockopt(sock, IPPROTO_TCP, TCP_MONITOR_SET_EVENTFD, &evfd,
                     sizeof(int));
  if (error) {
    error = -errno;
    goto err0;
  }
#endif

  error = uv_poll_init(loop, (uv_poll_t *)monitor, evfd);
  if (error) {
    goto err0;
  }

  monitor->tcp = tcp;

  return 0;

#ifdef TCP_MONITOR_USE_CREME
err1:
  close(creme_fd);
#endif
err0:
  close(evfd);
  return error;
}

static void
uv_tcp_monitor_on_tcp_close(uv_poll_t *handle, int status, int events)
{
  int fd;

  if (status < 0) {
    fprintf(stderr, "%s", uv_strerror(status));
  }

  if (!(events & UV_READABLE)) {
    fprintf(stderr, "Unexpected event %d detected\n", events);
  }

  uv_fileno((uv_handle_t *)handle, &fd);

  uint64_t counter;
  ssize_t rsize;
  rsize = read(fd, &counter, sizeof(counter));
  assert(rsize == sizeof(counter));
  if (counter != 1) {
    printf("couter == %lu\n", counter);
    assert(counter == 1);
  }

  uv_poll_stop(handle);

  uv_tcp_monitor_t *monitor = (uv_tcp_monitor_t *)handle;
#ifdef TCP_MONITOR_USE_CREME
  close(monitor->creme_fd);
#endif
  monitor->saved_close(monitor);
}

/*
int
uv_tcp_monitor_schedule_close(uv_tcp_monitor_t *monitor, uv_tcp_monitor_cb cb)
{
  int error;

  if (monitor == NULL) {
    return -EINVAL;
  }

  error = uv_poll_start((uv_poll_t *)monitor, UV_READABLE,
                        uv_tcp_monitor_on_tcp_close);
  assert(error == 0);

  monitor->saved_close = cb;
  uv_close((uv_handle_t *)monitor->tcp, (uv_close_cb)free);
  uv_close((uv_handle_t *)monitor->tcp, NULL);

  return 0;
}
*/

int
uv_tcp_monitor_wait_close(uv_tcp_monitor_t *monitor, uv_tcp_monitor_cb cb)
{
  monitor->saved_close = cb;
  return uv_poll_start((uv_poll_t *)monitor, UV_READABLE,
                        uv_tcp_monitor_on_tcp_close);
}
