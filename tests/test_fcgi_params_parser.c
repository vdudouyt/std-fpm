#include <string.h>
#include "../fcgi_params_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include "asserts.h"

typedef struct {
   char *key, *value;
} kvpair;

#define MAX_GOT_ANSWERS 5
unsigned int count = 0;
kvpair got_answers[MAX_GOT_ANSWERS];

static void reset_answers() {
   for(int i = 0; i < count; i++) {
      free(got_answers[i].key);
      free(got_answers[i].value);
   }
   count = 0;
}

static void callback(const char *key, const char *value, void *data) {
   if(count < MAX_GOT_ANSWERS) {
      got_answers[count].key = strdup(key);
      got_answers[count].value = strdup(value);
   }
   count++;
}

static void test_basic_case() {
   reset_answers();
   fcgi_params_parser_t *parser = fcgi_params_parser_new(4096);
   parser->callback = callback;
   fcgi_params_parser_write(parser, "\x80\x00\x00\x05", 4);
   fcgi_params_parser_write(parser, "\x80\x00\x00\x08", 4);
   fcgi_params_parser_write(parser, "NAMEW12345678", 13);
   fcgi_params_parser_write(parser, "\x80\x00\x00\x05\x80\x00\x00\x08", 8);
   fcgi_params_parser_write(parser, "1111W76543210", 13);
   fcgi_params_parser_free(parser);

   ASSERT_EQUAL(count, 2);
   ASSERT_STR_EQUAL(got_answers[0].key, "NAMEW");
   ASSERT_STR_EQUAL(got_answers[0].value, "12345678");
   ASSERT_STR_EQUAL(got_answers[1].key, "1111W");
   ASSERT_STR_EQUAL(got_answers[1].value, "76543210");
}

static char input_str[] = "\x80\x00\x00\x05\x80\x00\x00\x08NAMEW12345678\x80\x00\x00\x05\x80\x00\x00\x08""1111W76543210";

static void test_at_once() {
   reset_answers();
   fcgi_params_parser_t *parser = fcgi_params_parser_new(4096);
   parser->callback = callback;
   fcgi_params_parser_write(parser, input_str, sizeof(input_str) - 1);
   fcgi_params_parser_free(parser);

   ASSERT_EQUAL(count, 2);
   ASSERT_STR_EQUAL(got_answers[0].key, "NAMEW");
   ASSERT_STR_EQUAL(got_answers[0].value, "12345678");
   ASSERT_STR_EQUAL(got_answers[1].key, "1111W");
   ASSERT_STR_EQUAL(got_answers[1].value, "76543210");
}

static void test_char_by_char() {
   reset_answers();
   fcgi_params_parser_t *parser = fcgi_params_parser_new(4096);
   parser->callback = callback;

   for(int i = 0; i < sizeof(input_str) - 1; i++) {
      fcgi_params_parser_write(parser, &input_str[i], 1);
   }

   fcgi_params_parser_free(parser);

   ASSERT_EQUAL(count, 2);
   ASSERT_STR_EQUAL(got_answers[0].key, "NAMEW");
   ASSERT_STR_EQUAL(got_answers[0].value, "12345678");
   ASSERT_STR_EQUAL(got_answers[1].key, "1111W");
   ASSERT_STR_EQUAL(got_answers[1].value, "76543210");
}

void test_fcgi_params_parser() {
   test_basic_case();
   test_at_once();
   test_char_by_char();
   reset_answers();
}
