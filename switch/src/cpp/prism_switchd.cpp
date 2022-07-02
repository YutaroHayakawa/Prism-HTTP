#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/poll.h>
#include <arpa/inet.h>

#include <prism_switch/prism_switch.h>
#include "bcc_vale_bpf_native.h"

using ebpf::BPFHashTable;
using ebpf::StatusTuple;

static struct {
  char *bpf_src;
  char *vale_name;
  char *include_path;
  uint32_t sw_addr;
  uint16_t sw_port;
} g_conf;

static void
usage(char *prog_name)
{
  std::cerr
      << "Usage: " << prog_name
      << " -s <vale_name> -I <include path> -f <bpf source> -a <address:port>"
      << std::endl;
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
build_sw_addr(std::string host)
{
  auto spl = split(host, ':');
  g_conf.sw_addr = inet_addr(spl[0].c_str());
  g_conf.sw_port = htons((uint16_t)atoi(spl[1].c_str()));
}

static int
parse_options(int argc, char **argv)
{
  int opt;

  g_conf.vale_name = NULL;
  g_conf.include_path = NULL;

  while ((opt = getopt(argc, argv, "f:s:I:a:")) != -1) {
    switch (opt) {
    case 'f':
      g_conf.bpf_src = strdup(optarg);
      break;
    case 's':
      g_conf.vale_name = strdup(optarg);
      break;
    case 'I':
      g_conf.include_path = strdup(optarg);
      break;
    case 'a':
      build_sw_addr(std::string(optarg));
      break;
    default:
      usage(argv[0]);
      return EINVAL;
    }
  }

  if (g_conf.vale_name == NULL || g_conf.include_path == NULL) {
    usage(argv[0]);
    return EINVAL;
  }

  return 0;
}

static bool end = false;

void
on_int(int sig)
{
  end = true;
}

#define TRACE_PIPE "/sys/kernel/debug/tracing/trace_pipe"
static void *
poll_tracing_pipe(void *arg)
{
  int fd = open(TRACE_PIPE, O_RDONLY);
  if (fd < 0) {
    std::cout << "Failed to open trace pipe" << std::endl;
    return NULL;
  }

  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;

  int error;
  char data[0xFFFF];
  ssize_t rlen;
  while (!end) {
    memset(data, 0, 0xFFFF);

    error = poll(&pfd, 1, 1);
    if (error == 0) {
      continue;
    } else if (error < 0) {
      perror("poll");
      break;
    }

    if (pfd.revents & POLLIN) {
      rlen = read(fd, data, 0xFFFF);
      if (rlen > 0) {
        std::cout << data;
        std::cout.flush();
      }
    }
  }

  return NULL;
}

int
main(int argc, char **argv)
{
  int error;

  error = parse_options(argc, argv);
  if (error) {
    return EXIT_FAILURE;
  }

  ebpf::VALE_BPF vale;
  std::string include_opt = "-I" + std::string(g_conf.include_path);
  std::vector<std::string> cflags = {
      include_opt, "-O3", "-D CONFIG_ADDR=" + std::to_string(g_conf.sw_addr),
      "-D CONFIG_PORT=" + std::to_string(g_conf.sw_port)};

  std::ifstream t(g_conf.bpf_src);
  std::stringstream prog;
  prog << t.rdbuf();

  auto status = vale.init(prog.str(), cflags);
  if (status.code() == -1) {
    std::cerr << status.msg() << std::endl;
    return EXIT_FAILURE;
  }

  std::string vale_name = std::string(g_conf.vale_name);
  if (vale_name.back() != ':') {
    vale_name += ":";
  }

  status = vale.attach_vale_bpf(vale_name, "vale_lookup");
  if (status.code() == -1) {
    std::cerr << status.msg() << std::endl;
    return EXIT_FAILURE;
  }

  signal(SIGINT, on_int);

  pthread_t trace_thread;
  pthread_create(&trace_thread, NULL, poll_tracing_pipe, NULL);
  std::cout << "Server listening on " << g_conf.vale_name << std::endl;
  pthread_join(trace_thread, NULL);

  vale.detach_vale_bpf(vale_name.c_str());

  return 0;
}
