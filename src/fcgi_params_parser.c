#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "fcgi_params_parser.h"

#define BE16(a, b) (((a) << 8) | (b))
#define BE32(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX_KEY_LENGTH 256

enum { STATUS_READ_KEY_LENGTH = 0, STATUS_READ_VALUE_LENGTH = 1, STATUS_READ_MESSAGE = 2 };

static char *status_str(const int status) {
   switch(status) {
      case STATUS_READ_KEY_LENGTH: return "STATUS_READ_KEY_LENGTH";
      case STATUS_READ_VALUE_LENGTH: return "STATUS_READ_VALUE_LENGTH";
      case STATUS_READ_MESSAGE: return "STATUS_READ_MESSAGE";
      default: return "UNKNOWN";
   }
}

void fcgi_params_parser_dump(fcgi_params_parser_t *this) {
   printf("Parser { status = %s, buf = [u8 x %d], pos = %d, key_length = %d, value_length = %d }\n",
      status_str(this->status), this->pos, this->pos, this->key_length, this->value_length);
}

fcgi_params_parser_t *fcgi_params_parser_new(unsigned int buf_size) {
   if(buf_size < MAX_KEY_LENGTH) {
      return NULL;
   }

   fcgi_params_parser_t *ret = malloc(sizeof(fcgi_params_parser_t));
   if(!ret) return NULL;
   memset(ret, 0, sizeof(fcgi_params_parser_t));
   ret->buf = malloc(buf_size);
   if(!ret->buf) return NULL;

   ret->buf_size = buf_size;
   memset(ret->buf, 0, buf_size);
   return ret;
}

void fcgi_params_parser_free(fcgi_params_parser_t *this) {
   free(this->buf);
   free(this);
}

void fcgi_params_parser_write(fcgi_params_parser_t *this, const char *input, unsigned int length) {
   for(unsigned int i = 0; i < length; i++) {
      if(this->pos < this->buf_size) this->buf[this->pos] = input[i];
      this->pos++;

      const bool got_uint8    = this->pos == 1 && !(this->buf[0] & 0x80);
      const bool got_uint32   = this->pos == 4 &&  (this->buf[0] & 0x80);

      if(this->status <= STATUS_READ_VALUE_LENGTH && (got_uint8 || got_uint32)) {
         unsigned int *out = this->status == STATUS_READ_VALUE_LENGTH ? &this->value_length : &this->key_length;
         *out = got_uint32 ? BE32(this->buf[0] & 0x7F, this->buf[1], this->buf[2], this->buf[3]) : this->buf[0];
         this->status++;
         this->pos = 0;
      } 

      if(this->status == STATUS_READ_MESSAGE && this->pos >= this->key_length + this->value_length) {
         this->buf[MIN(this->pos, this->buf_size)] = '\0';
         char keybuf[MAX_KEY_LENGTH];
         memset(keybuf, 0, sizeof(keybuf));
         memcpy(keybuf, this->buf, MIN(this->key_length, sizeof(keybuf) - 1));
         unsigned char *value = &this->buf[MIN(this->key_length, this->buf_size - 1)];
         if(this->callback) this->callback(keybuf, (const char*) value, this->userdata);
         this->pos = this->status = this->key_length = this->value_length = 0;
      }
   }
}
