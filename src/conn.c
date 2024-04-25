#include "conn.h"
#include "fdutils.h"
#include "log.h"
#include "debug.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <errno.h>

#define RETURN_ERROR(...) { log_write(__VA_ARGS__); return NULL; }

conn_t *conn_new(struct bufferevent *bev, int type) {
   conn_t *ret = malloc(sizeof(conn_t));
   if(!ret) RETURN_ERROR("[conn] malloc failed");
   memset(ret, 0, sizeof(conn_t));

   ret->bev = bev;
   ret->type = type;
   return ret;
}

void conn_set_name(conn_t *this, const char *fmt, ...) {
   #ifdef DEBUG_LOG
   va_list args;
   va_start(args, fmt);
   vsnprintf(this->name, sizeof(this->name), fmt, args);
   va_end(args);
   DEBUG("[%s] new conn created", this->name);
   #endif
}

void conn_free(conn_t *this) {
   if(this->client) {
      evbuffer_free(this->client->inMemoryBuf);
      free(this->client);
   }
   DEBUG("[%s] freed", this->name);
   free(this);
}

conn_t *fd_new_client_conn(struct bufferevent *bev) {
   conn_t *ret = conn_new(bev, STDFPM_FCGI_CLIENT);
   if(!ret) {
      RETURN_ERROR("[fd_new_client_conn] failed to create conn");
   }
   ret->client = malloc(sizeof(fcgi_client_t));
   if(!ret->client) {
      RETURN_ERROR("[fd_new_client_conn] fcgi_client malloc failed");
   }
   memset(ret->client, 0, sizeof(fcgi_client_t));
   fcgi_parser_init(&ret->client->fcgiParser);

   ret->client->inMemoryBuf = evbuffer_new();
   if(!ret->client->inMemoryBuf) {
      RETURN_ERROR("[fd_new_client_conn] evbuffer allocation failed");
   }

   #ifdef DEBUG_LOG
   static unsigned int ctr = 1;
   conn_set_name(ret, "client_%d", ctr++);
   #endif

   return ret;
}

conn_t *fd_new_process_conn(fcgi_process_t *proc, struct bufferevent *bev) {
   conn_t *ret = conn_new(bev, STDFPM_FCGI_PROCESS);
   if(!ret) {
      RETURN_ERROR("[fd_new_process_conn] failed to create conn");
   }
   ret->process = proc;

   #ifdef DEBUG_LOG
   static unsigned int ctr = 1;
   conn_set_name(ret, "responder_%d", ctr++);
   #endif
   return ret;
}
