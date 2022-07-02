#pragma once

#include <membuf.h>
#include <stdint.h>

#define HTTP_HEADERS_MAX 16

struct http_header {
  char *name;
  uint64_t name_len;
  char *val;
  uint64_t val_len;
};

struct http_request {
  struct membuf mem;
  int32_t minor_version;
  char *method;
  uint64_t method_len;
  char *path;
  uint64_t path_len;
  struct http_header headers[HTTP_HEADERS_MAX];
  uint64_t nheaders;
  char *body;
  uint64_t body_len;
};

struct http_response;
struct http_response {
  struct membuf mem;
  struct membuf body_mem;
  uint32_t status;
  char *reason;
  struct http_header headers[HTTP_HEADERS_MAX];
  uint64_t nheaders;
  int (*after_res)(struct http_response *);
  void *handoff_data;
};

int http_request_init(struct http_request *req);
void http_request_deinit(struct http_request *req);
void http_request_reset(struct http_request *req);
struct http_header *http_request_find_header(struct http_request *req,
    const char *name,uint64_t name_len);
uint64_t http_request_determine_body_len(struct http_request *req);
int http_parse_request(struct http_request *req);
void http_print_request(struct http_request *req);
int http_response_init(struct http_response *res);
void http_response_deinit(struct http_response *res);
void http_response_reset(struct http_response *res);
int http_response_add_header(struct http_response *res, char *name,
                             uint64_t name_len, char *val, uint64_t val_len);
