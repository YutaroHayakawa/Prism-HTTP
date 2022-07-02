#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <algorithm>

#include <extern/picohttpparser.h>
#include <uv.h>

#include <http.h>

void
http_header_reset(struct http_header *header)
{
  header->name = NULL;
  header->name_len = 0;
  header->val = NULL;
  header->val_len = 0;
}

int
http_request_init(struct http_request *req)
{
  membuf_init(&req->mem, 5000000);
  req->minor_version = 0;
  req->method = NULL;
  req->method_len = 0;
  req->path = NULL;
  req->path_len = 0;

  for (uint32_t i = 0; i < HTTP_HEADERS_MAX; i++) {
    http_header_reset(req->headers + i);
  }

  req->nheaders = HTTP_HEADERS_MAX;
  req->body = NULL;
  req->body_len = 0;

  return 0;
}

void
http_request_deinit(struct http_request *req)
{
  membuf_deinit(&req->mem);
}

void
http_request_reset(struct http_request *req)
{
  membuf_reset(&req->mem);
  req->minor_version = 0;
  req->method = NULL;
  req->method_len = 0;
  req->path = NULL;
  req->path_len = 0;

  for (uint32_t i = 0; i < HTTP_HEADERS_MAX; i++) {
    http_header_reset(req->headers + i);
  }

  req->nheaders = HTTP_HEADERS_MAX;
  req->body = NULL;
  req->body_len = 0;
}

struct http_header *
http_request_find_header(struct http_request *req, const char *name,
    uint64_t name_len)
{
  for (uint32_t i = 0; i < req->nheaders; i++) {
    if (strncmp(req->headers[i].name, name,
                std::min(req->headers[i].name_len, name_len)) == 0) {
      return req->headers + i;
    }
  }

  return NULL;
}

uint64_t
http_request_determine_body_len(struct http_request *req)
{
  char *endptr;
  struct http_header *h = http_request_find_header(req, "Content-Length", 14);
  uint64_t ret;
  
  if (h == NULL) {
    return 0;
  }

  ret = strtoll(h->val, &endptr, 0);

  if (endptr != h->val + h->val_len) {
    return 0;
  }

  return ret;
}

int
http_response_init(struct http_response *res)
{
  membuf_init(&res->mem, 4096);
  membuf_init(&res->body_mem, 5000000);
  res->status = 0;
  res->reason = "Uninitialized";
  for (uint32_t i = 0; i < HTTP_HEADERS_MAX; i++) {
    http_header_reset(res->headers + i);
  }
  res->nheaders = 0;
  res->after_res = NULL;
  return 0;
}

void
http_response_deinit(struct http_response *res)
{
  membuf_deinit(&res->mem);
  membuf_deinit(&res->body_mem);
}

void
http_response_reset(struct http_response *res)
{
  membuf_reset(&res->mem);
  membuf_reset(&res->body_mem);
  res->status = 0;
  res->reason = "Uninitialized";
  for (uint32_t i = 0; i < HTTP_HEADERS_MAX; i++) {
    http_header_reset(res->headers + i);
  }
  res->nheaders = 0;
  res->after_res = NULL;
}

int
http_response_add_header(struct http_response *res, char *name,
                         uint64_t name_len, char *val, uint64_t val_len)
{
  if (res->nheaders == HTTP_HEADERS_MAX) {
    return -EBUSY;
  }

  res->headers[res->nheaders].name = name;
  res->headers[res->nheaders].name_len = name_len;
  res->headers[res->nheaders].val = val;
  res->headers[res->nheaders].val_len = val_len;
  res->nheaders++;

  return 0;
}

int
http_parse_request(struct http_request *req)
{
  struct membuf *mem = &req->mem;
  req->nheaders = HTTP_HEADERS_MAX;
  return phr_parse_request(
      mem->begin, mem->cur - mem->begin, (const char **)&req->method,
      &req->method_len, (const char **)&req->path, &req->path_len,
      &req->minor_version, (struct phr_header *)req->headers, &req->nheaders,
      mem->prev - mem->begin);
}

void
http_print_request(struct http_request *req)
{
  printf("method        : %.*s\n", (int)req->method_len, req->method);
  printf("path          : %.*s\n", (int)req->path_len, req->path);
  printf("minor_version : %d\n", req->minor_version);
  printf("nheaders      : %lu\n", req->nheaders);
  printf("body_len      : %lu\n", req->body_len);
  for (uint32_t i = 0; i < req->nheaders; ++i) {
    printf("headers[%d]    : %.*s: %.*s\n", i, (int)req->headers[i].name_len,
           req->headers[i].name, (int)req->headers[i].val_len,
           req->headers[i].val);
  }
}
