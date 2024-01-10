#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "asserts.h"
#include "test_fcgi_params_parser.h"
#include "test_fcgi_parser.h"
#include "test_parse_script_filename.h"

int main(int argc, char **argv) {
   if(argc != 2) {
      fprintf(stderr, "Wrong usage\n");
      exit(-1);
   }
   if(strcmp(argv[1], "test_fcgi_params_parser") == 0) {
      test_fcgi_params_parser();
   } else if(strcmp(argv[1], "test_fcgi_parser") == 0) {
      test_fcgi_parser();
   } else if(strcmp(argv[1], "test_parse_script_filename") == 0) {
      test_parse_script_filename();
   } else {
      fprintf(stderr, "Unknown test name: %s\n", argv[1]);
      exit(-1);
   }
   return get_failures_count() > 0 ? -1 : 0;
}
