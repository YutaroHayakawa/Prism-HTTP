#include <assert.h>
#include <errno.h>
#include <linux/sockios.h>
#include <linux/tcp.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <tcp_export.h>

#ifndef TCP_ESTABLISHED
#define TCP_ESTABLISHED 1
#endif

struct tcp_info_sub {
  uint8_t tcpi_state;
  uint8_t tcpi_ca_state;
  uint8_t tcpi_retransmits;
  uint8_t tcpi_probes;
  uint8_t tcpi_backoff;
  uint8_t tcpi_options;
  uint8_t tcpi_snd_wscale : 4;
  uint8_t tcpi_rcv_wscale : 4;
};

static int
tcp_repair_start(int sock)
{
  int error, opt = 1;
  error = setsockopt(sock, IPPROTO_TCP, TCP_REPAIR, &opt, sizeof(opt));
  if (error) {
    return errno;
  }
  return 0;
}

static int
tcp_repair_done(int sock)
{
  int error, opt = -1;
  error = setsockopt(sock, IPPROTO_TCP, TCP_REPAIR, &opt, sizeof(opt));
  if (error) {
    return errno;
  }
  return 0;
}

static int
tcp_is_established(int sock, struct tcp_info_sub *info)
{
  int error;
  socklen_t opt_len = sizeof(*info);

  error = getsockopt(sock, IPPROTO_TCP, TCP_INFO, info, &opt_len);
  if (error == -1 || opt_len != sizeof(*info)) {
    return errno;
  }

  if (info->tcpi_state != TCP_ESTABLISHED) {
    return EINVAL;
  }

  return 0;
}

static int
tcp_get_queue_len(int sock, prism::TCPState *ex)
{
  int error, size;

  error = ioctl(sock, SIOCOUTQ, &size);
  if (error == -1) {
    return errno;
  }

  ex->set_sendq_len(size);

  error = ioctl(sock, SIOCOUTQNSD, &size);
  if (error == -1) {
    return errno;
  }

  ex->set_unsentq_len(size);

  error = ioctl(sock, SIOCINQ, &size);
  if (error == -1) {
    return errno;
  }

  ex->set_recvq_len(size);

  return 0;
}

static int
tcp_get_options(int sock, prism::TCPState *ex, struct tcp_info_sub *info)
{
  int error;
  uint32_t mss;
  uint32_t timestamp;
  socklen_t opt_len = sizeof(mss);

  error = getsockopt(sock, IPPROTO_TCP, TCP_MAXSEG, &mss, &opt_len);
  if (error == -1) {
    return errno;
  }

  ex->set_mss(mss);
  ex->set_send_wscale(info->tcpi_snd_wscale);
  ex->set_recv_wscale(info->tcpi_rcv_wscale);

  opt_len = sizeof(timestamp);
  error = getsockopt(sock, IPPROTO_TCP, TCP_TIMESTAMP, &timestamp, &opt_len);
  if (error == -1) {
    return errno;
  }

  ex->set_timestamp(timestamp);

  return 0;
}

#ifndef TCPOPT_MSS
#define TCPOPT_MSS 2
#endif

#ifndef TCPOPT_WINDOW
#define TCPOPT_WINDOW 3
#endif

#ifndef TCPOPT_SACK_PERM
#define TCPOPT_SACK_PERM 4
#endif

#ifndef TCPOPT_TIMESTAMP
#define TCPOPT_TIMESTAMP 8
#endif

static int
tcp_set_options(int sock, const prism::TCPState *ex)
{
  int error;
  struct tcp_repair_opt opts[4];

  opts[0].opt_code = TCPOPT_SACK_PERM;
  opts[0].opt_val = 0;
  opts[1].opt_code = TCPOPT_WINDOW;
  opts[1].opt_val = ex->send_wscale() + (ex->recv_wscale() << 16);
  opts[2].opt_code = TCPOPT_TIMESTAMP;
  opts[2].opt_val = 0;
  opts[3].opt_code = TCPOPT_MSS;
  opts[3].opt_val = ex->mss();

  error = setsockopt(sock, IPPROTO_TCP, TCP_REPAIR_OPTIONS, opts,
                     sizeof(struct tcp_repair_opt) * 4);
  if (error == -1) {
    return errno;
  }

  uint32_t tstamp = ex->timestamp();
  error = setsockopt(sock, IPPROTO_TCP, TCP_TIMESTAMP, &tstamp, sizeof(tstamp));
  if (error == -1) {
    return errno;
  }

  return 0;
}

static int
tcp_get_window(int sock, prism::TCPState *ex)
{
  int error;
  struct tcp_repair_window window;
  socklen_t slen = sizeof(struct tcp_repair_window);

  error = getsockopt(sock, IPPROTO_TCP, TCP_REPAIR_WINDOW, &window, &slen);
  if (error) {
    return errno;
  }

  ex->set_snd_wl1(window.snd_wl1);
  ex->set_snd_wnd(window.snd_wnd);
  ex->set_max_window(window.max_window);
  ex->set_rcv_wnd(window.rcv_wnd);
  ex->set_rcv_wup(window.rcv_wup);

  return 0;
}

static int
tcp_set_window(int sock, const prism::TCPState *ex)
{
  int error;
  struct tcp_repair_window window;

  window.snd_wl1 = ex->snd_wl1();
  window.snd_wnd = ex->snd_wnd();
  window.max_window = ex->max_window();
  window.rcv_wnd = ex->rcv_wnd();
  window.rcv_wup = ex->rcv_wup();
  error =
      setsockopt(sock, IPPROTO_TCP, TCP_REPAIR_WINDOW, &window, sizeof(window));
  if (error) {
    return errno;
  }

  return 0;
}

static int
tcp_get_addr(int sock, prism::TCPState *ex)
{
  int error;
  struct sockaddr_in addr;
  socklen_t addr_len;

  addr_len = sizeof(addr);
  error = getsockname(sock, (struct sockaddr *)&addr, &addr_len);
  if (error == -1) {
    return errno;
  }

  ex->set_self_addr(addr.sin_addr.s_addr);
  ex->set_self_port(addr.sin_port);

  error = getpeername(sock, (struct sockaddr *)&addr, &addr_len);
  if (error == -1) {
    return errno;
  }

  ex->set_peer_addr(addr.sin_addr.s_addr);
  ex->set_peer_port(addr.sin_port);

  return 0;
}

static int
tcp_set_addr(int sock, const prism::TCPState *ex)
{
  int error;
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = ex->self_addr();
  addr.sin_port = (uint16_t)ex->self_port();
  error = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
  if (error) {
    return errno;
  }

  addr.sin_addr.s_addr = ex->peer_addr();
  addr.sin_port = (uint16_t)ex->peer_port();
  error = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
  if (error) {
    return errno;
  }

  return 0;
}

static int
tcp_get_queue(int sock, int qid, prism::TCPState *ex)
{
  int error;
  ssize_t ret;
  uint8_t *buf;
  uint32_t seq;
  socklen_t opt_len;
  uint64_t len;

  error = setsockopt(sock, IPPROTO_TCP, TCP_REPAIR_QUEUE, &qid, sizeof(int));
  if (error == -1) {
    return errno;
  }

  opt_len = sizeof(uint32_t);
  error = getsockopt(sock, IPPROTO_TCP, TCP_QUEUE_SEQ, &seq, &opt_len);
  if (error == -1) {
    return errno;
  }

  if (qid == TCP_SEND_QUEUE) {
    ex->set_seq(seq);
    len = ex->sendq_len();
  } else {
    ex->set_ack(seq);
    len = ex->recvq_len();
  }

  if (len) {
    buf = (uint8_t *)malloc(len + 1);
    if (!buf) {
      return ENOMEM;
    }

    ret = recv(sock, buf, len + 1, MSG_PEEK | MSG_DONTWAIT);
    if (ret < 0 || (uint64_t)ret != len) {
      return EINVAL;
    }
  } else {
    buf = NULL;
  }

  if (qid == TCP_SEND_QUEUE) {
    ex->set_sendq(buf, len);
  } else {
    ex->set_recvq(buf, len);
  }

  return 0;
}

/*
 * Took from CRIU
 * (https://github.com/checkpoint-restore/criu/blob/criu-dev/soccr/soccr.c)
 */
static int
__send_queue(int sock, int queue, const uint8_t *buf, uint32_t len)
{
  int ret, error, max_chunk;
  int off;

  max_chunk = len;
  off = 0;

  do {
    int chunk = len;

    if (chunk > max_chunk)
      chunk = max_chunk;

    ret = send(sock, buf + off, chunk, 0);
    if (ret <= 0) {
      if (max_chunk > 1024) {
        /*
         * Kernel not only refuses the whole chunk,
         * but refuses to split it into pieces too.
         *
         * When restoring recv queue in repair mode
         * kernel doesn't try hard and just allocates
         * a linear skb with the size we pass to the
         * system call. Thus, if the size is too big
         * for slab allocator, the send just fails
         * with ENOMEM.
         *
         * In any case -- try smaller chunk, hopefully
         * there's still enough memory in the system.
         */
        max_chunk >>= 1;
        continue;
      }

      error = errno;
      goto err0;
    }
    off += ret;
    len -= ret;
  } while (len);

  error = 0;
err0:
  return error;
}

static int
send_queue(int sock, int queue, const uint8_t *buf, uint32_t len)
{
  int error;

  error =
      setsockopt(sock, IPPROTO_TCP, TCP_REPAIR_QUEUE, &queue, sizeof(queue));
  if (error == -1) {
    return errno;
  }

  return __send_queue(sock, queue, buf, len);
}

static int
tcp_set_queue(int sock, int queue, const prism::TCPState *ex)
{
  int error;

  if (queue == TCP_RECV_QUEUE) {
    if (ex->recvq_len() == 0) {
      return 0;
    }

    return send_queue(sock, TCP_RECV_QUEUE,
                      (const uint8_t *)ex->recvq().c_str(), ex->recvq_len());
  }

  if (queue == TCP_SEND_QUEUE) {
    uint32_t len, ulen;

    /*
     * All data in a write buffer can be divided on two parts sent
     * but not yet acknowledged data and unsent data.
     * The TCP stack must know which data have been sent, because
     * acknowledgment can be received for them. These data must be
     * restored in repair mode.
     */
    ulen = ex->unsentq_len();
    len = ex->sendq_len() - ulen;
    if (len) {
      error = send_queue(sock, TCP_SEND_QUEUE,
                         (const uint8_t *)ex->sendq().c_str(), len);
      if (error) {
        return error;
      }
    }

    if (ulen) {
      /*
       * The second part of data have never been sent to outside, so
       * they can be restored without any tricks.
       */
      error = tcp_repair_done(sock);
      assert(error == 0);

      error = __send_queue(sock, TCP_SEND_QUEUE,
                           (const uint8_t *)ex->sendq().c_str() + len, ulen);
      if (error) {
        return error;
      }

      error = tcp_repair_start(sock);
      assert(error == 0);
    }

    return 0;
  }

  return EINVAL;
}

static int
tcp_set_seq(int sock, int queue, const prism::TCPState *ex)
{
  int error;
  uint32_t seq;

  if (queue == TCP_SEND_QUEUE) {
    seq = ex->seq();
  } else {
    seq = ex->ack();
  }

  error =
      setsockopt(sock, IPPROTO_TCP, TCP_REPAIR_QUEUE, &queue, sizeof(queue));
  if (error == -1) {
    return errno;
  }

  error = setsockopt(sock, IPPROTO_TCP, TCP_QUEUE_SEQ, &seq, sizeof(seq));
  if (error == -1) {
    return errno;
  }

  return 0;
}

int
tcp_export(int sock, prism::TCPState *ex)
{
  int error;
  struct tcp_info_sub info;

  if (ex == NULL) {
    return EINVAL;
  }

  ex->Clear();

#define TRY(_funccall, _label)                                                 \
  if ((error = _funccall) != 0) {                                              \
    goto _label;                                                               \
  }

  TRY(tcp_repair_start(sock), err0);
  TRY(tcp_is_established(sock, &info), err1);
  TRY(tcp_get_queue_len(sock, ex), err1);
  TRY(tcp_get_options(sock, ex, &info), err1);
  TRY(tcp_get_window(sock, ex), err1);
  TRY(tcp_get_addr(sock, ex), err1);
  TRY(tcp_get_queue(sock, TCP_SEND_QUEUE, ex), err1);
  TRY(tcp_get_queue(sock, TCP_RECV_QUEUE, ex), err2);

#undef TRY

  return 0;

err2:
  free((uint8_t *)ex->mutable_sendq()->c_str());
err1:
  assert(tcp_repair_done(sock) == 0);
err0:
  return error;
}

int
tcp_import(int sock, const prism::TCPState *ex)
{
  int error;

  if (ex == NULL) {
    return EINVAL;
  }

#define TRY(_funccall, _label)                                                 \
  if ((error = _funccall) != 0) {                                              \
    goto _label;                                                               \
  }

  TRY(tcp_repair_start(sock), err0);
  TRY(tcp_set_seq(sock, TCP_SEND_QUEUE, ex), err1);
  TRY(tcp_set_seq(sock, TCP_RECV_QUEUE, ex), err1);
  TRY(tcp_set_addr(sock, ex), err1);
  TRY(tcp_set_queue(sock, TCP_SEND_QUEUE, ex), err1);
  TRY(tcp_set_queue(sock, TCP_RECV_QUEUE, ex), err1);
  TRY(tcp_set_options(sock, ex), err1);
  TRY(tcp_set_window(sock, ex), err1);
  TRY(tcp_repair_done(sock), err1);

#undef TRY

  return 0;

err1:
  assert(tcp_repair_done(sock) == 0);
err0:
  return error;
}
