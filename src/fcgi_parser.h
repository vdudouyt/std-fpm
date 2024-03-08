#pragma once
#include <stdint.h>

typedef struct {
   uint8_t version;
   uint8_t type;
   uint16_t requestId;
   uint16_t contentLength;
   uint8_t paddingLength;
   uint8_t reserved;
} fcgi_header_t;

typedef void (*fcgi_parser_callback_t)(const fcgi_header_t *hdr, const char *data, void *userdata);

typedef struct {
   unsigned int status;
   unsigned char buf[8 + 65536 + 256]; // header + max content length + max padding length
   unsigned int pos;

   fcgi_header_t hdr;
   fcgi_parser_callback_t callback;
   void *userdata;
} fcgi_parser_t;

fcgi_parser_t *fcgi_parser_new();
void fcgi_parser_write(fcgi_parser_t *this, const uint8_t *input, unsigned int length);
void fcgi_parser_free(fcgi_parser_t *this);

