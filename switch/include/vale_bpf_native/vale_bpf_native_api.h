#pragma once

struct vale_bpf_native_meta {
  uint8_t sport;
  uint8_t sring;
  uint8_t __unused[sizeof(void *) - 2];
} __attribute__((packed));

enum vale_bpf_action { VALE_BPF_BROADCAST = 254, VALE_BPF_DROP = 255 };
