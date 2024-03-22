#pragma once
#include <string.h>
#include <event2/buffer.h>
#include "fd_ctx.h"

void fcgi_write_buf(struct evbuffer *outBuf, unsigned int requestId, unsigned int type, const char *content, size_t contentLength);
void fcgi_send_response(fd_ctx_t *ctx, const char *response, size_t size);
void fcgi_serve_response(int listen_sock, const char *response, size_t size);
