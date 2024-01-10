#pragma once

typedef void (*fcgi_params_parser_callback_t)(const char *key, const char *value, void *userdata);

typedef struct {
   unsigned int status;
   unsigned char *buf;
   unsigned int pos, buf_size;
   unsigned int key_length, value_length;
   fcgi_params_parser_callback_t callback;
   void *userdata;
} fcgi_params_parser_t;

fcgi_params_parser_t *fcgi_params_parser_new(unsigned int buf_size);
void fcgi_params_parser_free(fcgi_params_parser_t *this);
void fcgi_params_parser_dump(fcgi_params_parser_t *this);
void fcgi_params_parser_write(fcgi_params_parser_t *this, const char *input, unsigned int length);
