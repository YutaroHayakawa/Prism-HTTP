#include <cassert>
#include <arpa/inet.h>
#include <netinet/ip.h>

#include <uv.h>
#include <phttp_handoff_server.h>

struct connect_ctx {
  uint32_t retry;
  uint32_t retry_limit;
  struct sockaddr_in *addr;
  uv_timer_t *retry_timer;
  phttp_hoproto_connect_cb user_cb;
  uv_tcp_t saved_handler;
};

static void phttp_after_connect(uv_connect_t *req, int status);

static void
retry_connect(uv_timer_t *timer)
{
  int error;
  uv_connect_t *req = (uv_connect_t *)timer->data;
  uv_tcp_t *client = (uv_tcp_t *)req->handle;
  struct connect_ctx *ctx = (struct connect_ctx *)req->data;

  memcpy(client, &ctx->saved_handler, sizeof(*client));
  memset(req, 0, sizeof(*req));
  req->data = ctx;

  error = uv_tcp_connect(req, client, (struct sockaddr *)ctx->addr,
                         phttp_after_connect);
  assert(error == 0);
}

static void
phttp_after_connect(uv_connect_t *req, int status)
{
  int error;
  struct connect_ctx *ctx = (struct connect_ctx *)req->data;

  if (status == UV_ECONNREFUSED) {
    /*
     * Reached to retry limit. Give up connect and call user callback.
     */
    if (ctx->retry == ctx->retry_limit) {
      uv_close((uv_handle_t *)ctx->retry_timer, (uv_close_cb)free);

      ctx->user_cb((uv_tcp_t *)req->handle, status);

      free(ctx);
      free(req);
      return;
    }

    /*
     * Set timer and retry
     */
    printf("Connection refused from %s:%u retrying... (%u/%u)\n",
           inet_ntoa(ctx->addr->sin_addr), ntohs(ctx->addr->sin_port),
           ctx->retry, ctx->retry_limit);

    if (ctx->retry_timer == NULL) {
      ctx->retry_timer = (uv_timer_t *)malloc(sizeof(*ctx->retry_timer));
      assert(ctx->retry_timer != NULL);
      error = uv_timer_init(req->handle->loop, ctx->retry_timer);
      assert(error == 0);
      ctx->retry_timer->data = req;
    }

    error = uv_timer_start(ctx->retry_timer, retry_connect, 1000, 0);
    assert(error == 0);

    ctx->retry++;

    return;
  }

  assert(status == 0);

  /*
   * Connected successfully
   */
  ctx->user_cb((uv_tcp_t *)req->handle, status);
  uv_close((uv_handle_t *)ctx->retry_timer, (uv_close_cb)free);
  free(ctx);
  free(req);
}

int
phttp_start_connect(uv_tcp_t *ho_client, struct sockaddr_in *addr,
                    uint32_t retry_limit, phttp_hoproto_connect_cb cb)
{
  uv_connect_t *req;
  struct connect_ctx *ctx;

  req = (uv_connect_t *)malloc(sizeof(*req));
  assert(req != NULL);

  ctx = (struct connect_ctx *)malloc(sizeof(*ctx));
  assert(ctx != NULL);

  ctx->retry = 0;
  ctx->retry_limit = retry_limit;
  ctx->user_cb = cb;
  ctx->addr = addr;
  ctx->retry_timer = NULL;
  memcpy(&ctx->saved_handler, ho_client, sizeof(*ho_client));
  req->data = ctx;

  return uv_tcp_connect(req, ho_client, (struct sockaddr *)addr,
                        phttp_after_connect);
}
