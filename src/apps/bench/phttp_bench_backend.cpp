#include <netinet/ip.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <phttp.h>
#include <extern/tlse.h>

#include "common.h"

#define DEBUG(_fmt, ...)                                                       \
  do {                                                                         \
    struct timeval _t0;                                                        \
    gettimeofday(&_t0, NULL);                                                  \
    fprintf(stderr, "%03d.%06d %s [%d] " _fmt, (int)(_t0.tv_sec % 1000),       \
            (int)_t0.tv_usec, __FUNCTION__, __LINE__, ##__VA_ARGS__);          \
  } while (0)

static http_server_socket_t hss;
static http_handoff_server_socket_t hhss;
static http_server_handoff_data_t *conn_pool;
static struct global_config gconf;
static struct phttp_args phttp_args;
static uint32_t rr_factor = 0;
static uint32_t nconnection = 0;

static int
bench_backend_request_handler(struct http_request *req,
                              struct http_response *res, bool imported)
{
  int error;

  if (imported) {
    uint64_t objsize;
    sscanf(req->path, "/%lu", &objsize);
    res->status = 200;
    res->reason = "OK";
    error = membuf_consume(&res->body_mem, objsize);
    assert(error == 0);
  } else {
    res->status = 600;
    res->reason = "Handoff";
    res->handoff_data = conn_pool + rr_factor;
    if (++rr_factor == nconnection) {
      rr_factor = 0;
    }
  }

  return 0;
}

#define CONN_POOL_SIZE 1
static int
start_connect_to_proxy(uv_loop_t *loop, std::string proxy_addr,
                       uint16_t proxy_port, uint32_t workerid)
{
  int error;

  nconnection = CONN_POOL_SIZE;
  conn_pool = (http_server_handoff_data_t *)calloc(sizeof(conn_pool[0]),
                                                   CONN_POOL_SIZE);
  assert(conn_pool != NULL);

  for (uint32_t i = 0; i < CONN_POOL_SIZE; i++) {
    conn_pool[i].addr = inet_addr(proxy_addr.c_str());
    conn_pool[i].port = htons(proxy_port + workerid);
    error = start_connect(loop, conn_pool + i);
    assert(error == 0);
  }

  return 0;
}

static void
set_bench_backend_args(argparse::ArgumentParser *parser)
{
  parser->addArgument({"--proxy-addr"}, "Bench proxy server IPv4 address");
  parser->addArgument({"--proxy-port"}, "Bench proxy server server TCP port");
  parser->addArgument({"--nworkers"}, "Number of workers");
}

int
main(int argc, char **argv)
{
  int error;
  uv_loop_t *loop;

  argparse::ArgumentParser parser(
      "phttp-bench-backend", "Simple benchmark application (backend)", "MIT");
  phttp_argparse_set_all_args(&parser);
  set_bench_backend_args(&parser);

  auto args = parser.parseArgs(argc, argv);
  phttp_argparse_parse_all(&args, &phttp_args);
  auto proxy_addr = args.get<std::string>("proxy-addr");
  auto proxy_port = args.get<uint16_t>("proxy-port");
  auto nworkers = args.get<uint32_t>("nworkers");

  hss.request_handler = bench_backend_request_handler;

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

      error = start_connect_to_proxy(loop, proxy_addr, proxy_port, i);
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

      prism_switch_client_destroy(gconf.sw_client);
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

  free(workers);

  return 0;
}
