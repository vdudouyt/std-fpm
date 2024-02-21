#pragma once
#define FCGI_BUF_SIZE 8 + 65536 + 256

typedef struct {
   char data[FCGI_BUF_SIZE];
   unsigned int readPos, writePos;
} buf_t;

void buf_discard(buf_t *buf);
