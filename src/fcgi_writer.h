#pragma once
#include "buf.h"
#include "string.h"

void fcgi_write_buf(buf_t *outBuf, unsigned int requestId, unsigned int type, const char *content, size_t contentLength);
