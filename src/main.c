#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/epoll.h>
#include <assert.h>
#include <stdarg.h>
#include "debug.h"
#include "log.h"
#include "fdutils.h"
#include "fcgi_parser.h"
#include "buf.h"

#define EXIT_WITH_ERROR(...) { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); exit(-1); }

static int stdfpm_create_listening_socket(const char *sock_path) {
   int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
   if(listen_sock == -1) EXIT_WITH_ERROR("[main] couldn't create a listener socket: %s", strerror(errno));

   struct sockaddr_un s_un;
   s_un.sun_family = AF_UNIX;
   strcpy(s_un.sun_path, sock_path);

   if(connect(listen_sock, (struct sockaddr *) &s_un, sizeof(s_un)) == -1) {
      unlink(s_un.sun_path); // Socket is in use, prepare for binding
   }

   if(bind(listen_sock, (struct sockaddr *) &s_un, sizeof(s_un)) == -1) {
      EXIT_WITH_ERROR("[main] failed to bind a unix domain socket at %s: %s", s_un.sun_path, strerror(errno));
   }

   chmod(s_un.sun_path, 0777);
   if(listen(listen_sock, 1024) == -1) {
      EXIT_WITH_ERROR("[main] failed to listen a unix domain socket at %s: %s", s_un.sun_path, strerror(errno));
   }

   fd_setnonblocking(listen_sock);
   fd_setcloseonexec(listen_sock);

   return listen_sock;
}

typedef struct stdfpm_context_s {
   enum { STDFPM_LISTENER, STDFPM_FCGI_CLIENT, STDFPM_FCGI_PROCESS } type;
   int fd, epollfd;
   struct stdfpm_context_s *pairedWith;
   bool toDelete;
   stdfpm_buf_t buf;
   fcgi_parser_t fcgiParser;

   #ifdef DEBUG_LOG
   char name[64];
   #endif
} stdfpm_context_t;

static void stdfpm_epoll_ctl(stdfpm_context_t *ctx, int op, uint32_t events) {
   struct epoll_event ev;
   memset(&ev, 0, sizeof(struct epoll_event));
   ev.events = events;
   ev.data.ptr = ctx;

   if(epoll_ctl(ctx->epollfd, op, ctx->fd, &ev) != 0) {
      DEBUG("[%s] stdfpm_epoll_ctl failed: op=%d events=0x%08x", ctx->name, op, events);
   }
}

static stdfpm_context_t *stdfpm_create_context(unsigned int type) {
   stdfpm_context_t *ctx = malloc(sizeof(stdfpm_context_t));
   memset(ctx, 0, sizeof(stdfpm_context_t));
   ctx->type = type;
   fd_setnonblocking(ctx->fd);
   fd_setcloseonexec(ctx->fd);
   return ctx;
}

#ifdef DEBUG_LOG
void stdfpm_context_set_name(stdfpm_context_t *ctx, const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   vsnprintf(ctx->name, sizeof(ctx->name), fmt, args);
   va_end(args);
   DEBUG("[%s] new connection created", ctx->name);
}
#endif

#define RETURN_ERROR(msg) { log_write(msg); return NULL; }

static stdfpm_context_t *stdfpm_accept(stdfpm_context_t *listenCtx) {
   struct sockaddr_un client_sockaddr;
   unsigned int len = sizeof(client_sockaddr);
   int client_sock = accept(listenCtx->fd, (struct sockaddr *) &client_sockaddr, &len);
   if(client_sock == -1) RETURN_ERROR("[fd_ctx] failed while accepting socket");
   stdfpm_context_t *newCtx = stdfpm_create_context(STDFPM_FCGI_CLIENT);
   newCtx->fd = client_sock;
   newCtx->epollfd = listenCtx->epollfd;
   fcgi_parser_init(&newCtx->fcgiParser);
   #ifdef DEBUG_LOG
   static int ctr = 0;
   ctr++;
   stdfpm_context_set_name(newCtx, "client_%d", ctr);
   DEBUG("[%s] client accepted: %s", listenCtx->name, newCtx->name);
   #endif
   return newCtx;
}

void stdfpm_onconnect(stdfpm_context_t *listenCtx) {
   stdfpm_context_t *newctx = stdfpm_accept(listenCtx);
   stdfpm_epoll_ctl(newctx, EPOLL_CTL_ADD, EPOLLIN | EPOLLRDHUP);
}

void stdfpm_onsocketreadable(stdfpm_context_t *ctx) {
   DEBUG("[%s] stdfpm_onsocketreadable", ctx->name);
   ssize_t bytes_read = buf_recv(&ctx->buf, ctx->fd);
   DEBUG("[%s] %ld bytes read", ctx->name, bytes_read);
   if(bytes_read <= 0) return;

   if(ctx->type == STDFPM_FCGI_CLIENT) {
      fcgi_parse(&ctx->fcgiParser, ctx->buf.lastWrite, ctx->buf.lastWriteLen);
      char *script_filename = fcgi_get_script_filename(&ctx->fcgiParser);

      if(script_filename) {
         DEBUG("[%s] parsed script_filename: %s", ctx->name, script_filename);
      }
   }
}

void stdfpm_onsocketwriteable(stdfpm_context_t *ctx) {
   DEBUG("[%s] stdfpm_onsocketwriteable", ctx->name);
}

void stdfpm_ondisconnect(stdfpm_context_t *ctx) {
   DEBUG("[%s] stdfpm_ondisconnect", ctx->name);
   ctx->toDelete = true;
}

int main(int argc, char **argv) {
   log_set_echo(true);
   signal(SIGPIPE, SIG_IGN);
   signal(SIGCHLD, SIG_IGN);

   stdfpm_context_t *listenCtx = stdfpm_create_context(STDFPM_LISTENER);
   listenCtx->fd = stdfpm_create_listening_socket("/tmp/std-fpm.sock");
   assert(listenCtx->fd != -1);
   listenCtx->epollfd = epoll_create(0xCAFE);
   assert(listenCtx->epollfd != -1);
   stdfpm_epoll_ctl(listenCtx, EPOLL_CTL_ADD, EPOLLIN);
   #ifdef DEBUG_LOG
   stdfpm_context_set_name(listenCtx, "listen_sock");
   #endif

   const unsigned int EVENTS_COUNT = 20;
   struct epoll_event pevents[EVENTS_COUNT];

   while(1) {
      int event_count = epoll_wait(listenCtx->epollfd, pevents, EVENTS_COUNT, 10000);
      if(event_count < 0) {
         perror("epoll");
         exit(-1);
      }

      if(event_count == 0) continue;

      for(int i = 0; i < event_count; i++) {
         stdfpm_context_t *ctx = pevents[i].data.ptr;
         if(ctx->type == STDFPM_LISTENER) {
            stdfpm_onconnect(ctx);
            continue;
         }
         if(pevents[i].events & EPOLLIN) stdfpm_onsocketreadable(ctx);
         if(pevents[i].events & EPOLLOUT) stdfpm_onsocketwriteable(ctx);
         if(pevents[i].events & EPOLLRDHUP) stdfpm_ondisconnect(ctx);
      }

      // TODO: is each ctx guaranteed to appear only once?
      for(int i = 0; i < event_count; i++) {
         stdfpm_context_t *ctx = pevents[i].data.ptr;
         if(ctx->toDelete) {
            DEBUG("freeing %s", ctx->name);
            stdfpm_epoll_ctl(ctx, EPOLL_CTL_DEL, 0);
            close(ctx->fd);
            buf_release(&ctx->buf);
            free(ctx);
         }
      }
   }
}
