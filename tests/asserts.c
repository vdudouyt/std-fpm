#include <stdio.h>
#include <string.h>
#include "asserts.h"

static unsigned int failures = 0;

void assert_true(int result, const char *file, unsigned int line, const char *expr) {
   if(!result) {
      failures++;
      fprintf(stderr, "Failed test at %s line %d\n", file, line);
      fprintf(stderr, "#    got: %d, expected: truthy value\n", result);
      fprintf(stderr, "#    got expression: %s\n", expr);
   }
}

void assert_equal(int left, int right, const char *file, unsigned int line, const char *left_expr, const char *right_expr) {
   if(left != right) {
      failures++;
      fprintf(stderr, "Failed test at %s line %d\n", file, line);
      fprintf(stderr, "#    got: %d, expected: %d\n", left, right);
      fprintf(stderr, "#    got expression: %s\n", left_expr);
   }
}

void assert_str_equal(const char *left,
      const char *right,
      const char *file,
      unsigned int line,
      const char *left_expr,
      const char *right_expr) {
   if(strcmp(left, right) != 0) {
      failures++;
      fprintf(stderr, "Failed test at %s line %d\n", file, line);
      fprintf(stderr, "#    got: '%s', expected: '%s'\n", left, right);
      fprintf(stderr, "#    got expression: %s\n", left_expr);
   }
}

int get_failures_count() {
   return failures;
}
