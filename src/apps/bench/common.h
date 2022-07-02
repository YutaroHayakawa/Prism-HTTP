#pragma once

#include <phttp.h>

static int
http_server_close(uv_tcp_t *server)
{
  uv_close((uv_handle_t *)server, NULL);
  return 0;
}

static int
http_handoff_server_close(uv_tcp_t *server)
{
  uv_close((uv_handle_t *)server, NULL);
  return 0;
}

static int
read_from_file(const char *fname, void *buf, int max_len)
{
  FILE *f = fopen(fname, "rb");
  if (f) {
    int size = fread(buf, 1, max_len - 1, f);
    if (size > 0) {
      ((unsigned char *)buf)[size] = 0;
    } else {
      ((unsigned char *)buf)[0] = 0;
    }
    fclose(f);
    return size;
  }
  return 0;
}

static void
load_keys(struct TLSContext *context, const char *fname, const char *priv_fname)
{
  unsigned char buf[0xFFFF];
  unsigned char buf2[0xFFFF];
  int size = read_from_file(fname, buf, 0xFFFF);
  int size2 = read_from_file(priv_fname, buf2, 0xFFFF);
  if (size > 0) {
    if (context) {
      tls_load_certificates(context, buf, size);
      tls_load_private_key(context, buf2, size2);
    }
  }
}

static void
tweak_phttp_args(struct phttp_args *args, uint32_t workerid)
{
  args->ho_port += workerid;
}

static void
init_server_conf(struct phttp_args *args, http_server_socket_t *hss)
{
  hss->hs.close = http_server_close;
  hss->backlog = args->backlog;
  hss->server_addr = inet_addr(args->addr.c_str());
  hss->server_port = htons(args->port);
  memcpy(hss->server_mac, args->mac, 6);
  if (args->tls) {
    hss->tls = tls_create_context(1, TLS_V12);
    assert(hss->tls != NULL);
    load_keys(hss->tls, args->tls_crt.c_str(), args->tls_key.c_str());
  }
}

static void
init_handoff_server_conf(struct phttp_args *args,
                         http_handoff_server_socket_t *hhss,
                         http_server_socket *hss)
{
  hhss->hs.close = http_handoff_server_close;
  hhss->backlog = args->ho_backlog;
  hhss->server_addr = inet_addr(args->ho_addr.c_str());
  hhss->server_port = htons(args->ho_port);
  hhss->server_socket = hss;
}

static void
init_global_conf(struct phttp_args *args, struct global_config *gconf,
                 uv_loop_t *loop)
{
  std::string host = args->sw_addr + ":" + args->sw_port;
  gconf->sw_client = prism_switch_client_create(loop, host.c_str());
}

static void
init_all_conf(uv_loop_t *loop, struct phttp_args *args,
              http_server_socket_t *hss, http_handoff_server_socket_t *hhss,
              struct global_config *gconf)
{
  init_server_conf(args, hss);
  init_handoff_server_conf(args, hhss, hss);
  init_global_conf(args, gconf, loop);
}

static void
dummy_work(uv_work_t *work)
{
  printf("Dummy work for initialize thread pool\n");
}

static void
dummy_work_done(uv_work_t *work, int status)
{
  assert(status == 0);
  printf("Dummy work done\n");
  free(work);
}

static void
make_dummy_work(uv_loop_t *loop)
{
  int error;
  uv_work_t *work = (uv_work_t *)malloc(sizeof(*work));
  assert(work != NULL);
  error = uv_queue_work(loop, work, dummy_work, dummy_work_done);
  assert(error == 0);
}

static void
on_walk(uv_handle_t *handle, void *arg)
{
  assert(handle != NULL);

  if (uv_is_closing(handle)) {
    return;
  }

  printf("Closing %p\n", handle);

  if (handle->type == UV_UDP) {
    uv_close(handle, NULL);
  }

  if (handle->type == UV_TCP) {
    struct http_socket *hs = (struct http_socket *)handle->data;
    if (hs != NULL) {
      hs->close((uv_tcp_t *)handle);
      return;
    }
  }

  uv_close(handle, NULL);
}

static void
on_sig(uv_signal_t *handle, int signal)
{
  printf("Caught SIGINT!\n");
  uv_print_all_handles(handle->loop, stdout);
  printf("\n");
  uv_walk(handle->loop, on_walk, NULL);
}

static void
try_connect(uv_timer_t *handle)
{
  int error, sock;
  struct sockaddr_in addr;
  http_server_handoff_data_t *ho_data =
      (http_server_handoff_data_t *)handle->data;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  assert(sock > 0);

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ho_data->addr;
  addr.sin_port = ho_data->port;

  error = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
  if (error) {
    printf("[%d] Could not connect to proxy. Retrying...\n", getpid());
    close(sock);
    return;
  }

  error = uv_tcp_open(&ho_data->dest, sock);
  assert(error == 0);

  error = uv_tcp_nodelay(&ho_data->dest, 1);
  assert(error == 0);

  error = uv_timer_stop(handle);
  assert(error == 0);

  uv_close((uv_handle_t *)handle, (uv_close_cb)free);

  printf("[%d] Connected!!!\n", getpid());
}

static int
start_connect(uv_loop_t *loop, http_server_handoff_data_t *ho_data)
{
  int error;

  error = uv_tcp_init(loop, &ho_data->dest);
  assert(error == 0);

  uv_timer_t *connect_timer = (uv_timer_t *)malloc(sizeof(*connect_timer));
  assert(connect_timer != NULL);

  error = uv_timer_init(loop, connect_timer);
  assert(error == 0);

  connect_timer->data = ho_data;

  error = uv_timer_start(connect_timer, try_connect, 0, 1000);
  assert(error == 0);

  return 0;
}
