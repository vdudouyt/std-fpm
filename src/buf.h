#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct {
   char *base;
   size_t readPos, writePos, len;

   char *lastWrite;
   size_t lastWriteLen;
} stdfpm_buf_t;

bool buf_can_read(stdfpm_buf_t *buf);
bool buf_can_write(stdfpm_buf_t *buf);
void buf_reset(stdfpm_buf_t *buf);
void buf_read(stdfpm_buf_t *buf, char *dst, size_t len);
void buf_write(stdfpm_buf_t *buf, const char *src, size_t len);
void buf_release(stdfpm_buf_t *buf);
ssize_t buf_recv(stdfpm_buf_t *buf, int fd);
ssize_t buf_send(stdfpm_buf_t *buf, int fd);
void buf_move(stdfpm_buf_t *src, stdfpm_buf_t *dst);
