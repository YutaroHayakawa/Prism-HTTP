#pragma once

#include <phttp_server.h>
#include <phttp_handoff_server.h>
#include <extern/argparse.h>

struct phttp_args {
  std::string addr;
  uint32_t port;
  uint8_t mac[6];
  int backlog;
  bool tls;
  std::string tls_crt;
  std::string tls_key;
  std::string ho_addr;
  uint32_t ho_port;
  int ho_backlog;
  std::string sw_addr;
  std::string sw_port;
};

void phttp_argparse_set_all_args(argparse::ArgumentParser *parser);
void phttp_argparse_parse_all(argparse::Arguments *args,
                              struct phttp_args *phttp_args);
