#pragma once

#include <prism_switch/prism_switch_client.h>
#include <uv.h>

enum psw_client_req_type {
  PSW_REQ_ADD,
  PSW_REQ_CHANGE_OWNER,
  PSW_REQ_DELETE,
  PSW_REQ_LOCK,
  PSW_REQ_UNLOCK
};

typedef struct uv_psw_client_work_s {
  uv_work_t super;
  int status;
  enum psw_client_req_type req_type;
  prism_switch_client_t *client;
  union {
    struct prism_switch_add_req add_req;
    struct prism_switch_delete_req delete_req;
    struct prism_switch_change_owner_req change_owner_req;
    struct prism_switch_lock_req lock_req;
  };
} uv_psw_client_work_t;

void uv_psw_client_work(uv_work_t *work);
