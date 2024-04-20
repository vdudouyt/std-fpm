#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "buf.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

bool buf_can_read(stdfpm_buf_t *buf) {
   return buf->writePos > 0;
}

bool buf_can_write(stdfpm_buf_t *buf) {
   return buf->readPos == 0;
}

static void buf_check_space(stdfpm_buf_t *buf, size_t len) {
   if(!buf->base) {
      buf->base = malloc(len);
      buf->len = len;
   } else if(buf->len < len) {
      buf->base = realloc(buf->base, len);
      buf->len = len;
   }
}

void buf_write(stdfpm_buf_t *buf, const char *src, size_t len) {
   buf_check_space(buf, buf->writePos + len);
   buf->lastWrite = &buf->base[buf->writePos];
   buf->lastWriteLen = len;
   memcpy(&buf->base[buf->writePos], src, len);
   buf->writePos += len;
}

void buf_reset(stdfpm_buf_t *buf) {
   buf->readPos = buf->writePos = 0;
}

void buf_read(stdfpm_buf_t *buf, char *dst, size_t len) {
   if(!buf->base) return;
   size_t bytes_available = MIN(buf->writePos - buf->readPos, len);
   memcpy(dst, &buf->base[buf->readPos], bytes_available);
   buf->readPos += bytes_available;
   if(buf->readPos == buf->writePos) buf_reset(buf);
}

ssize_t buf_recv(stdfpm_buf_t *buf, int fd) {
   if(!buf_can_write(buf)) return -1;
   char inBuf[4096];
   ssize_t sz = read(fd, inBuf, sizeof(inBuf));
   if(sz <= 0) return sz;
   buf_write(buf, inBuf, sz);
   return sz;
}

ssize_t buf_send(stdfpm_buf_t *buf, int fd) {
   if(!buf_can_read(buf)) return -1;
   ssize_t sz = write(fd, &buf->base[buf->readPos], buf->writePos - buf->readPos);
   if(sz <= 0) return sz;
   buf->readPos += sz;
   if(buf->readPos == buf->writePos) buf_reset(buf);
}

void buf_move(stdfpm_buf_t *src, stdfpm_buf_t *dst) {
   dst->base = src->base;
   dst->readPos = src->readPos;
   dst->writePos = src->writePos;
   src->base = NULL;
   buf_reset(src);
}

void buf_release(stdfpm_buf_t *buf) {
   if(buf->base) {
      free(buf->base);
      buf->base = NULL;
   }
}
