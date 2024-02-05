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
//#include <gmodule.h>

#include "fcgi_parser.h"
#include "fcgi_params_parser.h"

#define BUF_SIZE 65536

typedef struct {
   int fd;
   fcgi_parser_t *msg_parser;
   fcgi_params_parser_t *params_parser;

   int bytes_in_buf, write_pos;
   char buf[65536];
} conn_ctx_t;

conn_ctx_t *conn_ctx_new() {
   conn_ctx_t *ret = malloc(sizeof(conn_ctx_t));
   assert(ret);
   ret->msg_parser = fcgi_parser_new();
   ret->params_parser = fcgi_params_parser_new(4096);
   ret->bytes_in_buf = ret->write_pos = 0;
   return ret;
}

void conn_ctx_free(conn_ctx_t *this) {
   fcgi_parser_free(this->msg_parser);
   fcgi_params_parser_free(this->params_parser);
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
   printf("[main] listening at %s...\n", sock_path);
   assert(listen(listen_sock, 1024) != -1);

   int flags = fcntl(listen_sock, F_GETFL, 0);
   assert(fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK) != -1);
   return listen_sock;
}

typedef struct {
   int efd, sock;
   struct epoll_event ev;
} listen_ctx_t;

listen_ctx_t listen_ctx;

static void setnonblocking(int fd) {
   int flags = fcntl(fd, F_GETFL, 0);
   assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
}

void onconnect(struct epoll_event *evt);
void oninput(conn_ctx_t *ctx);
void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata);
void onfcgiparam(const char *key, const char *value, void *userdata);
void ondisconnect(struct epoll_event *evt);

void onconnect(struct epoll_event *evt) {
   struct sockaddr_un client_sockaddr;
   int len = sizeof(client_sockaddr);
   printf("Accepting...\n");
   int client_sock = accept(listen_ctx.sock, (struct sockaddr *) &client_sockaddr, &len);
   assert(client_sock != -1);
   setnonblocking(client_sock);

   conn_ctx_t *ctx = conn_ctx_new();
   ctx->fd = client_sock;
   ctx->msg_parser->callback = onfcgimessage;
   ctx->msg_parser->userdata = ctx;
   ctx->params_parser->callback = onfcgiparam;
   ctx->params_parser->userdata = ctx;

   listen_ctx.ev.data.ptr = ctx;
   assert(epoll_ctl(listen_ctx.efd, EPOLL_CTL_ADD, ctx->fd, &listen_ctx.ev) == 0);

   printf("Done\n");
}

void oninput(conn_ctx_t *ctx) {
   static char buf[4096];
   int bytes_read;
   while((bytes_read = recv(ctx->fd, buf, sizeof(buf), 0)) > 0) {
      printf("[main] got %d bytes: %s\n", bytes_read, buf);
      fcgi_parser_write(ctx->msg_parser, buf, bytes_read);
   }
}

void onwriteok(conn_ctx_t *ctx) {
   printf("onwriteok\n");
   while(ctx->bytes_in_buf > 0) {
      int bytes_written = send(ctx->fd, &ctx->buf[ctx->write_pos], ctx->bytes_in_buf, 0);
      if(bytes_written <= 0) break;
      ctx->write_pos += bytes_written;
      ctx->bytes_in_buf -= bytes_written;
   }
}

#define FCGI_PARAMS              4

void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata) {
   printf("[main] got fcgi message: type=%d\n", hdr->type);
   conn_ctx_t *ctx = userdata;
   if(hdr->type == FCGI_PARAMS) fcgi_params_parser_write(ctx->params_parser, data, hdr->contentLength);
}

void onfcgiparam(const char *key, const char *value, void *userdata) {
   if(!strcmp(key, "SCRIPT_FILENAME")) {
      printf("Got script filename: %s\n", value);
   }
}

void ondisconnect(struct epoll_event *evt) {
   printf("[main] connection closed, removing from interest: %d\n", evt->data.fd);
   conn_ctx_t *ctx = evt->data.ptr;
   assert(epoll_ctl(listen_ctx.efd, EPOLL_CTL_DEL, ctx->fd, NULL) == 0);
   conn_ctx_free(ctx);
}

int main() {
   listen_ctx.sock = create_listening_socket();
   listen_ctx.efd = epoll_create( 0xCAFE ); 
   assert(listen_ctx.efd != -1);
   memset(&listen_ctx.ev, 0, sizeof(struct epoll_event));
   listen_ctx.ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
   listen_ctx.ev.data.fd = listen_ctx.sock;
   assert(epoll_ctl(listen_ctx.efd, EPOLL_CTL_ADD, listen_ctx.sock, &listen_ctx.ev) == 0);

   const unsigned int EVENTS_COUNT = 20;
   struct epoll_event pevents[EVENTS_COUNT];

   while(1) {
      int event_count = epoll_wait(listen_ctx.efd, pevents, EVENTS_COUNT, 10000);
      printf("[main] Got %d events\n", event_count);
      if(event_count < 0) exit(-1);

      for(int i = 0; i < event_count; i++) {
         printf("[main] event %d: %08x sock = %d\n", i, pevents[i].events, pevents[i].data.fd);
         conn_ctx_t *ctx = pevents[i].data.ptr;
         if(pevents[i].data.fd == listen_ctx.sock) {
            onconnect(&pevents[i]);
            continue;
         }
         if(pevents[i].events & EPOLLIN) oninput(ctx);
         if(pevents[i].events & EPOLLOUT) onwriteok(ctx);
         if(pevents[i].events & EPOLLHUP) ondisconnect(&pevents[i]);
      }
   }
}
