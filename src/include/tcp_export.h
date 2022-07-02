#pragma once

#include <stdint.h>
#include <tcp.pb.h>

struct prism_tcp_state {
  prism::TCPState *state;
};

int tcp_export(int sock, prism::TCPState *state);
int tcp_import(int sock, const prism::TCPState *state);
