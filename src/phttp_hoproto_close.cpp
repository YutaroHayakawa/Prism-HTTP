#include <cassert>
#include <unistd.h>
#include <phttp_server.h>

static void
after_delete_switch_rule(uv_work_t *_work, int status)
{
  uv_psw_client_work_t *work = (uv_psw_client_work_t *)_work;
  assert(status == 0 && work->status == 0);
  free(work);
}

static void
after_close_tcp_monitor(uv_handle_t *_monitor)
{
  uv_tcp_monitor_t *monitor = (uv_tcp_monitor_t *)_monitor;
  uv_tcp_t *client = monitor->tcp;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;

  // free(client);
  http_client_socket_deinit(hcs);
  free(hcs);
}

static void
after_close_imported_tcp(uv_tcp_monitor_t *monitor)
{
  int error;
  uv_tcp_t *client = monitor->tcp;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;
  struct global_config *gconf = (struct global_config *)client->loop->data;
  prism_switch_client_t *sw_client = gconf->sw_client;

  /*
   * Cleanup switch rule
   */
  uv_psw_client_work_t *work = (uv_psw_client_work_t *)malloc(sizeof(*work));
  assert(work != NULL);

  work->status = -1;
  work->req_type = PSW_REQ_DELETE;
  work->client = sw_client;
  work->delete_req.peer_addr = hcs->peername_cache.peer_addr;
  work->delete_req.peer_port = hcs->peername_cache.peer_port;

  error = uv_queue_work(client->loop, (uv_work_t *)work, uv_psw_client_work,
                        after_delete_switch_rule);
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
after_close_tcp(uv_tcp_monitor_t *monitor)
{
  uv_close((uv_handle_t *)monitor, after_close_tcp_monitor);
}

int
phttp_start_close(uv_tcp_t *client)
{
  int error;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;

  if (hcs->imported) {
    error =
        uv_tcp_monitor_schedule_close(&hcs->monitor, after_close_imported_tcp);
    assert(error == 0);
  } else {
    error = uv_tcp_monitor_schedule_close(&hcs->monitor, after_close_tcp);
    assert(error == 0);
  }

  return 0;
}
