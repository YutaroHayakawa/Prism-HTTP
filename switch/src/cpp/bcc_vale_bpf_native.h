#pragma once

#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <errno.h>

#include <bcc/BPF.h>

extern "C" {
#define NETMAP_WITH_LIBS
#include <net/netmap.h>
#include <net/netmap_user.h>
#include <vale_bpf_native/vale_bpf_native.h>
}

namespace ebpf {
class VALE_BPF : public BPF {
protected:
  int nmfd;

public:
  VALE_BPF(void) { nmfd = open("/dev/netmap", O_RDWR); }

  StatusTuple
  attach_vale_bpf(const std::string &vale_name, const std::string &func_name)
  {
    int error;

    struct nm_ifreq req;
    memset(&req, 0, sizeof(req));
    strcpy(req.nifr_name, vale_name.c_str());

    struct vale_bpf_native_req *r = (struct vale_bpf_native_req *)req.data;
    r->method = INSTALL_PROG;

    StatusTuple ret =
        load_func(func_name, BPF_PROG_TYPE_XDP, r->req.install_req.prog_fd);
    if (ret.code() < 0) {
      return ret;
    }

    error = ioctl(nmfd, NIOCCONFIG, &req);
    if (error < 0) {
      return StatusTuple(-1, "Failed to load eBPF program");
    }

    return StatusTuple(0);
  }

  StatusTuple
  detach_vale_bpf(const std::string &vale_name)
  {
    int error;

    struct nm_ifreq req;
    memset(&req, 0, sizeof(req));
    strcpy(req.nifr_name, vale_name.c_str());

    struct vale_bpf_native_req *r = (struct vale_bpf_native_req *)req.data;
    r->method = UNINSTALL_PROG;

    error = ioctl(nmfd, NIOCCONFIG, &req);
    if (error < 0) {
      return StatusTuple(-1, "Failed to unload eBPF program");
    }

    return StatusTuple(0);
  }

  ~VALE_BPF() { close(nmfd); };
};
} // namespace ebpf
