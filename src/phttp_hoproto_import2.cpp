#include <tcp_export.h>
#include <tls_export.h>
#include <http_export.h>
#include <phttp_server.h>
#include <phttp_prof.h>
#include <phttp_handoff_server.h>
#include <util.h>

#include <prism_switch/prism_switch_client.h>

#define PROF(_id, _peer_addr, _peer_port)                                      \
  do {                                                                         \
    prof_tstamp(PROF_TYPE_IMPORT, _id, _peer_addr, _peer_port);                \
  } while (0)

static void
after_change_owner(struct psw_req_base *req, void *data)
{
  int error;
  uv_tcp_t *client = (uv_tcp_t *)data;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;

  assert(req->status == 0);

  PROF(PROF_CHOWN, hcs->peername_cache.peer_addr,
       hcs->peername_cache.peer_port);

  error = uv_read_start((uv_stream_t *)client, phttp_on_alloc, phttp_on_read);
  assert(error == 0);

  error = phttp_send_http_res(client, false);
  assert(error == 0);
}

static int
start_change_owner(uv_tcp_t *client)
{
  struct global_config *gconf = (struct global_config *)client->loop->data;
  prism_switch_client_t *sw_client = (prism_switch_client_t *)gconf->sw_client;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;
  http_server_socket_t *hss = hcs->server_sock;

  /*
   * Request switch to change the owner of this flow.
   * unlock the rule since proxy hands off it immidiately.
   */
  struct psw_chown_req chown_req;
  chown_req.type = PSW_REQ_CHOWN;
  chown_req.status = 0;
  chown_req.peer_addr = hcs->peername_cache.peer_addr;
  chown_req.peer_port = hcs->peername_cache.peer_port;
  chown_req.owner_addr = hss->server_addr;
  chown_req.owner_port = hss->server_port;
  memcpy(chown_req.owner_mac, hss->server_mac, 6);
  chown_req.unlock = 1;

  return prism_switch_client_queue_task(
      sw_client, (struct psw_req_base *)&chown_req, after_change_owner, client);
}

struct forward_ctx {
  uint32_t peer_addr;
  uint16_t peer_port;
  uv_write_t req;
  uv_buf_t buf[2];
  std::string *serialized_data;
};

static void
forward_done(uv_write_t *req, int status)
{
  struct forward_ctx *ctx = (struct forward_ctx *)req->data;

  if (status != 0) {
    uv_perror("forward_done", status);
  }

  assert(status == 0);

  PROF(PROF_FORWARDING, ctx->peer_addr, ctx->peer_port);

  delete ctx->serialized_data;
  free(ctx->buf[0].base);
  free(ctx);
}

static http_server_socket_t *
ho_client_to_hss(uv_tcp_t *ho_client)
{
  http_handoff_client_socket_t *hhcs =
      (http_handoff_client_socket_t *)ho_client->data;
  http_handoff_server_socket_t *hhss =
      (http_handoff_server_socket_t *)hhcs->hhss;
  return hhss->server_socket;
}

static int
import_tcp(uv_loop_t *loop, uv_tcp_t **tcp, const prism::TCPState *tcp_state)
{
  int error;
  *tcp = (uv_tcp_t *)malloc(sizeof(uv_tcp_t));
  assert(*tcp != NULL);

  error = uv_tcp_init(loop, *tcp);
  assert(error == 0);

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  assert(sock != -1);

  error = tcp_import(sock, tcp_state);
  assert(error == 0);

  error = uv_tcp_open(*tcp, sock);
  assert(error == 0);

  error = uv_tcp_nodelay(*tcp, 1);
  assert(error == 0);

  return error;
}

static int
import_tls(uv_tcp_t *tcp, struct TLSContext **tls,
           const prism::TLSState *tls_state)
{
  int error, sock;

  *tls = tls_create_context(1, TLS_V12);
  assert(*tls != NULL);

  error = tls_import(*tls, tls_state);
  assert(error == 0);

  tls_make_exportable(*tls, 1);
  assert(error == 0);

  uv_fileno((uv_handle_t *)tcp, &sock);
  error = tls_make_ktls(*tls, sock);
  assert(error == 0);

  return 0;
}

static int
continue_import(uv_loop_t *loop, uv_tcp_t **client,
                const prism::HTTPHandoffReq *ho_req)
{
  int error;
  http_client_socket_t *hcs = (http_client_socket_t *)malloc(sizeof(*hcs));
  assert(hcs != NULL);

  error = http_client_socket_init(hcs, true);
  assert(error == 0);

  hcs->peername_cache.peer_addr = ho_req->tcp().peer_addr();
  hcs->peername_cache.peer_port = ho_req->tcp().peer_port();

  error = import_tcp(loop, client, &ho_req->tcp());
  assert(error == 0);

  PROF(PROF_IMPORT_TCP, hcs->peername_cache.peer_addr,
       hcs->peername_cache.peer_addr);

  error = uv_tcp_monitor_init(loop, &hcs->monitor, *client);
  assert(error == 0);

  if (ho_req->has_tls()) {
    error = import_tls(*client, &hcs->tls, &ho_req->tls());
    assert(error == 0);
    PROF(PROF_IMPORT_TLS, hcs->peername_cache.peer_addr,
         hcs->peername_cache.peer_port);
  }

  (*client)->data = hcs;

  return 0;
}

static int
forward_proto_states(struct http_response *res, prism::HTTPHandoffReq *ho_req)
{
  int error;
  bool serialize_ok;
  struct http_server_handoff_data *ho_data =
      (struct http_server_handoff_data *)res->handoff_data;

  struct forward_ctx *ctx = (struct forward_ctx *)malloc(sizeof(*ctx));
  assert(ctx != NULL);

  ctx->peer_addr = ho_req->tcp().peer_addr();
  ctx->peer_port = ho_req->tcp().peer_port();

  ctx->serialized_data = new std::string(ho_req->ByteSizeLong(), '\0');
  serialize_ok = ho_req->SerializeToString(ctx->serialized_data);
  assert(serialize_ok);
  PROF(PROF_SERIALIZE, ctx->peer_addr, ctx->peer_port);

  uint32_t padlen = ctx->serialized_data->size() % 8 == 0
                        ? 0
                        : 8 - ctx->serialized_data->size() % 8;

  struct phttp_ho_header *header =
      (struct phttp_ho_header *)malloc(sizeof(*header) + padlen);
  assert(header != NULL);
  header->length = htonl(ctx->serialized_data->size());

  ctx->buf[0].base = (char *)header;
  ctx->buf[0].len = sizeof(uint32_t) + padlen;
  ctx->buf[1].base = const_cast<char *>(ctx->serialized_data->c_str());
  ctx->buf[1].len = ctx->serialized_data->size();
  ctx->req.data = ctx;

  error = uv_write(&ctx->req, (uv_stream_t *)&ho_data->dest, ctx->buf, 2,
                   forward_done);
  assert(error == 0);

  return 0;
}

int
phttp_on_handoff(uv_tcp_t *ho_client, prism::HTTPHandoffReq *ho_req)
{
  int error;

  PROF(PROF_HANDOFF, ho_req->tcp().peer_addr(), ho_req->tcp().peer_port());

  /*
   * First, import only http request and invoke request handler.
   * If the application returned 100 ~ 500 status code, we need
   * to import rest of the protocol states and send response to
   * the client.
   */
  struct http_request *req = (struct http_request *)malloc(sizeof(*req));
  assert(req != NULL);
  error = http_request_init(req);
  assert(error == 0);
  error = http_request_import(req, &ho_req->http());
  assert(error == 0);

  PROF(PROF_IMPORT_HTTP, ho_req->tcp().peer_addr(), ho_req->tcp().peer_port());

  struct http_response *res = (struct http_response *)malloc(sizeof(*res));
  assert(res != NULL);
  error = http_response_init(res);
  assert(error == 0);

  http_server_socket_t *hss = ho_client_to_hss(ho_client);
  error = hss->request_handler(req, res, true);
  assert(error == 0);

  PROF(PROF_HANDLE_HTTP_REQ, ho_req->tcp().peer_addr(),
       ho_req->tcp().peer_port());

  /*
   * Handler returned ordinal http response. Import
   * rest of the protocol states and send response.
   */
  if (res->status <= 500) {
    uv_tcp_t *client;
    http_client_socket_t *hcs;

    const_cast<prism::TCPState &>(ho_req->tcp())
        .set_self_addr(hss->server_addr);
    const_cast<prism::TCPState &>(ho_req->tcp())
        .set_self_port(hss->server_port);

    error = continue_import(ho_client->loop, &client, ho_req);
    assert(error == 0);

    hcs = (http_client_socket_t *)client->data;
    memcpy(&hcs->req, req, sizeof(*req));
    memcpy(&hcs->res, res, sizeof(*res));
    hcs->server_sock = hss;

    error = start_change_owner(client);
    assert(error == 0);

    /*
     * Contents of request and response objects are
     * copied into client socket object, we just need
     * to free container.
     */
    free(req);
    free(res);

    return 0;
  }

  assert(res->status == 600);

  DEBUG("3. Forward protocol states\n");

  error = forward_proto_states(res, ho_req);
  assert(error == 0);

  http_request_deinit(req);
  http_response_deinit(res);
  free(req);
  free(res);

  return 0;
}
