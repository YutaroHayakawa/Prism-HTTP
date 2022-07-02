#pragma once

#include <stdint.h>
#include <uv.h>

struct prism_switch_client_s;
typedef struct prism_switch_client_s prism_switch_client_t;

enum psw_requests {
  PSW_REQ_ADD,
  PSW_REQ_DELETE,
  PSW_REQ_CHOWN,
  PSW_REQ_LOCK,
  PSW_REQ_UNLOCK
};

typedef struct psw_req_base {
  uint8_t type;
  uint16_t status;
  uint32_t peer_addr;
  uint16_t peer_port;
} __attribute__((packed)) psw_req_base_t;

typedef struct psw_add_req {
  uint8_t type;
  uint16_t status;
  uint32_t peer_addr;
  uint16_t peer_port;
  uint32_t virtual_addr;
  uint16_t virtual_port;
  uint32_t owner_addr;
  uint16_t owner_port;
  uint8_t owner_mac[6];
  uint8_t lock;
} __attribute__((packed)) psw_add_req_t;

typedef struct psw_chown_req {
  uint8_t type;
  uint16_t status;
  uint32_t peer_addr;
  uint16_t peer_port;
  uint32_t owner_addr;
  uint16_t owner_port;
  uint8_t owner_mac[6];
  uint8_t unlock;
} __attribute__((packed)) psw_chown_req_t;

typedef struct psw_delete_req {
  uint8_t type;
  uint16_t status;
  uint32_t peer_addr;
  uint16_t peer_port;
} __attribute__((packed)) psw_delete_req_t;

typedef struct psw_lock_req {
  uint8_t type;
  uint16_t status;
  uint32_t peer_addr;
  uint16_t peer_port;
} __attribute__((packed)) psw_lock_req_t;

typedef void (*psw_config_cb)(struct psw_req_base *req, void *data);

extern prism_switch_client_t *prism_switch_client_create(uv_loop_t *loop,
                                                         const char *host);
extern int prism_switch_client_queue_task(prism_switch_client_t *client,
                                          struct psw_req_base *req,
                                          psw_config_cb cb, void *data);
extern void prism_switch_client_destroy(prism_switch_client_t *client);
