#include "buf.h"
#include <string.h>
#include <sys/socket.h>
#define MIN(a, b) ((a) < (b) ? (a) : (b))

void buf_reset(buf_t *buf) {
   buf->readPos = buf->writePos = 0;
}

size_t buf_bytes_remaining(buf_t *this) {
   return this->writePos - this->readPos;
}

ssize_t buf_read_fd(buf_t *this, int fd) {
   ssize_t ret = recv(fd, &this->data[this->writePos], sizeof(this->data) - this->writePos, 0);
   if(ret > 0) this->writePos += ret;
   return ret;
}

ssize_t buf_write_fd(buf_t *this, int fd) {
   ssize_t ret = send(fd, &this->data[this->readPos], buf_bytes_remaining(this), 0);
   if(ret > 0) this->readPos += ret;
   if(this->readPos == this->writePos) buf_reset(this);
   return ret;
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
      buf_reset(from);
   }
   return ret;
}
