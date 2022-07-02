#pragma once

#include <tls.pb.h>

extern "C" {
#include <extern/tlse.h>
}

struct prism_tls_state {
  prism::TLSState *state;
};

int tls_export(struct TLSContext *tls, prism::TLSState *ex);
int tls_import(struct TLSContext *tls, const prism::TLSState *ex);
