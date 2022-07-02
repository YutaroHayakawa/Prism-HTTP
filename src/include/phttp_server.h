#pragma once

#include <http.h>
#include <membuf.h>
#include <uv_tcp_monitor.h>

#include <prism_switch/prism_switch_client.h>

enum http_state { HTTP_PARSING_HEADER, HTTP_RECEIVING_BODY };

struct http_socket {
  int (*close)(uv_tcp_t *);
};

typedef int (*request_handler_t)(struct http_request *, struct http_response *,
                                 bool);

typedef struct http_server_socket {
  struct http_socket hs;
  int backlog;
  uint32_t server_addr;
  uint32_t server_port;
  uint8_t server_mac[6];
  struct TLSContext *tls;
  request_handler_t request_handler;
} http_server_socket_t;

typedef struct http_client_socket {
  struct http_socket hs;

  struct TLSContext *tls;

  enum http_state http_state;
  struct http_request req;
  struct http_response res;

  /*
   * Import/Export related data
   */
  bool imported;
  uv_tcp_monitor_t monitor;
  void *export_data;

  /*
   * Buffers for sending response
   */
  uv_write_t wreq;
  uv_buf_t wbufs[2];

  /*
   * Peer address and peer port in network byte order.
   * Should be set on accept handler or import handler.
   */
  struct {
    uint32_t peer_addr;
    uint32_t peer_port;
  } peername_cache;

  http_server_socket_t *server_sock;
} http_client_socket_t;

typedef struct http_server_handoff_data {
  uv_tcp_t dest;
  uint32_t addr;
  uint32_t port;
  uv_write_t wreq;
  uv_buf_t wbuf;
} http_server_handoff_data_t;

struct global_config {
  prism_switch_client_t *sw_client;
};

/*
 * Initialize http server. Users need to provide libuv loop object and
 * configuration which contains following things.
 *
 * backlog         : Number of backlog which will be passed to listen(2)
 * server_addr     : Server's IPv4 address in network byte order
 * server_port     : Server's TCP port in network byte order. Only lower 16bits
 * are used. server_mac      : Server's MAC address. Required for handoff in L2
 * networks. request_handler : HTTP request handler
 */
int phttp_server_init(uv_loop_t *loop, http_server_socket_t *conf);
int http_client_socket_init(http_client_socket_t *hcs, bool import);
void http_client_socket_deinit(http_client_socket_t *hcs);

/*
 * For internal use only
 */
void phttp_on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
void phttp_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
int phttp_start_handoff(uv_tcp_t *client);
int phttp_send_http_res(uv_tcp_t *client, bool continue_res);
int phttp_start_close(uv_tcp_t *client);
