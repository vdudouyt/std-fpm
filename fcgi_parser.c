#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include "fcgi_parser.h"

#define BE16(a, b) (((a) << 8) | (b))
#define BE32(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

enum { STATUS_READ_HEADER, STATUS_READ_MESSAGE };

fcgi_parser_t *fcgi_parser_new() {
   fcgi_parser_t *ret = malloc(sizeof(fcgi_parser_t));
   memset(ret, 0, sizeof(fcgi_parser_t));
   return ret;
}

void fcgi_parser_write(fcgi_parser_t *this, const uint8_t *input, unsigned int length) {
   for(unsigned int i = 0; i < length; i++) {
      if(this->pos < sizeof(this->buf)) this->buf[this->pos] = input[i];
      this->pos++;

      if(this->status == STATUS_READ_HEADER && this->pos >= 8) {
         this->pos = 0;
         this->status = STATUS_READ_MESSAGE;
	      this->hdr.version        = this->buf[0];
	      this->hdr.type           = this->buf[1];
	      this->hdr.requestId      = BE16(this->buf[2], this->buf[3]);
	      this->hdr.contentLength  = BE16(this->buf[4], this->buf[5]);
	      this->hdr.paddingLength  = this->buf[6];
	      this->hdr.reserved       = this->buf[7];
      }

      if(this->status == STATUS_READ_MESSAGE && this->pos >= (this->hdr.contentLength + this->hdr.paddingLength)) {
         this->pos = 0;
         this->status = STATUS_READ_HEADER;
         if(this->callback) this->callback(&this->hdr, this->buf, this->userdata);
         memset(&this->hdr, 0, sizeof(fcgi_header_t));
      }
   }
}

void fcgi_parser_free(fcgi_parser_t *this) {
   free(this);
}
