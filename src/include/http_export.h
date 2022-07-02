#pragma once

#include <http.h>
#include <http.pb.h>

int http_request_export(struct http_request *req, prism::HTTPReq *ex);
int http_request_import(struct http_request *req, const prism::HTTPReq *ex);
