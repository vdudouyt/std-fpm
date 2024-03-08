#include "debug_utils.h"

void hexdump(const unsigned char *buf, size_t size) {
   for(int i = 0; i < size; i++) {
      printf("%02x ", buf[i]);;
   }
   printf("\n");
}

void escape(unsigned char *out, const unsigned char *in, size_t input_length) {
   unsigned int d = 0;
   for(unsigned int i = 0; i < input_length; i++) {
      if(isprint(in[i])) {
         out[d++] = in[i];
      } else {
         out[d++] = '\\';
         out[d++] = 'x';
         sprintf(&out[d], "%02x", in[i]);
         d += 2;
      }
   }
   out[d++] = '\0';
}
