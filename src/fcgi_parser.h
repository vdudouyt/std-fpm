#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_PATH_LENGTH 4096

typedef struct {
   unsigned int pos;
   uint8_t type;
   uint16_t length;
   uint8_t paddingLength;
} fcgi_request_parser_t;

typedef struct {
   unsigned int pos;
   enum { READ_KEY_LENGTH = 0, READ_VALUE_LENGTH = 1, READ_PARAM = 2 } state;
   unsigned int keyLength, valueLength;
   char buf[MAX_PATH_LENGTH];
   bool gotScriptFilename;
} fcgi_params_parser_t;

typedef struct {
   fcgi_request_parser_t requestParser;
   fcgi_params_parser_t paramsParser;
} fcgi_parser_t;

void fcgi_parser_init(fcgi_parser_t *ctx);
void fcgi_parse(fcgi_parser_t *ctx, const char *buf, size_t len);
char *fcgi_get_script_filename(fcgi_parser_t *ctx);
