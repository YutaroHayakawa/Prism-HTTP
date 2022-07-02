#include <cstdint>
#include <extern/tlse.h>
#include <phttp_argparse.h>

void
phttp_argparse_set_all_args(argparse::ArgumentParser *parser)
{
  parser->addArgument({"--addr"}, "HTTP server IPv4 address");
  parser->addArgument({"--port"}, "HTTP server TCP port");
  parser->addArgument({"--mac"}, "HTTP server MAC address");
  parser->addArgument({"--backlog"}, "HTTP server backlog");

  parser->addArgument({"--tls"}, "Enable TLS",
                      argparse::ArgumentType::StoreTrue);
  parser->addArgument({"--tls-crt"}, "TLS certificate file");
  parser->addArgument({"--tls-key"}, "TLS secret key file");

  parser->addArgument({"--ho-addr"}, "HTTP handoff server IPv4 address");
  parser->addArgument({"--ho-port"}, "HTTP handoff server TCP port");
  parser->addArgument({"--ho-backlog"}, "HTTP handoff server backlog");

  parser->addArgument({"--sw-addr"}, "Switch daemon IPv4 address");
  parser->addArgument({"--sw-port"}, "Switch daemon TCP port");
}

static void
phttp_argparse_parse_server_conf(argparse::Arguments *args,
                                 struct phttp_args *phttp_args)
{
  auto addr = args->get<std::string>("addr");
  auto port = args->get<uint16_t>("port");
  auto mac = args->get<std::string>("mac");
  auto backlog = args->get<int>("backlog");

  phttp_args->addr = addr;
  phttp_args->port = port;
  phttp_args->backlog = backlog;

  sscanf(mac.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", phttp_args->mac + 0,
         phttp_args->mac + 1, phttp_args->mac + 2, phttp_args->mac + 3,
         phttp_args->mac + 4, phttp_args->mac + 5);

  if (args->has("tls")) {
    auto crt = args->get<std::string>("tls-crt");
    auto key = args->get<std::string>("tls-key");
    phttp_args->tls = true;
    phttp_args->tls_crt = crt;
    phttp_args->tls_key = key;
  } else {
    phttp_args->tls = false;
  }
}

static void
phttp_argparse_parse_handoff_server_conf(argparse::Arguments *args,
                                         struct phttp_args *phttp_args)
{
  auto ho_addr = args->get<std::string>("ho-addr");
  auto ho_port = args->get<uint16_t>("ho-port");
  auto ho_backlog = args->get<int>("ho-backlog");

  phttp_args->ho_addr = ho_addr;
  phttp_args->ho_port = ho_port;
  phttp_args->ho_backlog = ho_backlog;
}

static void
phttp_argparse_parse_global_conf(argparse::Arguments *args,
                                 struct phttp_args *phttp_args)
{
  auto sw_addr = args->get<std::string>("sw-addr");
  auto sw_port = args->get<std::string>("sw-port");

  phttp_args->sw_addr = sw_addr;
  phttp_args->sw_port = sw_port;
}

void
phttp_argparse_parse_all(argparse::Arguments *args,
                         struct phttp_args *phttp_args)
{
  phttp_argparse_parse_server_conf(args, phttp_args);
  phttp_argparse_parse_handoff_server_conf(args, phttp_args);
  phttp_argparse_parse_global_conf(args, phttp_args);
}
