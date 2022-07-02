#include <stdint.h>
#include <tls_export.h>
#include <cstdlib>

int
tls_export(struct TLSContext *tls, prism::TLSState *ex)
{
  int ret;
  uint8_t *buf;

  buf = (uint8_t *)calloc(1, 0xFFFF);
  if (buf == NULL) {
    return -ENOMEM;
  }

  ret = tls_export_context(tls, buf, 0xFFFF, 1);
  if (ret == 0) {
    return -EINVAL;
  }

  ex->set_buf(buf, ret);

  free(buf);

  return 0;
}

int
tls_import(struct TLSContext *tls, const prism::TLSState *ex)
{
  ssize_t ret;

  ret =
      tls_import_context2(tls, (uint8_t *)ex->buf().c_str(), ex->buf().size());
  if (ret < 0) {
    return -EINVAL;
  }

  tls_make_exportable(tls, 1);

  return 0;
}
