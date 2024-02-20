#pragma once

typedef struct fd_ctx_s fd_ctx_t;
typedef struct fcgi_client_s fcgi_client_t;

struct fcgi_client_s {
   fcgi_parser_t *msg_parser;
   fcgi_params_parser_t *params_parser;
};

struct fd_ctx_s {
   int fd;
   int bytes_in_buf, write_pos;
   enum { STDFPM_LISTEN_SOCK = 1, STDFPM_FCGI_CLIENT, STDFPM_FCGI_PROCESS } type;

   char name[64];
   char outBuf[65536];
   fcgi_client_t *client;
};
