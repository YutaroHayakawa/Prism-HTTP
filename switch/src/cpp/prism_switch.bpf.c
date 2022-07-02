#pragma once

#include <vale_bpf_native/vale_bpf_native_api.h>
#include <prism_switch/prism_switch.h>

#define ENOENT 2
#define EBUSY 16
#define EEXIST 17
#define EINVAL 22

struct eth {
  uint8_t dst[6];
  uint8_t src[6];
  uint16_t type;
};

#define ETH_P_IP 0x0800

struct ip {
  uint16_t hl:4;
  uint16_t v:4;
  uint8_t tos;
  uint16_t len;
  uint16_t id;
  uint16_t off;
  uint8_t ttl;
  uint8_t proto;
  uint16_t csum;
  uint32_t src;
  uint32_t dst;
};

#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_ICMP 1

struct tcp {
  uint16_t src;
  uint16_t dst;
  uint32_t seq;
  uint32_t ack_seq;
  uint16_t res1:4;
  uint16_t doff:4;
  uint16_t fin:1;
  uint16_t syn:1;
  uint16_t rst:1;
  uint16_t psh:1;
  uint16_t ack:1;
  uint16_t urg:1;
  uint16_t res2:2;
  uint16_t window;
  uint16_t csum;
  uint16_t urg_ptr;
};

struct udp {
  uint16_t src;
  uint16_t dst;
  uint16_t len;
  uint16_t csum;
};

struct prism_switch_headers {
  struct eth *eth;
  struct ip *ip;
  union {
    struct tcp *tcp;
    struct udp *udp;
  };
  struct psw_req_base *prb;
};

struct prism_switch_metadata {
  uint8_t *data;
  uint8_t *data_end;
  uint8_t *cur;
  uint32_t sport;
  uint32_t sring;
  uint32_t dport;
  uint8_t matched;
  uint8_t abort;
};

BPF_TABLE("hash", prism_key_t, prism_value_t, prism, 2048);
BPF_TABLE("hash", l2_key_t, uint32_t, l2, 2048);

static __attribute__((always_inline)) uint16_t
csum16_add(uint16_t csum, uint16_t addend)
{
  uint16_t ret = csum + addend;
  return ret + (ret < addend);
}

static __attribute__((always_inline)) uint16_t
csum16_sub(uint16_t csum, uint16_t addend)
{
  return csum16_add(csum, ~addend);
}

static __attribute__((always_inline)) void
config_prepare_response(struct prism_switch_metadata *metadata,
    struct prism_switch_headers *headers, int status)
{
  uint32_t tmp_ip;
  uint16_t tmp_port;
  uint8_t tmp_mac;

  if (status > 0) {
    metadata->abort = 1;
    return;
  }

  /*
   * Swap all addresses
   */

  #pragma unroll
  for (int i = 0; i < 6; i++) {
    tmp_mac = headers->eth->src[i];
    headers->eth->src[i] = headers->eth->dst[i];
    headers->eth->dst[i] = tmp_mac;
  }

  tmp_ip = headers->ip->src;
  headers->ip->src = headers->ip->dst;
  headers->ip->dst = tmp_ip;

  tmp_port = headers->udp->src;
  headers->udp->src = headers->udp->dst;
  headers->udp->dst = tmp_port;

  uint16_t udp_csum;
  udp_csum = csum16_sub(headers->udp->csum, ~(headers->prb->status));
  udp_csum = csum16_add(udp_csum, ~((uint16_t)-status));
  headers->udp->csum = udp_csum;
}

static __attribute__((always_inline)) int
config_handle_add_req(struct prism_switch_metadata *metadata,
    struct prism_switch_headers *headers)
{
  int error;
  struct psw_add_req *par = (struct psw_add_req *)metadata->cur;
  if (!((metadata->cur + sizeof(*par) <= metadata->data_end))) {
    return -EINVAL;
  }

  prism_key_t key = {0};
  key.addr = par->peer_addr;
  key.port = par->peer_port;

  prism_value_t *val = prism.lookup(&key);
  if (val != NULL) {
    return -EEXIST;
  }

  prism_value_t new_val = {0};
  new_val.virtual_addr = par->virtual_addr;
  new_val.virtual_port = par->virtual_port;
  new_val.owner_addr = par->owner_addr;
  new_val.owner_port = par->owner_port;
  __builtin_memcpy(new_val.owner_mac, par->owner_mac, 6);
  new_val.locked = par->lock;

  /*
   * Currently just abort when error occurs
   */
  error = prism.update(&key, &new_val);
  if (error) {
    return error;
  }

  return 0;
}

static __attribute__((always_inline)) int
config_handle_delete_req(struct prism_switch_metadata *metadata,
    struct prism_switch_headers *headers)
{
  int error;
  struct psw_delete_req *pdr = (struct psw_delete_req *)metadata->cur;
  if (!((metadata->cur + sizeof(*pdr) <= metadata->data_end))) {
    return -EINVAL;
  }

  prism_key_t key = {0};
  key.addr = pdr->peer_addr;
  key.port = pdr->peer_port;

  error = prism.delete(&key);
  if (error) {
    return error;
  }

  return 0;
}

static __attribute__((always_inline)) int
config_handle_chown_req(struct prism_switch_metadata *metadata,
    struct prism_switch_headers *headers)
{
  int error;
  struct psw_chown_req *pcr = (struct psw_chown_req *)metadata->cur;
  if (!((metadata->cur + sizeof(*pcr) <= metadata->data_end))) {
    metadata->abort = 1;
    return -EINVAL;
  }

  prism_key_t key = {0};
  key.addr = pcr->peer_addr;
  key.port = pcr->peer_port;

  prism_value_t *val = prism.lookup(&key);
  if (val == NULL) {
    return -ENOENT;
  }

  prism_value_t new_val;
  __builtin_memcpy(&new_val, val, sizeof(*val));
  new_val.owner_addr = pcr->owner_addr;
  new_val.owner_port = pcr->owner_port;
  __builtin_memcpy(new_val.owner_mac, pcr->owner_mac, 6);
  if (pcr->unlock) {
    new_val.locked = new_val.locked == 1 ? 0 : 1;
  }

  error = prism.update(&key, &new_val);
  if (error) {
    return error;
  }

  return 0;
}

static __attribute__((always_inline)) int
config_handle_lock_req(struct prism_switch_metadata *metadata,
    struct prism_switch_headers *headers)
{
  int error;
  struct psw_lock_req *plr = (struct psw_lock_req *)metadata->cur;
  if (!((metadata->cur + sizeof(*plr) <= metadata->data_end))) {
    metadata->abort = 1;
    return -EINVAL;
  }

  prism_key_t key = {0};
  key.addr = plr->peer_addr;
  key.port = plr->peer_port;

  prism_value_t *val = prism.lookup(&key);
  if (val == NULL) {
    return -ENOENT;
  }

  /*
   * TODO: We should use XADD for better performance
   */
  prism_value_t new_val;
  __builtin_memcpy(&new_val, val, sizeof(*val));
  new_val.locked = new_val.locked == 0 ? 1 : 0;

  error = prism.update(&key, &new_val);
  if (error) {
    return error;
  }

  return 0;
}

static __attribute__((always_inline)) int
config_handle_unlock_req(struct prism_switch_metadata *metadata,
    struct prism_switch_headers *headers)
{
  int error;
  struct psw_lock_req *pur = (struct psw_lock_req *)metadata->cur;
  if (!((metadata->cur + sizeof(*pur) <= metadata->data_end))) {
    metadata->abort = 1;
    return -EINVAL;
  }

  prism_key_t key = {0};
  key.addr = pur->peer_addr;
  key.port = pur->peer_port;

  prism_value_t *val = prism.lookup(&key);
  if (val == NULL) {
    return -ENOENT;
  }

  /*
   * TODO: We should use XADD for better performance
   */
  prism_value_t new_val;
  __builtin_memcpy(&new_val, val, sizeof(*val));
  new_val.locked = new_val.locked == 1 ? 0 : 1;

  error = prism.update(&key, &new_val);
  if (error) {
    return error;
  }

  return 0;
}

static __attribute__((always_inline)) void
configure_switch(struct prism_switch_metadata *metadata,
    struct prism_switch_headers *headers)
{
  int error;
  struct psw_req_base *prb = (struct psw_req_base *)metadata->cur;
  if (!((metadata->cur + sizeof(*prb) <= metadata->data_end))) {
    metadata->abort = 1;
    return;
  }

  headers->prb = prb;

  switch (prb->type) {
    case PSW_REQ_ADD:
#ifdef DEBUG
      bpf_trace_printk("ADD\n");
#endif
      error = config_handle_add_req(metadata, headers);
      break;
    case PSW_REQ_DELETE:
#ifdef DEBUG
      bpf_trace_printk("DELETE\n");
#endif
      error = config_handle_delete_req(metadata, headers);
      break;
    case PSW_REQ_CHOWN:
#ifdef DEBUG
      bpf_trace_printk("CHOWN\n");
#endif
      error = config_handle_chown_req(metadata, headers);
      break;
    case PSW_REQ_LOCK:
#ifdef DEBUG
      bpf_trace_printk("LOCK\n");
#endif
      error = config_handle_lock_req(metadata, headers);
      break;
    case PSW_REQ_UNLOCK:
#ifdef DEBUG
      bpf_trace_printk("UNLOCK\n");
#endif
      error = config_handle_unlock_req(metadata, headers);
      break;
    default:
      metadata->abort = 1;
      return;
  }

  config_prepare_response(metadata, headers, error);
}

static __attribute__((always_inline)) void
prism_out_lookup(struct prism_switch_metadata *metadata,
    struct prism_switch_headers *headers)
{
  prism_key_t key;
  key.addr = headers->ip->dst;
  key.port = headers->tcp->dst;

  prism_value_t *val = prism.lookup(&key);
  if (val == NULL) {
    return;
  }

  if (val->locked) {
    metadata->abort = 1;
    return;
  }

  uint16_t ip_csum;
  ip_csum = csum16_sub(headers->ip->csum, ~(headers->ip->src >> 16));
  ip_csum = csum16_sub(ip_csum, ~(headers->ip->src & 0xffff));
  ip_csum = csum16_add(ip_csum, ~(val->virtual_addr >> 16));
  ip_csum = csum16_add(ip_csum, ~(val->virtual_addr & 0xffff));
  headers->ip->csum = ip_csum;

  uint16_t tcp_csum;
  tcp_csum = csum16_sub(headers->tcp->csum, ~(headers->ip->src >> 16));
  tcp_csum = csum16_sub(tcp_csum, ~(headers->ip->src & 0xffff));
  tcp_csum = csum16_add(tcp_csum, ~(val->virtual_addr >> 16));
  tcp_csum = csum16_add(tcp_csum, ~(val->virtual_addr & 0xffff));
  tcp_csum = csum16_sub(tcp_csum, ~(headers->tcp->src));
  tcp_csum = csum16_add(tcp_csum, ~(val->virtual_port));
  headers->tcp->csum = tcp_csum;

  headers->tcp->src = val->virtual_port;
  headers->ip->src = val->virtual_addr;

  metadata->matched = 1;
}

static __attribute__((always_inline)) void
prism_in_lookup(struct prism_switch_metadata *metadata,
    struct prism_switch_headers *headers)
{
  prism_key_t key;
  key.addr = headers->ip->src;
  key.port = headers->tcp->src;

  prism_value_t *val = prism.lookup(&key);
  if (val == NULL) {
    return;
  }

  if (val->locked == 1) {
    metadata->abort = 1;
    return;
  }

  // Rewrite destination MAC address
  headers->eth->dst[0] = val->owner_mac[0];
  headers->eth->dst[1] = val->owner_mac[1];
  headers->eth->dst[2] = val->owner_mac[2];
  headers->eth->dst[3] = val->owner_mac[3];
  headers->eth->dst[4] = val->owner_mac[4];
  headers->eth->dst[5] = val->owner_mac[5];
  

  // Rewrite destination IP address
  uint16_t ip_csum;
  ip_csum = csum16_sub(headers->ip->csum, ~(headers->ip->dst >> 16));
  ip_csum = csum16_sub(ip_csum, ~(headers->ip->dst & 0xffff));
  ip_csum = csum16_add(ip_csum, ~(val->owner_addr >> 16));
  ip_csum = csum16_add(ip_csum, ~(val->owner_addr & 0xffff));
  headers->ip->csum = ip_csum;

  // Rewrite destination TCP port
  uint16_t tcp_csum;
  tcp_csum = csum16_sub(headers->tcp->csum, ~(headers->ip->dst >> 16));
  tcp_csum = csum16_sub(tcp_csum, ~(headers->ip->dst & 0xffff));
  tcp_csum = csum16_add(tcp_csum, ~(val->owner_addr >> 16));
  tcp_csum = csum16_add(tcp_csum, ~(val->owner_addr & 0xffff));
  tcp_csum = csum16_sub(tcp_csum, ~(headers->tcp->dst));
  tcp_csum = csum16_add(tcp_csum, ~(val->owner_port));
  headers->tcp->csum = tcp_csum;

  headers->ip->dst = val->owner_addr;
  headers->tcp->dst = val->owner_port;

  metadata->matched = 1;
}

static __attribute__((always_inline)) void
l2_lookup(struct prism_switch_metadata *metadata,
    struct prism_switch_headers *headers)
{
  if ((headers->eth->dst[0] & 1) != 0) {
    metadata->dport = VALE_BPF_BROADCAST;
    return;
  }

  int error = 0;
  uint32_t *sport = l2.lookup((l2_key_t *)headers->eth->src);
  if (sport == NULL) {
    error = l2.update((l2_key_t *)headers->eth->src, &metadata->sport);
  } else {
    if (*sport != metadata->sport) {
      *sport = metadata->sport;
    }
  }

  if (error) {
    metadata->dport = VALE_BPF_DROP;
    return;
  }

  uint32_t *dport = l2.lookup((l2_key_t *)headers->eth->dst);
  if (dport == NULL) {
    metadata->dport = VALE_BPF_BROADCAST;
    return;
  }

  metadata->dport = *dport;
}

uint32_t
vale_lookup(struct xdp_md *md)
{
  struct prism_switch_headers headers;
  struct prism_switch_metadata metadata;
  uint8_t *data = (uint8_t *)(long)md->data;
  uint8_t *data_end = (uint8_t *)(long)md->data_end;

  metadata.data = data;
  metadata.data_end = data_end;
  metadata.sport = md->ingress_ifindex;
  metadata.sring = md->rx_queue_index;
  metadata.cur = data;
  metadata.dport = VALE_BPF_DROP;
  metadata.matched = 0;
  metadata.abort = 0;

  headers.eth = NULL;
  headers.ip = NULL;
  headers.tcp = NULL;

  /*
   * Parsers
   */

  // Parse Ethernet
  headers.eth = (struct eth *)metadata.cur;
  if (!((metadata.cur + sizeof(struct eth) <= data_end))) {
    return VALE_BPF_DROP;
  }

  if (bpf_ntohs(headers.eth->type) != ETH_P_IP) {
    goto l2; // fallback to l2 switch
  }
  metadata.cur += sizeof(struct eth);

  //Parse IPv4
  headers.ip = (struct ip *)metadata.cur;
  if (!((metadata.cur + sizeof(struct ip)) <= data_end)) {
    goto l2; // fallback to l2 switch
  }

  if (headers.ip->hl != 5) {
    goto l2; // fallback to l2 switch
  }
  metadata.cur += sizeof(struct ip);

  /*
   * Possibly switch configuration packet
   */
  if (headers.ip->proto == IPPROTO_UDP) {
    headers.udp = (struct udp *)metadata.cur;
    if (!((metadata.cur + sizeof(struct udp)) <= data_end)) {
      goto l2;
    }
    metadata.cur += sizeof(struct udp);

    if (headers.ip->dst != CONFIG_ADDR ||
        headers.udp->dst != CONFIG_PORT) {
      goto l2;
    }

    /*
    if (!udp_csum_ok(metadata.headers)) {
      return VALE_BPF_DROP;
    }
    */

    configure_switch(&metadata, &headers);
    if (metadata.abort == 1) {
      return VALE_BPF_DROP;
    }

    return metadata.sport;
  }

  /*
   * Prism logic
   */
  if (headers.ip->proto == IPPROTO_TCP) {
    headers.tcp = (struct tcp *)metadata.cur;
    if (!((metadata.cur + sizeof(struct tcp)) <= data_end)) {
      goto l2;
    }
    metadata.cur += sizeof(struct tcp);

    /*
     * Table lookups
     */
    prism_out_lookup(&metadata, &headers);
    if (metadata.matched == 1) {
      goto l2;
    }

    if (metadata.abort == 1) {
      return VALE_BPF_DROP;
    }

    prism_in_lookup(&metadata, &headers);
    if (metadata.abort == 1) {
      return VALE_BPF_DROP;
    }
  }

l2:
  l2_lookup(&metadata, &headers);

  return metadata.dport;
}
