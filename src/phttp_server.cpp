#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <unistd.h>
#include <sys/eventfd.h>

#include <http_export.h>
#include <phttp_handoff_server.h>
#include <phttp_server.h>
#include <phttp_prof.h>
#include <tcp_export.h>
#include <tls_export.h>
#include <uv_tcp_monitor.h>
#include <util.h>

#define PROF(_id)                                                              \
  do {                                                                         \
    prof_tstamp(PROF_TYPE_EXPORT, _id, hcs->peername_cache.peer_addr,          \
                hcs->peername_cache.peer_port);                                \
  } while (0)

void
http_client_socket_deinit(http_client_socket_t *hcs)
{
  http_request_deinit(&hcs->req);
  http_response_deinit(&hcs->res);
  if (hcs->tls) {
    tls_destroy_context(hcs->tls);
  }
}

int
http_client_socket_init(http_client_socket_t *hcs, bool import)
{
  int error;

  /* Setup close handler */
  hcs->hs.close = phttp_start_close;

  hcs->tls = NULL;
  hcs->http_state = HTTP_PARSING_HEADER;

  if (!import) {
    error = http_request_init(&hcs->req);
    assert(error == 0);
    error = http_response_init(&hcs->res);
    assert(error == 0);
  }

  hcs->imported = import;
  /* hcs->monitor uninitialized here */
  /* hcs->wreq uninitialized here */
  /* hcs->wbufs uninitialized here */
  hcs->peername_cache.peer_addr = 0;
  hcs->peername_cache.peer_port = 0;
  /* hcs->server_socket uninitialized here */

  return 0;
}

void
phttp_on_alloc(uv_handle_t *_client, size_t suggested_size, uv_buf_t *buf)
{
  uv_tcp_t *client = (uv_tcp_t *)_client;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;

  /*
   * Grow buffer when it is not enough. This may have
   * unacceptable performance penalty. Need to tune
   * default buffer length when we see frequent grow.
   */
  if (membuf_avail(&hcs->req.mem) < 1024) {
    fprintf(stderr, "Warning: Memory growing occured\n");
    membuf_grow(&hcs->req.mem, 4096);
  }

  buf->base = hcs->req.mem.cur;
  buf->len = membuf_avail(&hcs->req.mem);
}

static void
after_send_http_res(uv_write_t *wreq, int status)
{
  uv_tcp_t *client = (uv_tcp_t *)wreq->handle;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;
  struct http_request *req = &hcs->req;
  struct http_response *res = &hcs->res;

  assert(status == 0);

  PROF(PROF_HTTP_RES);

  if (res->after_res) {
    res->after_res(res);
  }

  http_request_reset(req);
  http_response_reset(res);
}

static void
after_send_continue_res(uv_write_t *wreq, int status)
{
  uv_tcp_t *client = (uv_tcp_t *)wreq->handle;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;
  struct http_response *res = &hcs->res;

  assert(status == 0);

  http_response_reset(res);
}

int
phttp_send_http_res(uv_tcp_t *client, bool continue_res)
{
  int error, nprinted;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;
  struct http_response *res = &hcs->res;
  struct membuf *mem = &res->mem;
  struct membuf *body_mem = &res->body_mem;
  uv_write_cb after_res;

  if (continue_res) {
    nprinted = snprintf(mem->cur, membuf_avail(mem),
                        "HTTP/1.1 %u %s\r\n",
                        res->status, res->reason);
    assert(nprinted > 0 && (uint64_t)nprinted < membuf_avail(mem));
    membuf_consume(mem, (uint64_t)nprinted);
    after_res = after_send_continue_res;
  } else {
    /* Print boiler plate headers */
    nprinted = snprintf(mem->cur, membuf_avail(mem),
                        "HTTP/1.1 %u %s\r\n"
                        "Server: Prism\r\n",
                        res->status, res->reason);
    assert(nprinted > 0 && (uint64_t)nprinted < membuf_avail(mem));
    membuf_consume(mem, (uint64_t)nprinted);

    nprinted = snprintf(mem->cur, membuf_avail(mem), "Content-Length: %lu\r\n",
                        membuf_used(body_mem));
    assert(nprinted > 0 && (uint64_t)nprinted < membuf_avail(mem));
    membuf_consume(mem, (uint64_t)nprinted);

    /* Print user supplied headers */
    struct http_header *h;
    for (uint32_t i = 0; i < res->nheaders; i++) {
      h = res->headers + i;
      assert(h->name_len <= INT_MAX && h->val_len <= INT_MAX);
      nprinted = snprintf(mem->cur, membuf_avail(mem), "%.*s: %.*s\r\n",
                          (int)h->name_len, h->name, (int)h->val_len, h->val);
      assert(nprinted > 0 && (uint64_t)nprinted < membuf_avail(mem));
      membuf_consume(mem, (uint64_t)nprinted);
    }

    after_res = after_send_http_res;
  }

  assert(membuf_avail(mem) > 2);
  memcpy(mem->cur, "\r\n", 2);
  membuf_consume(mem, 2);

  unsigned int nsend = 1;
  uv_write_t *wreq = &hcs->wreq;
  uv_buf_t *wbufs = hcs->wbufs;
  wbufs[0].base = mem->begin;
  wbufs[0].len = membuf_used(mem);

  if (membuf_used(body_mem) != 0) {
    nsend++;
    wbufs[1].base = body_mem->begin;
    wbufs[1].len = membuf_used(body_mem);
  }

  error =
      uv_write(wreq, (uv_stream_t *)client, wbufs, nsend, after_res);
  assert(error == 0);

  return 0;
}

void
phttp_on_read(uv_stream_t *_client, ssize_t nread, const uv_buf_t *buf)
{
  int error, nparsed;
  uint64_t body_len;
  uv_tcp_t *client = (uv_tcp_t *)_client;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;
  struct http_request *req = &hcs->req;
  struct http_response *res = &hcs->res;
  struct membuf *mem = &hcs->req.mem;

  if (nread == 0) {
    return;
  }

  if (nread < 0) {
    uv_perror("on_read", (int)nread);
    hcs->hs.close(client);
    return;
  }

  error = membuf_consume(mem, (size_t)nread);
  assert(error == 0);

  /*
   * We may have TLS decryption here
   */

  switch (hcs->http_state) {
  case HTTP_PARSING_HEADER:
    nparsed = http_parse_request(req);
    if (nparsed == -1) {
      fprintf(stderr, "HTTP request parsing failed\n");
      hcs->hs.close(client);
      return;
    } else if (nparsed == -2) {
      return;
    }

    /*
     * Parsing done, go to next state
     */
    body_len = http_request_determine_body_len(req);
    if (body_len == 0) {
      break;
    } else {
      req->body = mem->prev + nparsed;
      req->body_len = body_len;
      hcs->http_state = HTTP_RECEIVING_BODY;
    }

  case HTTP_RECEIVING_BODY:
    if ((uint64_t)(mem->cur - req->body) == req->body_len) {
      hcs->http_state = HTTP_PARSING_HEADER;
      break;
    } else if ((uint64_t)(mem->cur - req->body) > req->body_len) {
      /*
       * Unacceptable request format, may be oversized body or
       * pipelining. Abort request.
       */
      printf("Unacceptable request format!\n");
      hcs->hs.close(client);
      return;
    } else {
      /*
       * Request is incomplete, continue receiving.
       */
      return;
    }

    break;

  default:
    fprintf(stderr, "Unknown HTTP state\n");
    break;
  }

  error = hcs->server_sock->request_handler(req, res, false);
  assert(error == 0);

  /*
   * Our own special status code for invoking handoff
   */
  if (res->status == 600) {
    error = phttp_start_handoff(client);
    if (error) {
      res->status = 500;
      res->reason = "Internal Server Error";
    } else {
      return;
    }
  }

  error = phttp_send_http_res(client, false);
  if (error) {
    printf("Error returned from HTTP handler!\n");
    hcs->hs.close(client);
  }
}

static void
on_tls_handshake_alloc(uv_handle_t *_client, size_t suggested_size,
                       uv_buf_t *buf)
{
  buf->base = (char *)malloc(4096);
  buf->len = 4096;
}

static void
after_tls_send_pending(uv_write_t *req, int status)
{
  uv_tcp_t *client = (uv_tcp_t *)req->handle;
  uv_buf_t *wbuf = (uv_buf_t *)req->data;
  http_client_socket_t *hcs = (http_client_socket_t *)client->data;
  assert(status == 0);
  tls_buffer_clear(hcs->tls);
  free(wbuf);
  free(req);
}

static int
tls_send_pending(uv_tcp_t *client)
{
  int error;
  http_client_socket *hcs = (http_client_socket *)client->data;
  uv_write_t *wreq = (uv_write_t *)malloc(sizeof(*wreq));
  uv_buf_t *wbuf = (uv_buf_t *)malloc(sizeof(*wbuf));
  assert(wreq != NULL && wbuf != NULL);

  unsigned int write_buf_len = 0;
  wbuf->base = (char *)const_cast<unsigned char *>(
      tls_get_write_buffer(hcs->tls, &write_buf_len));
  wbuf->len = write_buf_len;
  wreq->data = wbuf;

  error =
      uv_write(wreq, (uv_stream_t *)client, wbuf, 1, after_tls_send_pending);
  assert(error == 0);

  return 0;
}

static void
on_tls_handshake_read(uv_stream_t *_client, ssize_t nread, const uv_buf_t *buf)
{
  int error;
  uv_tcp_t *client = (uv_tcp_t *)_client;
  http_client_socket *hcs = (http_client_socket *)client->data;

  if (nread < 0) {
    uv_perror("on_read", (int)nread);
    hcs->hs.close(client);
    free(buf->base);
    return;
  }

  int has_pending_message;
  has_pending_message = tls_consume_stream(
      hcs->tls, (const unsigned char *)buf->base, nread, NULL);
  if (has_pending_message) {
    error = tls_send_pending(client);
    assert(error == 0);
  }

  if (tls_established(hcs->tls)) {
    int sock;
    uv_fileno((uv_handle_t *)client, &sock);
    error = tls_make_ktls(hcs->tls, sock);
    assert(error == 0);
    error = uv_read_start((uv_stream_t *)client, phttp_on_alloc, phttp_on_read);
    assert(error == 0);
  }

  free(buf->base);
}

static void
on_connection(uv_stream_t *_server, int status)
{
  int error;
  struct sockaddr_in peeraddr;
  int peeraddr_len = sizeof(peeraddr);

  assert(status == 0);

  uv_tcp_t *client = (uv_tcp_t *)malloc(sizeof(*client));
  http_client_socket_t *hcs = (http_client_socket_t *)malloc(sizeof(*hcs));
  assert(client != NULL && hcs != NULL);

  error = http_client_socket_init(hcs, false);
  assert(error == 0);

  hcs->server_sock = (http_server_socket_t *)_server->data;
  client->data = hcs;

  error = uv_tcp_init(_server->loop, client);
  assert(error == 0);

  error = uv_accept(_server, (uv_stream_t *)client);
  assert(error == 0);

  error = uv_tcp_nodelay(client, 1);
  assert(error == 0);

  error = uv_tcp_monitor_init(_server->loop, &hcs->monitor, client);
  assert(error == 0);

  uv_read_cb read_cb;
  uv_alloc_cb alloc_cb;
  if (hcs->server_sock->tls) {
    read_cb = on_tls_handshake_read;
    alloc_cb = on_tls_handshake_alloc;
    hcs->tls = tls_accept(hcs->server_sock->tls);
    tls_make_exportable(hcs->tls, 1);
  } else {
    read_cb = phttp_on_read;
    alloc_cb = phttp_on_alloc;
  }

  error = uv_read_start((uv_stream_t *)client, alloc_cb, read_cb);
  assert(error == 0);

  error =
      uv_tcp_getpeername(client, (struct sockaddr *)&peeraddr, &peeraddr_len);
  assert(error == 0);

  hcs->peername_cache.peer_addr = peeraddr.sin_addr.s_addr;
  hcs->peername_cache.peer_port = peeraddr.sin_port;
}

int
phttp_server_init(uv_loop_t *loop, http_server_socket_t *hss)
{
  int error, sock, opt = 1;

  uv_tcp_t *server = (uv_tcp_t *)malloc(sizeof(*server));
  assert(server != NULL);

  server->data = hss;

  error = uv_tcp_init_ex(loop, server, AF_INET);
  assert(error == 0);

  uv_fileno((uv_handle_t *)server, &sock);

  error = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  assert(error == 0);

  error = setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
  assert(error == 0);

  error = uv_tcp_simultaneous_accepts(server, 1);
  assert(error == 0);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = hss->server_addr;
  addr.sin_port = hss->server_port;

  error = uv_tcp_bind(server, (struct sockaddr *)&addr, sizeof(addr));
  assert(error == 0);

  error = uv_listen((uv_stream_t *)server, hss->backlog, on_connection);
  assert(error == 0);

  return 0;
}
