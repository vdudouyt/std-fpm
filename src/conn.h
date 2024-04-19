#pragma once
#include <uv.h>
#include <stdbool.h>
#include "fcgi_parser.h"
#include "fcgi_process.h"
#include "worker.h"

typedef struct conn_s conn_t;

struct conn_s {
   enum { STDFPM_FCGI_CLIENT, STDFPM_FCGI_PROCESS } type;
   uv_pipe_t *pipe;
   struct conn_s *pairedWith;
   unsigned int pendingWrites;
   bool isConnecting;

   #ifdef DEBUG_LOG
   char name[64];
   #endif
   // whenever to retry or drop client connection on fastcgi process connection error
   enum { RETRY_ON_FAILURE, CLOSE_ON_FAILURE } probeMode;
   uv_buf_t storedBuf;
   fcgi_parser_t fcgiParser;
   fcgi_process_t *process;
};

conn_t *conn_new(uv_pipe_t *pipe, int type);
void conn_set_name(conn_t *this, const char *fmt, ...);
void conn_free(conn_t *this);

conn_t *conn_client_accept(conn_t *listener);
conn_t *fd_new_client_conn(uv_pipe_t *pipe);
conn_t *fd_new_process_conn(fcgi_process_t *proc, uv_pipe_t *pipe);
void conn_bidirectional_pipe(conn_t *conn1, conn_t *conn2);
