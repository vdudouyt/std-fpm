#pragma once
#include "string.h"
#include <event2/buffer.h>

void fcgi_write_buf(struct evbuffer *outBuf, unsigned int requestId, unsigned int type, const char *content, size_t contentLength);
