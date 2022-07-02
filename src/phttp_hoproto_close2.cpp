#include <cassert>
#include <unistd.h>
#include <phttp_server.h>

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr); \
        (type *)( (char *)__mptr - offsetof(type,member) );})

static void
after_close_tcp_monitor(uv_handle_t *_monitor)
{
  uv_tcp_monitor_t *monitor = (uv_tcp_monitor_t *)_monitor;
  http_client_socket_t *hcs = container_of(monitor, http_client_socket_t, monitor);  // TODO Fix this

  http_client_socket_deinit(hcs);
  free(hcs);
}

static void
after_real_close_imported(uv_tcp_monitor_t *monitor)
{
  int error;
  http_client_socket_t *hcs = container_of(monitor, http_client_socket_t, monitor);  // TODO Fix this
  struct global_config *gconf = (struct global_config *)monitor->super.loop->data;
  prism_switch_client_t *sw_client = gconf->sw_client;

  /*
   * Cleanup switch rule
   */
  struct psw_delete_req del_req;

  del_req.type = PSW_REQ_DELETE;
  del_req.status = 0;
  del_req.peer_addr = hcs->peername_cache.peer_addr;
  del_req.peer_port = hcs->peername_cache.peer_port;

  error = prism_switch_client_queue_task(
      sw_client, (struct psw_req_base *)&del_req, NULL, NULL);
  assert(error == 0);

  /*
   * Cleanup all client states
   */
  int evfd;
  uv_poll_stop((uv_poll_t *)monitor);
  uv_fileno((uv_handle_t *)monitor, &evfd);
  close(evfd);
  uv_close((uv_handle_t *)monitor, after_close_tcp_monitor);
}

static void
after_close_imported(uv_handle_t *_client)
{
  int error;
  http_client_socket_t *hcs = (http_client_socket_t *)_client->data;
  error = uv_tcp_monitor_wait_close(&hcs->monitor, after_real_close_imported);
  assert(error == 0);
  free(_client);
}

static void
after_close(uv_handle_t *_client)
{
  http_client_socket_t *hcs = (http_client_socket_t *)_client->data;
  uv_close((uv_handle_t *)&hcs->monitor, after_close_tcp_monitor);
  free(_client);
}

int
phttp_start_close(uv_tcp_t *client)
{
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;

  if (hcs->imported) {
    uv_close((uv_handle_t *)client, after_close_imported);
  } else {
    uv_close((uv_handle_t *)client, after_close);
  }

  return 0;
}
