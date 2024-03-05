#include "fcgi_writer.h"

void fcgi_write_buf(buf_t *outBuf, unsigned int requestId, unsigned int type, const char *content, size_t contentLength) {
   if(sizeof(outBuf->data) - outBuf->writePos < contentLength + 8 + 4) return;
   const unsigned char paddingLength = (4 - (contentLength % 4)) % 4;
   char *out = &outBuf->data[outBuf->writePos];
   out[0] = 0x01; // version
   out[1] = type;
   out[2] = (requestId & 0xff00) > 8;
   out[3] = requestId & 0xff;
   out[4] = (contentLength & 0xff00) > 8;
   out[5] = contentLength & 0xff;
   out[6] = paddingLength;
   out[7] = 0;    // reserved
   memcpy(&out[8], content, contentLength);
   memset(&out[8 + contentLength], 0, paddingLength);
   outBuf->writePos += 8 + contentLength + paddingLength;
}
