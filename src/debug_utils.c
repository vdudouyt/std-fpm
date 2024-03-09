#include "debug_utils.h"

void hexdump(const char *buf, size_t size) {
   for(int i = 0; i < size; i++) {
      printf("%02x ", (unsigned char) buf[i]);
   }
   printf("\n");
}

void escape(char *out, const char *in, size_t input_length) {
   unsigned int d = 0;
   for(unsigned int i = 0; i < input_length; i++) {
      if(isprint(in[i])) {
         out[d++] = in[i];
      } else {
         out[d++] = '\\';
         out[d++] = 'x';
         sprintf(&out[d], "%02x", (unsigned char) in[i]);
         d += 2;
      }
   }
   out[d++] = '\0';
}
