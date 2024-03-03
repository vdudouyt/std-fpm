#include "fd_ctx.h"
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>

fd_ctx_t *fd_ctx_new(int fd, int type) {
   fd_ctx_t *ret = malloc(sizeof(fd_ctx_t));
   assert(ret);
   memset(ret, 0, sizeof(fd_ctx_t));

   sprintf(ret->name, "<not set>");
   ret->fd = fd;
   ret->type = type;
   buf_reset(&ret->outBuf);
   
   strcpy(ret->name, "");
   ret->client = NULL;
   return ret;
}

void fd_ctx_set_name(fd_ctx_t *this, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   vsnprintf(this->name, sizeof(this->name), fmt, args);
   va_end(args);
}

void fd_ctx_free(fd_ctx_t *this) {
   if(this->client) {
      fcgi_parser_free(this->client->msg_parser);
      fcgi_params_parser_free(this->client->params_parser);
      free(this->client);
   }
   free(this);
}

fd_ctx_t *fd_new_process_ctx(fcgi_process_t *proc) {
   fd_ctx_t *ret = fd_ctx_new(proc->fd, STDFPM_FCGI_PROCESS);
   ret->process = proc;

   static unsigned int ctr = 1;
   fd_ctx_set_name(ret, "responder_%d", ctr++);
   return ret;
}

void fd_ctx_bidirectional_pipe(fd_ctx_t *ctx1, fd_ctx_t *ctx2) {
   ctx2->pipeTo = ctx1;
   ctx1->pipeTo = ctx2;
}
