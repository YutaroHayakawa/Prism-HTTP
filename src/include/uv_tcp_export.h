#pragma once

#include <uv.h>
#include "tcp_export.h"

int uv_tcp_import(uv_tcp_t *tcp, struct prism_tcp_state *state);
int uv_tcp_export(uv_tcp_t *tcp, struct prism_tcp_state *state);
