#pragma once

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

typedef struct {
  uint32_t addr;
  uint32_t port;
} prism_key_t;

typedef struct {
  uint32_t virtual_addr;
  uint16_t virtual_port;
  uint32_t owner_addr;
  uint16_t owner_port;
  uint8_t owner_mac[6];
  uint8_t locked;
  uint8_t _pad;
} prism_value_t;

struct l2_key {
  uint8_t dst[6];
} __attribute__((packed));

typedef struct l2_key l2_key_t;
