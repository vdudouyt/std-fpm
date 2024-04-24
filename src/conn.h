#pragma once
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <stdbool.h>
#include "fcgi_process.h"
#include "fcgi_parser.h"
#include "fcgi_params_parser.h"
#include "config.h"

typedef struct conn_s conn_t;
typedef struct fcgi_client_s fcgi_client_t;

struct fcgi_client_s {
   fcgi_parser_t *msg_parser;
   fcgi_params_parser_t *params_parser;
   struct evbuffer *inMemoryBuf;
};

struct conn_s {
   stdfpm_config_t *config;
   struct bufferevent *bev;
   enum { STDFPM_FCGI_CLIENT, STDFPM_FCGI_PROCESS } type;
   struct conn_s *pairedWith;
   bool closeAfterWrite;

   #ifdef DEBUG_LOG
   char name[64];
   #endif

   fcgi_client_t *client;
   fcgi_process_t *process;
};

conn_t *conn_new(struct bufferevent *bev, int type);
void conn_set_name(conn_t *this, const char *fmt, ...);
void conn_free(conn_t *this);

conn_t *conn_client_accept(conn_t *listener);
conn_t *fd_new_client_conn(struct bufferevent *bev);
conn_t *fd_new_process_conn(fcgi_process_t *proc);
void conn_bidirectional_pipe(conn_t *conn1, conn_t *conn2);
