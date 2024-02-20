#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
//#include <gmodule.h>

#include "fcgi_parser.h"
#include "fcgi_params_parser.h"
#include "main.h"
#include "fcgitypes.h"
#include "log.h"

#define BUF_SIZE 65536

FILE *MAINLOG = NULL;
int epollfd;

static void setnonblocking(int fd) {
   int flags = fcntl(fd, F_GETFL, 0);
   assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
}

static void setcloseonexec(int fd) {
   int flags = fcntl(fd, F_GETFD, 0);
   assert(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1);
}

static const char *conntype_to_str(unsigned int type) {
   static const char LISTEN_SOCK[] = "LISTEN_SOCK",
      FCGI_CLIENT[] = "FCGI_CLIENT",
      FCGI_PROCESS[] = "FCGI_PROCESS",
      UNKNOWN[] = "UNKNOWN";
   switch(type) {
      case STDFPM_LISTEN_SOCK: return LISTEN_SOCK;
      case STDFPM_FCGI_CLIENT: return FCGI_CLIENT;
      case STDFPM_FCGI_PROCESS: return FCGI_PROCESS;
      default: return UNKNOWN;
   }
}

fd_ctx_t *fd_ctx_new(int fd, int type) {
   fd_ctx_t *ret = malloc(sizeof(fd_ctx_t));
   assert(ret);

   ret->fd = fd;
   ret->bytes_in_buf = ret->write_pos = 0;
   ret->type = type;
   
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

static int create_listening_socket() {
   int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
   assert(listen_sock != -1);

   static char sock_path[] = "/tmp/std-fpm.sock";
   struct sockaddr_un s_un;
   s_un.sun_family = AF_UNIX;
   strcpy(s_un.sun_path, sock_path);
   unlink(s_un.sun_path);

   assert(bind(listen_sock, (struct sockaddr *) &s_un, sizeof(s_un)) != -1);
   chmod(s_un.sun_path, 0777);
   assert(listen(listen_sock, 1024) != -1);
   setnonblocking(listen_sock);

   return listen_sock;
}

void onconnect(fd_ctx_t *lctx);
void onsocketread(fd_ctx_t *ctx);
void ondisconnect(fd_ctx_t *ctx);
void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata);
void onfcgiparam(const char *key, const char *value, void *userdata);

void onconnect(fd_ctx_t *lctx) {
   struct sockaddr_un client_sockaddr;
   int len = sizeof(client_sockaddr);
   int client_sock = accept(lctx->fd, (struct sockaddr *) &client_sockaddr, &len);
   assert(client_sock != -1);
   setnonblocking(client_sock);
   setcloseonexec(client_sock);

   static unsigned int ctr = 1;
   fd_ctx_t *ctx = fd_ctx_new(client_sock, STDFPM_FCGI_CLIENT);
   fd_ctx_set_name(ctx, "client_%d", ctr++);
   ctx->client = malloc(sizeof(fcgi_client_t));
   memset(ctx->client, 0, sizeof(fcgi_client_t));
   log_write(MAINLOG, "[%s] new connection accepted: %s", lctx->name, ctx->name);

   ctx->client->msg_parser = fcgi_parser_new();
   ctx->client->msg_parser->callback = onfcgimessage;
   ctx->client->msg_parser->userdata = ctx;

   ctx->client->params_parser = fcgi_params_parser_new(4096);
   ctx->client->params_parser->callback = onfcgiparam;
   ctx->client->params_parser->userdata = ctx;

   struct epoll_event ev;
   memset(&ev, 0, sizeof(struct epoll_event));
   ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
   ev.data.ptr = ctx;

   assert(epoll_ctl(epollfd, EPOLL_CTL_ADD, ctx->fd, &ev) == 0);
}

void hexdump(const unsigned char *buf, size_t size) {
   for(int i = 0; i < size; i++) {
      printf("%02x ", buf[i]);;
   }
   printf("\n");
}

void onsocketread(fd_ctx_t *ctx) {
   log_write(MAINLOG, "[%s] starting onsocketread()", ctx->name);
   static char buf[4096];
   int bytes_read;
   while((bytes_read = recv(ctx->fd, buf, sizeof(buf), 0)) > 0) {
      log_write(MAINLOG, "[%s] received %d bytes", ctx->name, bytes_read);
      hexdump(buf, bytes_read);

      if(ctx->type == STDFPM_FCGI_CLIENT) {
         log_write(MAINLOG, "[%s] forwarded %d bytes to FastCGI parser", ctx->name, bytes_read);
         fcgi_parser_write(ctx->client->msg_parser, buf, bytes_read);
      }
   }
}

void onsocketwriteok(fd_ctx_t *ctx) {
   log_write(MAINLOG, "[%s] socket is ready for writing", ctx->name);
}

static void escape(unsigned char *out, const unsigned char *in, size_t input_length) {
   unsigned int d = 0;
   for(unsigned int i = 0; i < input_length; i++) {
      if(isprint(in[i])) {
         out[d++] = in[i];
      } else {
         out[d++] = '\\';
         out[d++] = 'x';
         sprintf(&out[d], "%02x", in[i]);
         d += 2;
      }
   }
   out[d++] = '\0';
}

void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata) {
   fd_ctx_t *ctx = userdata;

   log_write(MAINLOG, "[%s] got fcgi message: { type = %s, requestId = 0x%02x, contentLength = %d }",
      ctx->name, fcgitype_to_string(hdr->type), hdr->requestId, hdr->contentLength);
   if(hdr->contentLength) {
      char escaped_data[4*65536+1];
      escape(escaped_data, data, hdr->contentLength);
      log_write(MAINLOG, "[%s] message content: \"%s\"", ctx->name, escaped_data);
   }

   if(hdr->type == FCGI_PARAMS) fcgi_params_parser_write(ctx->client->params_parser, data, hdr->contentLength);
}

void onfcgiparam(const char *key, const char *value, void *userdata) {
   fd_ctx_t *ctx = userdata;
   if(!strcmp(key, "SCRIPT_FILENAME")) {
      log_write(MAINLOG, "[%s] got script filename: %s", ctx->name, value);
   }
}

void ondisconnect(fd_ctx_t *ctx) {
   log_write(MAINLOG, "[%s] connection closed, removing from interest", ctx->name);
   epoll_ctl(epollfd, EPOLL_CTL_DEL, ctx->fd, NULL);
   fd_ctx_free(ctx);
}

int main() {
   int listen_sock = create_listening_socket();
   epollfd = epoll_create( 0xCAFE );

   assert(epollfd != -1);

   fd_ctx_t *ctx = fd_ctx_new(listen_sock, STDFPM_LISTEN_SOCK);
   fd_ctx_set_name(ctx, "listen_sock");
   log_set_echo(true);
   log_write(MAINLOG, "[%s] server created", ctx->name);

   struct epoll_event ev;
   memset(&ev, 0, sizeof(struct epoll_event));
   ev.events = EPOLLIN;
   ev.data.ptr = ctx;
   assert(epoll_ctl(epollfd, EPOLL_CTL_ADD, ctx->fd, &ev) == 0);

   const unsigned int EVENTS_COUNT = 20;
   struct epoll_event pevents[EVENTS_COUNT];

   while(1) {
      int event_count = epoll_wait(epollfd, pevents, EVENTS_COUNT, 10000);
      if(event_count < 0) exit(-1);

      for(int i = 0; i < event_count; i++) {
         fd_ctx_t *ctx = pevents[i].data.ptr;
         if(ctx->type == STDFPM_LISTEN_SOCK) {
            onconnect(ctx);
            continue;
         }
         if(pevents[i].events & EPOLLIN) onsocketread(ctx);
         if(pevents[i].events & EPOLLOUT) onsocketwriteok(ctx);
         if(pevents[i].events & EPOLLHUP) ondisconnect(ctx);
      }
   }
}
