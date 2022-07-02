#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

struct membuf {
  char *begin;
  char *cur;
  char *prev;
  char *end;
};

inline void
membuf_init(struct membuf *buf, size_t initial_size)
{
  buf->begin = (char *)malloc(initial_size);
  assert(buf->begin != NULL);
  buf->cur = buf->begin;
  buf->prev = buf->begin;
  buf->end = buf->begin + initial_size;
}

inline void
membuf_init_from_mem(struct membuf *buf, uint8_t *mem, size_t mem_size)
{
  buf->begin = (char *)mem;
  buf->cur = buf->begin;
  buf->prev = buf->begin;
  buf->end = buf->begin + mem_size;
}

inline void
membuf_deinit(struct membuf *buf)
{
  free(buf->begin);
}

inline void
membuf_grow(struct membuf *buf, size_t grow_size)
{
  ptrdiff_t cur_ofs = buf->cur - buf->begin;
  ptrdiff_t prev_ofs = buf->prev - buf->begin;
  size_t new_size = buf->cur - buf->begin + grow_size;

  buf->begin = (char *)realloc(buf->begin, new_size);
  assert(buf->begin != NULL);

  buf->cur = buf->begin + cur_ofs;
  buf->prev = buf->begin + prev_ofs;
  buf->end = buf->begin + new_size;
}

inline void
membuf_reset(struct membuf *buf)
{
  buf->cur = buf->begin;
  buf->prev = buf->begin;
}

inline size_t
membuf_avail(struct membuf *buf)
{
  return (size_t)(buf->end - buf->cur);
}

inline size_t
membuf_used(struct membuf *buf)
{
  return (size_t)(buf->cur - buf->begin);
}

inline int
membuf_consume(struct membuf *buf, size_t size)
{
  if (membuf_avail(buf) < size) {
    return -1;
  }

  buf->prev = buf->cur;
  buf->cur += size;

  return 0;
}
