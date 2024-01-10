#pragma once

#define ASSERT_TRUE(cond) assert_true((cond), __FILE__, __LINE__, #cond);
#define ASSERT_EQUAL(got, expected) assert_equal((got), (expected), __FILE__, __LINE__, #got, #expected); 
#define ASSERT_STR_EQUAL(got, expected) assert_str_equal((got), (expected), __FILE__, __LINE__, #got, #expected); 

void assert_true(int result, const char *file, unsigned int line, const char *expr);
void assert_equal(int left, int right, const char *file, unsigned int line, const char *left_expr, const char *right_expr);
void assert_str_equal(const char *left,
      const char *right,
      const char *file,
      unsigned int line,
      const char *left_expr,
      const char *right_expr);

int get_failures_count();
