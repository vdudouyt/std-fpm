#include "buf.h"
#include <string.h>
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void buf_discard(buf_t *buf) {
   buf->readPos = buf->writePos = 0;
}

bool buf_ready_write(buf_t *buf, size_t size) {
   return buf != NULL && buf->readPos == 0 && (FCGI_BUF_SIZE - buf->writePos) >= size;
}

size_t buf_write(buf_t *this, char *buf, size_t size) {
   size_t bytes_written = MIN(FCGI_BUF_SIZE - this->writePos, size);
   memcpy(&this->data[this->writePos], buf, bytes_written);
   this->writePos += bytes_written;
   return bytes_written;
}

size_t buf_move(buf_t *this, buf_t *from) {
   size_t ret = buf_write(this, &from->data[from->readPos], from->writePos);
   from->readPos += ret;
   if(from->readPos == from->writePos) {
      buf_discard(from);
   }
   return ret;
}
