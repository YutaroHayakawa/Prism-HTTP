#include <http.pb.h>

#include <http.h>

int
http_request_export(struct http_request *req, prism::HTTPReq *ex)
{
  if (req == NULL || ex == NULL) {
    return -EINVAL;
  }

  struct membuf *mem = &req->mem;

  ex->set_buf(mem->begin, mem->cur - mem->begin);
  ex->set_minor_version(req->minor_version);
  ex->set_method_ofs(req->method - mem->begin);
  ex->set_method_len(req->method_len);
  ex->set_path_ofs(req->path - mem->begin);
  ex->set_path_len(req->path_len);

  if (req->body != NULL) {
    ex->set_body_ofs(req->body - mem->begin);
    ex->set_body_len(req->body_len);
  } else {
    ex->set_body_ofs(0);
    ex->set_body_len(0);
  }

  ex->set_nheaders(req->nheaders);

  for (uint64_t i = 0; i < req->nheaders; i++) {
    prism::HTTPHeader *h = ex->add_headers();
    h->set_name_ofs(req->headers[i].name - mem->begin);
    h->set_name_len(req->headers[i].name_len);
    h->set_val_ofs(req->headers[i].val - mem->begin);
    h->set_val_len(req->headers[i].val_len);
  }

  return 0;
}

int
http_request_import(struct http_request *req, const prism::HTTPReq *ex)
{
  if (req == NULL || ex == NULL) {
    return -EINVAL;
  }

  struct membuf *mem = &req->mem;
  if (membuf_avail(mem) < ex->buf().length()) {
    fprintf(stderr, "Warning: Memory growing occured\n");
    membuf_grow(mem, ex->buf().length() - membuf_avail(mem));
  }

  memcpy(mem->cur, ex->buf().c_str(), ex->buf().size());
  membuf_consume(mem, ex->buf().size());

  req->minor_version = ex->minor_version();
  req->method = mem->begin + ex->method_ofs();
  req->method_len = ex->method_len();
  req->path = mem->begin + ex->path_ofs();
  req->path_len = ex->path_len();
  req->body = ex->body_len() == 0 ? NULL : mem->begin + ex->body_ofs();
  req->body_len = ex->body_len();
  req->nheaders = ex->nheaders();

  for (uint64_t i = 0; i < ex->nheaders(); i++) {
    req->headers[i].name = mem->begin + ex->headers(i).name_ofs();
    req->headers[i].name_len = ex->headers(i).name_len();
    req->headers[i].val = mem->begin + ex->headers(i).val_ofs();
    req->headers[i].val_len = ex->headers(i).val_len();
  }

  return 0;
}
