/*
 * Copyright 2017 Yutaro Hayakawa
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#include <linux/bpf.h>
#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <bsd_glue.h>

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <dev/netmap/netmap_bdg.h>

#include <vale_bpf_native/vale_bpf_native.h>

struct eth {
  uint8_t dst[6];
  uint8_t src[6];
  uint16_t type;
};

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

static struct bpf_prog *prog = NULL;

/*
 * Dummy device just for passing dev->ifindex as
 * a ingress vale port
 */
static struct net_device *dummy_dev;

static uint32_t
vale_bpf_native_lookup(struct nm_bdg_fwd *ft, uint8_t *hint,
    struct netmap_vp_adapter *vpna, void *private_data)
{
  int ret;
  struct xdp_buff vale_bpf;
  struct xdp_rxq_info rxq;
  struct net_device *dev = (struct net_device *)this_cpu_ptr(dummy_dev);

  vale_bpf.data = (void *)ft->ft_buf; // packet head
  vale_bpf.data_end = (void *)(ft->ft_buf + (ptrdiff_t)ft->ft_len); // packet end
  vale_bpf.data_hard_start = (void *)ft->ft_buf; // unused
  vale_bpf.data_meta = vale_bpf.data; // unused

  dev->ifindex = vpna->bdg_port;
  rxq.dev = dev;
  rxq.queue_index = *hint;
  rxq.reg_state = 0; // unused
  vale_bpf.rxq = &rxq;

  if (prog) {
    rcu_read_lock();
    ret = bpf_prog_run_xdp(prog, &vale_bpf);
    rcu_read_unlock();
  } else {
    ret = NM_BDG_NOPORT;
  }

  if (ret > NM_BDG_NOPORT) {
    ret = NM_BDG_NOPORT;
  }

  return (u_int)ret;
}

static int vale_bpf_native_install_prog(int prog_fd) {
  struct bpf_prog *p;

  p = bpf_prog_get_type(prog_fd, BPF_PROG_TYPE_XDP);
  if (IS_ERR(p)) {
    return -1;
  }

  if (prog) {
    bpf_prog_put(prog);
  }

  prog = p;

  if (netmap_verbose) {
    printk("Loaded ebpf program to " VALE_NAME);
  }

  return 0;
}

static int vale_bpf_native_uninstall_prog(void) {
  if (prog) {
    bpf_prog_put(prog);

    prog = NULL;

    if (netmap_verbose) {
      printk("Unloaded ebpf program from " VALE_NAME);
    }
  }

  return 0;
}

static int vale_bpf_native_config(struct nm_ifreq *req) {
  int ret;
  struct vale_bpf_native_req *r = (struct vale_bpf_native_req *)req->data; 

  switch (r->method) {
    case INSTALL_PROG:
      ret = vale_bpf_native_install_prog(r->req.install_req.prog_fd);
      if (ret < 0) {
        printk("Installation of bpf program failed");
      }
      break;
    case UNINSTALL_PROG:
      ret = vale_bpf_native_uninstall_prog(); // always returns zero
      break;
    default:
      if (netmap_verbose) {
        printk("Invalid method");
      }
      return -1;
  }
  return ret;
}

static struct netmap_bdg_ops vale_bpf_native_ops = {
  vale_bpf_native_lookup,
  vale_bpf_native_config,
  NULL
};

static int
vale_bpf_native_init(void)
{
  int error;

  error = netmap_bdg_regops(VALE_NAME ":", &vale_bpf_native_ops, NULL, NULL);
  if (error) {
    printk("create a bridge named %s beforehand using vale-ctl", VALE_NAME);
    return -ENOENT;
  }

  dummy_dev = alloc_percpu(struct net_device);
  if (!dummy_dev) {
    return -ENOMEM;
  }

  printk("Loaded vale-bpf-native-" VALE_NAME);

  return 0;
}

static void
vale_bpf_native_fini(void)
{
  int error;

  error = netmap_bdg_regops(VALE_NAME ":", NULL, NULL, NULL);
  if (error) {
    printk("failed to release VALE bridge %d", error);
  }

  if (prog) {
    bpf_prog_put(prog);
  }

  free_percpu(dummy_dev);

  printk("Unloaded vale-bpf-native-" VALE_NAME);
}

module_init(vale_bpf_native_init);
module_exit(vale_bpf_native_fini);
MODULE_AUTHOR("Yutaro Hayakawa");
MODULE_DESCRIPTION("VALE BPF Native Module");
MODULE_LICENSE("GPL");
