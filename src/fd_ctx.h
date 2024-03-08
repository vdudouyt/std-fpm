#pragma once
#include "buf.h"
#include "fcgi_process.h"
#include "fcgi_parser.h"
#include "fcgi_params_parser.h"

typedef struct fd_ctx_s fd_ctx_t;
typedef struct fcgi_client_s fcgi_client_t;

struct fcgi_client_s {
   fcgi_parser_t *msg_parser;
   fcgi_params_parser_t *params_parser;
   buf_t inMemoryBuf; // buffer to write before SCRIPT_FILENAME can be determined
};

struct fd_ctx_s {
   int fd;
   enum { STDFPM_LISTEN_SOCK = 1, STDFPM_FCGI_CLIENT, STDFPM_FCGI_PROCESS } type;
   bool eof;
   struct fd_ctx_s *pipeTo;

   #ifdef DEBUG_LOG
   char name[64];
   #endif

   buf_t outBuf;
   fcgi_client_t *client;
   fcgi_process_t *process;
};

fd_ctx_t *fd_ctx_new(int fd, int type);
void fd_ctx_set_name(fd_ctx_t *this, const char *fmt, ...);
void fd_ctx_free(fd_ctx_t *this);

fd_ctx_t *fd_ctx_client_accept(fd_ctx_t *listener);
fd_ctx_t *fd_new_client_ctx(int fd);
fd_ctx_t *fd_new_process_ctx(fcgi_process_t *proc);
void fd_ctx_bidirectional_pipe(fd_ctx_t *ctx1, fd_ctx_t *ctx2);