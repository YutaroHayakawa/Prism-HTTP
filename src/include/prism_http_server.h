#pragma once

#include <http_export.h>
#include <tls_export.h>

typedef int (*prism_http_handoff_cb)(int sock, struct TLSContext *tls,
                                     struct http_request *http, void *arg);

void prism_http_server_run(const char *address, prism_http_handoff_cb cb,
                           void *arg);
