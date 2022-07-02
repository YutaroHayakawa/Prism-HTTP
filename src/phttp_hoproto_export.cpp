#include <unistd.h>

#include <tcp_export.h>
#include <tls_export.h>
#include <http_export.h>
#include <phttp_server.h>
#include <phttp_prof.h>
#include <uv_psw_client.h>
#include <util.h>

#define PROF(_id)                                                              \
  do {                                                                         \
    prof_tstamp(PROF_TYPE_EXPORT, _id, hcs->peername_cache.peer_addr,          \
                hcs->peername_cache.peer_port);                                \
  } while (0)

struct handoff_ctx {
  uv_write_t req;
  uv_buf_t buf[2];
  std::string *serialized_data;
  uv_tcp_t *client;
};

static void
after_close_tcp_monitor(uv_handle_t *_monitor)
{
  uv_tcp_monitor_t *monitor = (uv_tcp_monitor_t *)_monitor;
  uv_tcp_t *client = monitor->tcp;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;

  free(client);
  http_client_socket_deinit(hcs);
  free(hcs);
}

static void
handoff_done(uv_write_t *req, int status)
{
  struct handoff_ctx *ctx = (struct handoff_ctx *)req->data;
  uv_tcp_t *client = ctx->client;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;

  assert(status == 0);

  PROF(PROF_SEND_PROTO_STATES);

  delete ctx->serialized_data;
  free(ctx->buf[0].base);
  free(ctx);

  int evfd;
  uv_poll_stop((uv_poll_t *)&hcs->monitor);
  uv_fileno((uv_handle_t *)&hcs->monitor, &evfd);
  close(evfd);
  uv_close((uv_handle_t *)&hcs->monitor, after_close_tcp_monitor);
}

static void
after_close_tcp(uv_tcp_monitor_t *monitor)
{
  int error;
  bool serialize_ok;
  uv_tcp_t *client = (uv_tcp_t *)monitor->tcp;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;
  http_server_handoff_data_t *ho_data =
      (http_server_handoff_data_t *)hcs->res.handoff_data;
  prism::HTTPHandoffReq *ho_req = (prism::HTTPHandoffReq *)monitor->super.data;

  PROF(PROF_TCP_CLOSE);

  struct handoff_ctx *ctx = (struct handoff_ctx *)malloc(sizeof(*ctx));
  assert(ctx != NULL);

  ctx->serialized_data = new std::string();
  serialize_ok = ho_req->SerializeToString(ctx->serialized_data);
  assert(serialize_ok);
  PROF(PROF_SERIALIZE);

  uint32_t *buflen = (uint32_t *)malloc(sizeof(uint32_t));
  assert(buflen != NULL);
  *buflen = htonl(ctx->serialized_data->size());

  ctx->buf[0].base = (char *)buflen;
  ctx->buf[0].len = sizeof(uint32_t);
  ctx->buf[1].base = const_cast<char *>(ctx->serialized_data->c_str());
  ctx->buf[1].len = ctx->serialized_data->size();
  ctx->client = client;
  ctx->req.data = ctx;

  error = uv_write(&ctx->req, (uv_stream_t *)&ho_data->dest, ctx->buf, 2,
                   handoff_done);
  assert(error == 0);

  delete ho_req;
}

static int
export_tcp(int sock, prism::TCPState *tcp_state)
{
  return tcp_export(sock, tcp_state);
}

static int
export_tls(int sock, struct TLSContext *tls, prism::TLSState *tls_state)
{
  int error;
  error = tls_unmake_ktls(tls, sock);
  assert(error == 0);
  return tls_export(tls, tls_state);
}

static int
export_http(struct http_request *http, prism::HTTPReq *http_state)
{
  return http_request_export(http, http_state);
}

static int
export_all(uv_tcp_t *client, prism::HTTPHandoffReq **ho_reqp)
{
  int error, sock;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;
  prism::HTTPHandoffReq *ho_req = new prism::HTTPHandoffReq();
  prism::TCPState *tcp;
  prism::TLSState *tls;
  prism::HTTPReq *http;

  uv_fileno((uv_handle_t *)client, &sock);

  tcp = new prism::TCPState();
  error = export_tcp(sock, tcp);
  assert(error == 0);
  ho_req->set_allocated_tcp(tcp);
  PROF(PROF_EXPORT_TCP);

  if (hcs->tls != NULL) {
    tls = new prism::TLSState();
    error = export_tls(sock, hcs->tls, tls);
    assert(error == 0);
    ho_req->set_allocated_tls(tls);
    PROF(PROF_EXPORT_TLS);
  }

  http = new prism::HTTPReq();
  error = export_http(&hcs->req, http);
  assert(error == 0);
  ho_req->set_allocated_http(http);
  PROF(PROF_EXPORT_HTTP);

  *ho_reqp = ho_req;

  return 0;
}

static void
after_add_switch_rule(uv_work_t *_work, int status)
{
  int error;
  uv_psw_client_work_t *work = (uv_psw_client_work_t *)_work;
  uv_tcp_t *client = (uv_tcp_t *)work->super.data;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;

  assert(status == 0 && work->status == 0);

  PROF(PROF_ADD);

  free(work);

  prism::HTTPHandoffReq *ho_req;
  error = export_all(client, &ho_req);
  assert(error == 0);

  hcs->monitor.super.data = ho_req;
  uv_tcp_monitor_schedule_close(&hcs->monitor, after_close_tcp);
}

static void
after_lock_switch_rule(uv_work_t *_work, int status)
{
  int error;
  uv_psw_client_work_t *work = (uv_psw_client_work_t *)_work;
  uv_tcp_t *client = (uv_tcp_t *)work->super.data;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;

  assert(status == 0 && work->status == 0);

  PROF(PROF_LOCK);

  free(work);

  prism::HTTPHandoffReq *ho_req;
  error = export_all(client, &ho_req);
  assert(error == 0);

  hcs->monitor.super.data = ho_req;
  uv_tcp_monitor_schedule_close(&hcs->monitor, after_close_tcp);
}

int
phttp_start_handoff(uv_tcp_t *client)
{
  int error;
  struct global_config *gconf = (struct global_config *)client->loop->data;
  prism_switch_client_t *sw_client = (prism_switch_client_t *)gconf->sw_client;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;

  uv_psw_client_work_t *work = (uv_psw_client_work_t *)malloc(sizeof(*work));
  assert(work != NULL);

  if (hcs->imported) {
    /*
     * Request switch to lock the rule of this flow.
     */
    work->status = -1;
    work->super.data = client;
    work->req_type = PSW_REQ_LOCK;
    work->client = sw_client;
    work->lock_req.peer_addr = hcs->peername_cache.peer_addr;
    work->lock_req.peer_port = hcs->peername_cache.peer_port;

    error = uv_queue_work(client->loop, (uv_work_t *)work, uv_psw_client_work,
                          after_lock_switch_rule);
    assert(error == 0);
  } else {
    /*
     * Request switch to add rewrite rule to this flow.
     * Lock the rule since we will hand off it immidiately.
     */
    work->status = -1;
    work->super.data = client;
    work->req_type = PSW_REQ_ADD;
    work->client = sw_client;
    work->add_req.peer_addr = hcs->peername_cache.peer_addr;
    work->add_req.peer_port = hcs->peername_cache.peer_port;
    work->add_req.virtual_addr = hcs->server_sock->server_addr;
    work->add_req.virtual_port = hcs->server_sock->server_port;
    work->add_req.owner_addr = hcs->server_sock->server_addr;
    work->add_req.owner_port = hcs->server_sock->server_port;
    work->add_req.owner_mac = hcs->server_sock->server_mac;
    work->add_req.lock = true;

    DEBUG("2. Starting add switch rule\n");

    error = uv_queue_work(client->loop, (uv_work_t *)work, uv_psw_client_work,
                          after_add_switch_rule);
    assert(error == 0);
  }

  return 0;
}
