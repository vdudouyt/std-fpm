#include "fcgi_writer.h"

void fcgi_write_buf(struct evbuffer *outBuf, unsigned int requestId, unsigned int type, const char *content, size_t contentLength) {
   const unsigned char paddingLength = (4 - (contentLength % 4)) % 4;
   char padding[] = { 0, 0, 0, 0 };
   char hdr[8];
   hdr[0] = 0x01; // version
   hdr[1] = type;
   hdr[2] = (requestId & 0xff00) > 8;
   hdr[3] = requestId & 0xff;
   hdr[4] = (contentLength & 0xff00) > 8;
   hdr[5] = contentLength & 0xff;
   hdr[6] = paddingLength;
   hdr[7] = 0;    // reserved
   evbuffer_add(outBuf, hdr, sizeof(hdr));
   evbuffer_add(outBuf, content, contentLength);
   evbuffer_add(outBuf, padding, paddingLength);
}
