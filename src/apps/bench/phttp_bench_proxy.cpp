#include <vector>
#include <netinet/ip.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <phttp.h>
#include <extern/tlse.h>

#include "common.h"

static http_server_socket_t hss;
static http_handoff_server_socket_t hhss;
static http_server_handoff_data_t *backends;
static struct global_config gconf;
static struct phttp_args phttp_args;
static uint32_t rr_factor = 0;
static uint32_t nbackends = 0;

static int
bench_proxy_request_handler(struct http_request *req, struct http_response *res,
                            bool imported)
{
  uint64_t objsize;
  int error, nparsed;

  nparsed = sscanf(req->path, "/%lu", &objsize);
  if (nparsed == 0) {
    res->status = 500;
    res->reason = "Internal Server Error";
    memcpy(res->body_mem.cur, "Debug: Invalid path\n", 20);
    error = membuf_consume(&res->body_mem, 20);
    assert(error == 0);
  } else {
    res->status = 600;
    res->reason = "Handoff";
    res->handoff_data = backends + rr_factor;
    if (++rr_factor == nbackends) {
      rr_factor = 0;
    }
  }

  return 0;
}

static std::vector<std::string>
split(std::string str, char del)
{
  std::vector<std::string> result;
  std::string subStr;

  for (const char c : str) {
    if (c == del) {
      result.push_back(subStr);
      subStr.clear();
    } else {
      subStr += c;
    }
  }

  result.push_back(subStr);
  return result;
}

static void
set_bench_proxy_args(argparse::ArgumentParser *parser)
{
  parser->addArgument({"--backends"},
                      "Bench proxy backend server in ipv4_addr:tcp_port format "
                      "can be specified multiple times");
  parser->addArgument({"--nworkers"}, "Number of workers");
}

#define CONN_POOL_SIZE 1
static int
start_connect_to_backends(uv_loop_t *loop, std::string bes_arg,
                          uint32_t workerid)
{
  int error;

  std::vector<std::string> bes = split(bes_arg, ',');
  nbackends = (uint32_t)bes.size() * CONN_POOL_SIZE;
  backends =
      (http_server_handoff_data_t *)calloc(sizeof(backends[0]), nbackends);
  assert(backends != NULL);

  for (uint32_t i = 0; i < CONN_POOL_SIZE; i++) {
    for (size_t j = 0; j < bes.size(); j++) {
      http_server_handoff_data_t *ho_data = backends + (CONN_POOL_SIZE * i + j);
      std::vector<std::string> tmp = split(bes[j], ':');
      ho_data->addr = inet_addr(tmp[0].c_str());
      ho_data->port = htons((uint16_t)(atoi(tmp[1].c_str()) + workerid));
      error = start_connect(loop, ho_data);
      assert(error == 0);
    }
  }

  return 0;
}

int
main(int argc, char **argv)
{
  int error;
  uv_loop_t *loop;

  argparse::ArgumentParser parser(
      "phttp-bench-proxy", "Simple benchmark application (proxy)", "MIT");
  phttp_argparse_set_all_args(&parser);
  set_bench_proxy_args(&parser);

  auto args = parser.parseArgs(argc, argv);
  phttp_argparse_parse_all(&args, &phttp_args);
  auto bes_arg = args.get<std::string>("backends");
  auto nworkers = args.get<uint32_t>("nworkers");

  hss.request_handler = bench_proxy_request_handler;

  struct rlimit lim;
  lim.rlim_cur = 10000;
  lim.rlim_max = 10000;
  error = setrlimit(RLIMIT_NOFILE, &lim);
  assert(error == 0);

  /*
   * Main
   */
  pid_t *workers = (pid_t *)calloc(nworkers, sizeof(workers[0]));
  assert(workers != NULL);

  for (uint32_t i = 0; i < nworkers; i++) {
    workers[i] = fork();
    if (workers[i] == 0) {
      loop = (uv_loop_t *)malloc(sizeof(*loop));
      assert(loop != NULL);

      uv_loop_init(loop);

      tweak_phttp_args(&phttp_args, i);

      init_all_conf(loop, &phttp_args, &hss, &hhss, &gconf);

      loop->data = &gconf;

      error = phttp_server_init(loop, &hss);
      assert(error == 0);

      error = phttp_handoff_server_init(loop, &hhss);
      assert(error == 0);

      error = start_connect_to_backends(loop, bes_arg, i);
      assert(error == 0);

      uv_signal_t sig;
      uv_signal_init(loop, &sig);
      uv_signal_start(&sig, on_sig, SIGINT);

      make_dummy_work(loop);

      printf("Starting event loop... using libuv version %s\n",
             uv_version_string());
      uv_run(loop, UV_RUN_DEFAULT);

      if (hss.tls != NULL) {
        tls_destroy_context(hss.tls);
      }

      free(loop);

      return EXIT_SUCCESS;
    }
  }

  signal(SIGINT, SIG_IGN);

  int stat;
  for (uint32_t i = 0; i < nworkers; i++) {
    waitpid(workers[i], &stat, 0);
    if (stat != 0) {
      fprintf(stderr, "Child process %u stops with status %d\n", workers[i],
              stat);
    }
  }

  prism_switch_client_destroy(gconf.sw_client);
  free(backends);
  free(workers);

  return 0;
}
