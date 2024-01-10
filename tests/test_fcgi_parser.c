#include <string.h>
#include "../fcgi_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include "asserts.h"

#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10

char input1[] = "\x01\x01\x00\x01\x00\x08\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x01\x04\x00\x01\x01\xf4\x04\x00\x09\x00PATH_INFO\x0f\x17SCRIPT_FILENAME/var/www/html/test.fcgi\x0c\x00QUERY_STRING\x0e\x03REQUEST_METHODGET\x0c\x00""CONTENT_TYPE\x0e\x00""CONTENT_LENGTH\x0b\x0aSCRIPT_NAME/test.fcgi\x0b\x0aREQUEST_URI/test.fcgi\x0c\x0a""DOCUMENT_URI/test.fcgi\x0d\x0d""DOCUMENT_ROOT/var/www/html\x0f\x08SERVER_PROTOCOLHTTP/1.1\x0e\x04REQUEST_SCHEMEhttp\x11\x07GATEWAY_INTERFACECGI/1.1\x0f\x0cSERVER_SOFTWAREnginx/1.18.0\x0b\x09REMOTE_ADDR127.0.0.1\x0b\x05REMOTE_PORT44328\x0b\x00REMOTE_USER\x0b\x09SERVER_ADDR127.0.0.1\x0b\x02SERVER_PORT80\x0b\x01SERVER_NAME_\x0f\x03REDIRECT_STATUS200\x09\x09HTTP_HOSTlocalhost\x0f\x0bHTTP_USER_AGENTcurl/7.74.0\x0b\x03HTTP_ACCEPT*/*\x00\x00\x00\x00\x01\x04\x00\x01\x00\x00\x00\x00\x01\x05\x00\x01\x00\x00\x00\x00";
char input2[] = "\x01\x08\x00\x03\x00\x0a\x00\x00HELLOTHERE";

#define MAX_GOT_ANSWERS 16
static fcgi_header_t got_answers[MAX_GOT_ANSWERS];
char last_data[32];
static unsigned int count = 0;

#define MIN(a, b) ((a) < (b) ? (a) : (b))

void callback(const fcgi_header_t *hdr, const char *data, void *userdata) {
   memset(last_data, 0, sizeof(last_data)); 
   strncpy(last_data, data, MIN(hdr->contentLength, sizeof(last_data)-1));
   if(count < MAX_GOT_ANSWERS) {
      memcpy(&got_answers[count], hdr, sizeof(fcgi_header_t));
   }
   count++;
}

#define ASSERT_INPUT1_AT(start_idx) \
   ASSERT_EQUAL(got_answers[start_idx+0].type, FCGI_BEGIN_REQUEST); \
   ASSERT_EQUAL(got_answers[start_idx+0].requestId, 1); \
   ASSERT_EQUAL(got_answers[start_idx+0].contentLength, 8); \
   ASSERT_EQUAL(got_answers[start_idx+0].paddingLength, 0); \
   ASSERT_EQUAL(got_answers[start_idx+1].type, FCGI_PARAMS); \
   ASSERT_EQUAL(got_answers[start_idx+1].requestId, 1); \
   ASSERT_EQUAL(got_answers[start_idx+1].contentLength, 500); \
   ASSERT_EQUAL(got_answers[start_idx+1].paddingLength, 4); \
   ASSERT_EQUAL(got_answers[start_idx+2].type, FCGI_PARAMS); \
   ASSERT_EQUAL(got_answers[start_idx+2].requestId, 1); \
   ASSERT_EQUAL(got_answers[start_idx+2].contentLength, 0); \
   ASSERT_EQUAL(got_answers[start_idx+2].paddingLength, 0); \
   ASSERT_EQUAL(got_answers[start_idx+3].type, FCGI_STDIN); \
   ASSERT_EQUAL(got_answers[start_idx+3].requestId, 1); \
   ASSERT_EQUAL(got_answers[start_idx+3].contentLength, 0); \
   ASSERT_EQUAL(got_answers[start_idx+3].paddingLength, 0);

#define ASSERT_INPUT2_AT(start_idx) \
   ASSERT_EQUAL(got_answers[start_idx+0].type, 8); \
   ASSERT_EQUAL(got_answers[start_idx+0].requestId, 3); \
   ASSERT_EQUAL(got_answers[start_idx+0].contentLength, 10); \
   ASSERT_EQUAL(got_answers[start_idx+0].paddingLength, 0); \

static void test_basic_case() {
   count = 0;
   fcgi_parser_t *parser = fcgi_parser_new();
   parser->callback = callback;
   fcgi_parser_write(parser, input1, sizeof(input1) - 1);
   ASSERT_EQUAL(count, 4)
   fcgi_parser_write(parser, input2, sizeof(input2) - 1);
   ASSERT_EQUAL(count, 5)
   ASSERT_STR_EQUAL(last_data, "HELLOTHERE");
   fcgi_parser_free(parser);

   ASSERT_INPUT1_AT(0)
   ASSERT_INPUT2_AT(4)
}

static void test_at_once() {
   count = 0;
   static char input[sizeof(input1) + sizeof(input2) - 2];
   memcpy(input, input1, sizeof(input1)-1);
   memcpy(&input[sizeof(input1)-1], input2, sizeof(input2)-1);

   fcgi_parser_t *parser = fcgi_parser_new();
   parser->callback = callback;
   fcgi_parser_write(parser, input, sizeof(input));
   fcgi_parser_free(parser);

   ASSERT_EQUAL(count, 5)
   ASSERT_INPUT1_AT(0)
   ASSERT_INPUT2_AT(4)
}

static void test_char_by_char() {
   count = 0;
   fcgi_parser_t *parser = fcgi_parser_new();
   parser->callback = callback;

   for(int i = 0; i < sizeof(input1) - 1; i++) {
      fcgi_parser_write(parser, &input1[i], 1);
   }

   ASSERT_EQUAL(count, 4)

   for(int i = 0; i < sizeof(input2) - 1; i++) {
      fcgi_parser_write(parser, &input2[i], 1);
   }

   fcgi_parser_free(parser);

   ASSERT_EQUAL(count, 5)
   ASSERT_INPUT1_AT(0)
   ASSERT_INPUT2_AT(4)
}

void test_fcgi_parser() {
   test_basic_case();
   test_at_once();
   test_char_by_char();
}
