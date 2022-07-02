#include <uv_psw_client.h>

void
uv_psw_client_work(uv_work_t *work)
{
  int error;
  uv_psw_client_work_t *w = (uv_psw_client_work_t *)work;

  switch (w->req_type) {
  case PSW_REQ_ADD:
    error = prism_switch_add(w->client, &w->add_req);
    break;
  case PSW_REQ_CHANGE_OWNER:
    error = prism_switch_change_owner(w->client, &w->change_owner_req);
    break;
  case PSW_REQ_DELETE:
    error = prism_switch_delete(w->client, &w->delete_req);
    break;
  case PSW_REQ_LOCK:
    error = prism_switch_lock(w->client, &w->lock_req);
    break;
  case PSW_REQ_UNLOCK:
    error = prism_switch_unlock(w->client, &w->lock_req);
    break;
  default:
    error = -EINVAL;
    break;
  }

  w->status = error;
}
