#include "buf.h"

void buf_discard(buf_t *buf) {
   buf->readPos = buf->writePos = 0;
}
