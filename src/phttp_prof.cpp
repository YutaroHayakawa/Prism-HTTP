#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstdbool>
#include <vector>
#include <unistd.h>
#include <sys/time.h>
#include <phttp_prof.h>

bool initialized = false;
FILE *export_side;
FILE *import_side;

void
prof_tstamp(enum prof_types type, enum prof_ids id, uint32_t peer_addr,
            uint16_t peer_port)
{
#ifdef PHTTP_PROF
  FILE *f;
  struct timeval tstamp;

  if (!initialized) {
    char fname[64] = {0};
    sprintf(fname, "/tmp/prism_prof_export_%d.csv", getpid());
    export_side = fopen(fname, "w");
    sprintf(fname, "/tmp/prism_prof_import_%d.csv", getpid());
    import_side = fopen(fname, "w");
    initialized = true;
  }

  if (type == PROF_TYPE_EXPORT) {
    f = export_side;
  } else {
    f = import_side;
  }

  gettimeofday(&tstamp, NULL);
  fprintf(f, "%d,0x%x:%u,%03d.%06d\n", id, peer_addr, peer_port,
          (int)(tstamp.tv_sec % 1000), (int)tstamp.tv_usec);
  fflush(f);
#else
  return;
#endif
}
