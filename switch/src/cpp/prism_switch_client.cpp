#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <netinet/ip.h>
#include <sys/time.h>

#include <prism_switch/prism_switch_client.h>

struct psw_config_req {
  uv_udp_t super;
  uv_buf_t sbuf;
  psw_config_cb user_cb;
  void *user_data;
  uv_timer_t retry_timer;
  uint32_t retry_count;
  prism_switch_client_t *client;
};

struct prism_switch_client_s {
  uv_loop_t *loop;
  struct sockaddr_in sw_addr;
};

static std::vector<std::string>
split(std::string str, char del)
{
  int first = 0;
  int last = str.find_first_of(del);

  std::vector<std::string> result;

  while ((size_t)first < str.size()) {
    std::string subStr(str, first, last - first);

    result.push_back(subStr);

    first = last + 1;
    last = str.find_first_of(del, first);

    if ((size_t)last == std::string::npos) {
      last = str.size();
    }
  }

  return result;
}

static void
on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
  buf->base = (char *)malloc(1024);
  assert(buf->base != NULL);
  buf->len = 1024;
}

static void
on_recv(uv_udp_t *_client, ssize_t nread, const uv_buf_t *buf,
        const struct sockaddr *addr, unsigned flags)
{
  int error;
  struct psw_config_req *req = (struct psw_config_req *)_client;
  struct psw_req_base *prb;

  assert(flags != UV_UDP_PARTIAL);
  assert(nread > 0);

  prb = (struct psw_req_base *)buf->base;

  if (req->user_cb) {
    req->user_cb(prb, req->user_data);
  }

  error = uv_timer_stop(&req->retry_timer);
  assert(error == 0);

  free(req->sbuf.base);

  uv_close((uv_handle_t *)&req->retry_timer, NULL);
  uv_close((uv_handle_t *)req, (uv_close_cb)free);

  free(buf->base);
}

prism_switch_client_t *
prism_switch_client_create(uv_loop_t *loop, const char *host)
{
  int error;
  prism_switch_client_t *client =
      (prism_switch_client_t *)malloc(sizeof(*client));
  assert(client != NULL);

  auto spl_host = split(host, ':');
  client->sw_addr.sin_family = AF_INET;
  client->sw_addr.sin_addr.s_addr = inet_addr(spl_host[0].c_str());
  client->sw_addr.sin_port = htons((uint16_t)atoi(spl_host[1].c_str()));

  client->loop = loop;

  return client;
}

static void
after_send(uv_udp_send_t *req, int status)
{
  assert(status == 0);
  free(req);
}

static void
retry_send(uv_timer_t *timer)
{
  int error;
  struct psw_config_req *conf_req = (struct psw_config_req *)timer->data;
  prism_switch_client_t *client = (prism_switch_client_t*)conf_req->client;

  uv_udp_send_t *req = (uv_udp_send_t *)malloc(sizeof(*req));
  assert(req != NULL);

  error = uv_udp_send(req, &conf_req->super, &conf_req->sbuf,
      1, (const struct sockaddr *)&client->sw_addr, after_send);
  assert(error == 0);
  conf_req->retry_count++;
}

int
prism_switch_client_queue_task(prism_switch_client_t *client,
                               struct psw_req_base *req, psw_config_cb cb,
                               void *data)
{
  int error;
  struct psw_config_req *conf_req =
      (struct psw_config_req *)malloc(sizeof(*conf_req));
  assert(conf_req != NULL);

  switch (req->type) {
  case PSW_REQ_ADD:
    conf_req->sbuf.len = sizeof(psw_add_req_t);
    break;
  case PSW_REQ_DELETE:
    conf_req->sbuf.len = sizeof(psw_delete_req_t);
    break;
  case PSW_REQ_CHOWN:
    conf_req->sbuf.len = sizeof(psw_chown_req_t);
    break;
  case PSW_REQ_LOCK:
    conf_req->sbuf.len = sizeof(psw_lock_req_t);
    break;
  case PSW_REQ_UNLOCK:
    conf_req->sbuf.len = sizeof(psw_lock_req_t);
    break;
  default:
    return -EINVAL;
  }

  error = uv_udp_init(client->loop, &conf_req->super);
  assert(error == 0);

  struct sockaddr_in baddr;
  baddr.sin_family = AF_INET;
  baddr.sin_addr.s_addr = 0;
  baddr.sin_port = 0;
  error = uv_udp_bind(&conf_req->super, (struct sockaddr *)&baddr, 0);
  assert(error == 0);

  error = uv_udp_recv_start(&conf_req->super, on_alloc, on_recv);
  assert(error == 0);

  conf_req->sbuf.base = (char *)malloc(conf_req->sbuf.len);
  assert(conf_req->sbuf.base != NULL);
  memcpy(conf_req->sbuf.base, req, conf_req->sbuf.len);
  conf_req->user_cb = cb;
  conf_req->user_data = data;
  uv_timer_t *retry_timer = &conf_req->retry_timer;
  error = uv_timer_init(client->loop, retry_timer);
  assert(error == 0);
  error = uv_timer_start(retry_timer, retry_send, 100, 100);
  assert(error == 0);

  conf_req->retry_count = 0;
  conf_req->client = client;

  retry_timer->data = conf_req;

  uv_udp_send_t *sreq = (uv_udp_send_t *)malloc(sizeof(*sreq));
  assert(sreq != NULL);

  return uv_udp_send(sreq, &conf_req->super, &conf_req->sbuf, 1,
                     (const struct sockaddr *)&client->sw_addr, after_send);
}

void
prism_switch_client_destroy(prism_switch_client_t *client)
{
  free(client);
}
