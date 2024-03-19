#pragma once
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <stdbool.h>
#include "fcgi_process.h"
#include "fcgi_parser.h"
#include "fcgi_params_parser.h"

typedef struct fd_ctx_s fd_ctx_t;
typedef struct fcgi_client_s fcgi_client_t;

struct fcgi_client_s {
   fcgi_parser_t *msg_parser;
   fcgi_params_parser_t *params_parser;
   struct evbuffer *inMemoryBuf;
};

struct fd_ctx_s {
   struct bufferevent *bev;
   enum { STDFPM_FCGI_CLIENT, STDFPM_FCGI_PROCESS } type;
   struct fd_ctx_s *pipeTo;
   bool closeAfterWrite;

   #ifdef DEBUG_LOG
   char name[64];
   #endif

   fcgi_client_t *client;
   fcgi_process_t *process;
};

fd_ctx_t *fd_ctx_new(struct bufferevent *bev, int type);
void fd_ctx_set_name(fd_ctx_t *this, const char *fmt, ...);
void fd_ctx_free(fd_ctx_t *this);

fd_ctx_t *fd_ctx_client_accept(fd_ctx_t *listener);
fd_ctx_t *fd_new_client_ctx(struct bufferevent *bev);
fd_ctx_t *fd_new_process_ctx(fcgi_process_t *proc);
void fd_ctx_bidirectional_pipe(fd_ctx_t *ctx1, fd_ctx_t *ctx2);
