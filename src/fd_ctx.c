#include "fd_ctx.h"
#include "fdutils.h"
#include "log.h"

#include <stdarg.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>

#define RETURN_ERROR(msg) { log_write(msg); return NULL; }

fd_ctx_t *fd_ctx_new(int fd, int type) {
   fd_ctx_t *ret = malloc(sizeof(fd_ctx_t));
   if(!ret) RETURN_ERROR("[fd_ctx] malloc failed");
   memset(ret, 0, sizeof(fd_ctx_t));

   #ifdef DEBUG_LOG
   strcpy(ret->name, "<not set>");
   #endif

   ret->fd = fd;
   ret->type = type;
   buf_reset(&ret->outBuf);
   
   ret->client = NULL;
   return ret;
}

void fd_ctx_set_name(fd_ctx_t *this, const char *fmt, ...) {
   #ifdef DEBUG_LOG
   va_list args;
   va_start(args, fmt);
   vsnprintf(this->name, sizeof(this->name), fmt, args);
   va_end(args);
   #endif
}

void fd_ctx_free(fd_ctx_t *this) {
   if(this->client) {
      fcgi_parser_free(this->client->msg_parser);
      fcgi_params_parser_free(this->client->params_parser);
      free(this->client);
   }
   free(this);
}

fd_ctx_t *fd_ctx_client_accept(fd_ctx_t *listener) {
   struct sockaddr_un client_sockaddr;
   unsigned int len = sizeof(client_sockaddr);
   int client_sock = accept(listener->fd, (struct sockaddr *) &client_sockaddr, &len);
   if(client_sock == -1) return NULL;
   fd_setnonblocking(client_sock);
   fd_setcloseonexec(client_sock);
   return fd_new_client_ctx(client_sock);
}

fd_ctx_t *fd_new_client_ctx(int fd) {
   static unsigned int ctr = 1;
   fd_ctx_t *ret = fd_ctx_new(fd, STDFPM_FCGI_CLIENT);
   if(!ret) {
      RETURN_ERROR("[fd_new_client_ctx] failed to create fd_ctx");
   }
   fd_ctx_set_name(ret, "client_%d", ctr++);
   ret->client = malloc(sizeof(fcgi_client_t));
   if(!ret->client) {
      RETURN_ERROR("[fd_new_client_ctx] fcgi_client malloc failed");
   }
   memset(ret->client, 0, sizeof(fcgi_client_t));

   ret->client->msg_parser = fcgi_parser_new();
   ret->client->params_parser = fcgi_params_parser_new(4096);
   if(!ret->client->msg_parser || !ret->client->params_parser) {
      RETURN_ERROR("[fd_new_client_ctx] FastCGI parsers malloc failed");
   }

   ret->client->msg_parser->userdata = ret;
   ret->client->params_parser->userdata = ret;
   return ret;
}

fd_ctx_t *fd_new_process_ctx(fcgi_process_t *proc) {
   fd_ctx_t *ret = fd_ctx_new(proc->fd, STDFPM_FCGI_PROCESS);
   if(!ret) {
      RETURN_ERROR("[fd_new_process_ctx] failed to create fd_ctx");
   }
   ret->process = proc;

   static unsigned int ctr = 1;
   fd_ctx_set_name(ret, "responder_%d", ctr++);
   return ret;
}

void fd_ctx_bidirectional_pipe(fd_ctx_t *ctx1, fd_ctx_t *ctx2) {
   ctx2->pipeTo = ctx1;
   ctx1->pipeTo = ctx2;
}
