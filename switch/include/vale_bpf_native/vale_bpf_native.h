#pragma once

// Used by INSTALL method
struct vale_bpf_native_install_req {
  int prog_fd;
};

struct vale_bpf_native_meta {
  uint8_t sport;
  uint8_t sring;
  uint8_t __unused[sizeof(void *) - 2];
};

struct vale_bpf_native_req {
  uint8_t method;
  union {
    struct vale_bpf_native_install_req install_req;
  } req;
};

enum vale_bpf_native_method { INSTALL_PROG, UNINSTALL_PROG };
