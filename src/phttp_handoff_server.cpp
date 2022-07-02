#include <cstdint>
#include <sstream>
#include <http_export.h>
#include <phttp_handoff_server.h>
#include <uv.h>

static void
uv_perror(const char *msg, int error)
{
  fprintf(stderr, "%s: %s\n", msg, uv_strerror(error));
}

static void
http_handoff_client_socket_deinit(http_handoff_client_socket_t *hhcs)
{
  membuf_deinit(&hhcs->req_mem);
  delete hhcs->req;
}

static void
after_client_close(uv_handle_t *handle)
{
  uv_tcp_t *client = (uv_tcp_t *)handle;
  http_handoff_client_socket_t *hhcs =
      (http_handoff_client_socket_t *)client->data;
  http_handoff_client_socket_deinit(hhcs);
  free(hhcs);
  free(client);
}

static int
http_handoff_client_socket_close(uv_tcp_t *client)
{
  uv_close((uv_handle_t *)client, after_client_close);
  return 0;
}

static int
http_handoff_client_socket_init(http_handoff_client_socket_t *hhcs)
{
  hhcs->hs.close = http_handoff_client_socket_close;
  membuf_init(&hhcs->req_mem, 5000000);
  hhcs->req = new prism::HTTPHandoffReq();
  hhcs->hhss = NULL;
  return 0;
}

static void
on_alloc(uv_handle_t *_client, size_t suggested_size, uv_buf_t *buf)
{
  uv_tcp_t *client = (uv_tcp_t *)_client;
  http_handoff_client_socket_t *conn =
      (http_handoff_client_socket_t *)client->data;

  /*
   * Grow buffer when it is not enough. This may have
   * unacceptable performance penalty. Need to tune
   * default buffer length when we see frequent grow.
   */
  if (membuf_avail(&conn->req_mem) < 1024) {
    fprintf(stderr, "Warning: Memory growing occured\n");
    membuf_grow(&conn->req_mem, 4096);
  }

  buf->base = conn->req_mem.cur;
  buf->len = membuf_avail(&conn->req_mem);
}

/*
static void on_read(uv_stream_t *_client, ssize_t nread, const uv_buf_t *buf) {
  int error;
  uv_tcp_t *client = (uv_tcp_t *)_client;
  http_handoff_client_socket_t *conn =
      (http_handoff_client_socket_t *)client->data;
  struct membuf *req_mem = &conn->req_mem;
  prism::HTTPHandoffReq *req = conn->req;

  if (nread < 0) {
    uv_perror("handoff server on_read", (int)nread);
    uv_close((uv_handle_t *)client, after_client_close);
    return;
  }

  error = membuf_consume(req_mem, (uint64_t)nread);
  assert(error == 0);

  std::string *copy =
    new std::string(req_mem->begin, membuf_used(req_mem));
  bool done = req->ParseFromString(*copy);
  if (!done) {
    printf("Handoff message is incomplete.\n");
    return;
  }

  error = phttp_on_handoff(client, req);
  assert(error == 0);

  membuf_reset(req_mem);
  delete copy;
}
*/

static void
on_read(uv_stream_t *_client, ssize_t nread, const uv_buf_t *buf)
{
  int error;
  uv_tcp_t *client = (uv_tcp_t *)_client;
  http_handoff_client_socket_t *conn =
      (http_handoff_client_socket_t *)client->data;
  struct membuf *req_mem = &conn->req_mem;
  prism::HTTPHandoffReq *req = conn->req;

  if (nread == 0) {
    return;
  }

  if (nread < 0) {
    uv_perror("handoff server on_read", (int)nread);
    uv_close((uv_handle_t *)client, after_client_close);
    return;
  }

  error = membuf_consume(req_mem, (uint64_t)nread);
  assert(error == 0);

  uint32_t buflen;
  uint32_t padlen;
  char *cursor = req_mem->begin;
  while (1) {
    if ((uintptr_t)req_mem->cur - (uintptr_t)cursor < sizeof(uint32_t)) {
      break;
    }

    struct phttp_ho_header *header = (struct phttp_ho_header *)cursor;
    buflen = ntohl(header->length);
    padlen = buflen % 8 == 0 ? 0 : 8 - buflen % 8;

    if (req_mem->cur - cursor < buflen + padlen) {
      break;
    }

    cursor += sizeof(uint32_t) + padlen;

    bool done = req->ParseFromString(std::string(cursor, buflen));
    assert(done);

    cursor += buflen;

    error = phttp_on_handoff(client, req);
    assert(error == 0);

    req->Clear();
  }

  size_t rem = req_mem->cur - cursor;
  memmove(req_mem->begin, cursor, rem);
  membuf_reset(req_mem);
  membuf_consume(req_mem, rem);
}

static void
on_connection(uv_stream_t *_server, int status)
{
  int error;
  uv_tcp_t *client = NULL;
  http_handoff_server_socket_t *hhss = NULL;
  http_handoff_client_socket_t *hhcs = NULL;

  assert(status == 0);

  client = (uv_tcp_t *)malloc(sizeof(*client));
  assert(client != NULL);

  hhss = (http_handoff_server_socket_t *)_server->data;

  error = uv_tcp_init(_server->loop, client);
  assert(error == 0);

  error = uv_accept(_server, (uv_stream_t *)client);
  assert(error == 0);

  error = uv_tcp_nodelay(client, 1);
  assert(error == 0);

  hhcs = (http_handoff_client_socket_t *)malloc(sizeof(*hhcs));
  assert(hhss != NULL);

  error = http_handoff_client_socket_init(hhcs);
  assert(error == 0);

  hhcs->hhss = hhss;
  client->data = hhcs;

  error = uv_read_start((uv_stream_t *)client, on_alloc, on_read);
  assert(error == 0);

  return;
}

int
phttp_handoff_server_read_start(uv_tcp_t *tcp,
                                http_handoff_server_socket_t *hhss)
{
  int error;
  http_handoff_client_socket_t *hhcs;

  hhcs = (http_handoff_client_socket_t *)malloc(sizeof(*hhcs));
  assert(hhcs != NULL);

  error = http_handoff_client_socket_init(hhcs);
  assert(error == 0);

  hhcs->hhss = hhss;
  tcp->data = hhcs;

  error = uv_read_start((uv_stream_t *)tcp, on_alloc, on_read);
  assert(error == 0);

  return 0;
}

int
phttp_handoff_server_init(uv_loop_t *loop, http_handoff_server_socket_t *hhss)
{
  int error, sock, opt = 1;
  uv_tcp_t *server = (uv_tcp_t *)malloc(sizeof(*server));
  assert(server != NULL);

  error = uv_tcp_init_ex(loop, server, AF_INET);
  assert(error == 0);

  uv_fileno((uv_handle_t *)server, &sock);

  error = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  assert(error == 0);

  error = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
  assert(error == 0);

  error = uv_tcp_simultaneous_accepts(server, 0);
  assert(error == 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = hhss->server_addr;
  addr.sin_port = hhss->server_port;

  error = uv_tcp_bind(server, (struct sockaddr *)&addr, sizeof(addr));
  assert(error == 0);

  error = uv_listen((uv_stream_t *)server, hhss->backlog, on_connection);
  assert(error == 0);

  server->data = hhss;

  return 0;
}
