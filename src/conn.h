#pragma once
#include <uv.h>
#include <stdbool.h>
#include "fcgi_parser.h"
#include "worker.h"

typedef struct conn_s conn_t;

struct conn_s {
   enum { STDFPM_FCGI_CLIENT, STDFPM_FCGI_PROCESS } type;
   uv_pipe_t *pipe;
   struct conn_s *pairWith;
   bool closeAfterWrite;

   #ifdef DEBUG_LOG
   char name[64];
   #endif

   fcgi_parser_t fcgiParser;
};

conn_t *conn_new(uv_pipe_t *pipe, int type);
void conn_set_name(conn_t *this, const char *fmt, ...);
void conn_free(conn_t *this);

conn_t *conn_client_accept(conn_t *listener);
conn_t *fd_new_client_conn(uv_pipe_t *pipe);
//conn_t *fd_new_process_conn(fcgi_process_t *proc);
void conn_bidirectional_pipe(conn_t *conn1, conn_t *conn2);
