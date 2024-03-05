#pragma once
#define FCGI_BUF_SIZE 8 + 65536 + 256
#include <stdio.h>
#include <stdbool.h>

typedef struct {
   char data[FCGI_BUF_SIZE];
   unsigned int readPos, writePos;
} buf_t;

void buf_reset(buf_t *buf);
bool buf_ready_write(buf_t *buf, size_t size);
size_t buf_write(buf_t *this, char *buf, size_t size);
size_t buf_move(buf_t *this, buf_t *from);
size_t buf_bytes_remaining(buf_t *this);
