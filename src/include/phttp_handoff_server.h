#pragma once

#include <http_export.h>
#include <phttp_server.h>

typedef void (*phttp_hoproto_connect_cb)(uv_tcp_t *client, int status);

typedef struct http_handoff_server_socket {
  struct http_socket hs;
  int backlog;
  uint32_t server_addr;
  uint32_t server_port;
  http_server_socket_t *server_socket;
} http_handoff_server_socket_t;

typedef struct http_handoff_client_socket {
  struct http_socket hs;
  struct membuf req_mem;
  prism::HTTPHandoffReq *req;
  uv_write_t wreq;
  uv_buf_t wbuf;
  http_handoff_server_socket_t *hhss;
} http_handoff_client_socket_t;

struct phttp_ho_header {
  uint32_t length;
  uint8_t pad[0];
} __attribute__((packed));

int phttp_handoff_server_init(uv_loop_t *loop,
                              http_handoff_server_socket_t *hhss);
int phttp_handoff_server_read_start(uv_tcp_t *tcp,
                                    http_handoff_server_socket_t *hhss);
int phttp_on_handoff(uv_tcp_t *ho_client, prism::HTTPHandoffReq *ho_req);
