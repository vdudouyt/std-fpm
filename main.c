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
#include "main.h"

#define BUF_SIZE 65536

fcgi_process_t* procs[256];
unsigned int process_count = 0;

fcgi_process_t *fcgi_spawn(const char *path) {
   int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
   assert(listen_sock != -1);
   fcgi_process_t *ret = malloc(sizeof(fcgi_process_t));
   ret->s_un.sun_family = AF_UNIX;
   sprintf(ret->s_un.sun_path, "/tmp/stdfpm-%d.sock", process_count);
   unlink(ret->s_un.sun_path);

   process_count++;
   assert(bind(listen_sock, (struct sockaddr *) &ret->s_un, sizeof(ret->s_un)) != -1);
   chmod(ret->s_un.sun_path, 0777);
   printf("Listening...\n");
   assert(listen(listen_sock, 1024) != -1);

   pid_t pid = fork();
   if(pid > 0) {
      close(listen_sock);
      printf("FastCGI process %s is successfully started with PID=%d\n", path, pid);
      strcpy(ret->process_path, path); // TODO: fix buffer overflow
      ret->pid = pid;
      return ret;
   } else {
      free(ret);
      dup2(listen_sock, STDIN_FILENO);
      char *argv[] = { (char*) path, NULL };
      execv(path, argv);
      printf("execv failed\n");
   }
}

conn_t *conn_new() {
   conn_t *ret = malloc(sizeof(conn_t));
   assert(ret);
   ret->bytes_in_buf = ret->write_pos = 0;
   ret->client = NULL;
   ret->process = NULL;
   return ret;
}

void conn_free(conn_t *this) {
   if(this->client) {
      fcgi_parser_free(this->client->msg_parser);
      fcgi_params_parser_free(this->client->params_parser);
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
void onsocketread(conn_t *ctx);
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

   conn_t *ctx = conn_new();
   ctx->fd = client_sock;
   ctx->type = STDFPM_FCGI_CLIENT;
   ctx->client = malloc(sizeof(fcgi_client_t));
   memset(ctx->client, 0, sizeof(fcgi_client_t));

   ctx->client->msg_parser = fcgi_parser_new();
   ctx->client->msg_parser->callback = onfcgimessage;
   ctx->client->msg_parser->userdata = ctx;

   ctx->client->params_parser = fcgi_params_parser_new(4096);
   ctx->client->params_parser->callback = onfcgiparam;
   ctx->client->params_parser->userdata = ctx;

   listen_ctx.ev.data.ptr = ctx;
   assert(epoll_ctl(listen_ctx.efd, EPOLL_CTL_ADD, ctx->fd, &listen_ctx.ev) == 0);

   printf("Done\n");
}

void onsocketread(conn_t *conn) {
   printf("onsocketread(%lx)\n", (long unsigned int) conn);
   static char buf[4096];
   int bytes_read;
   while((bytes_read = recv(conn->fd, buf, sizeof(buf), 0)) > 0) {
      printf("[main] got %d bytes: %s\n", bytes_read, buf);

      if(conn->type == STDFPM_FCGI_CLIENT && !conn->pipe_to) {
         printf("[main] wrote %d bytes to FastCGI parser\n", bytes_read);
         fcgi_parser_write(conn->client->msg_parser, buf, bytes_read);
      }

      if(conn->type != STDFPM_FCGI_CLIENT && !conn->pipe_to) {
         continue; // discard
      }

//      if(conn->bytes_in_buf + bytes_read < sizeof(conn->outBuf)) {
//         printf("[main] wrote %d bytes to ctx->outBuf\n", bytes_read);
//         memcpy(&conn->outBuf[conn->bytes_in_buf], buf, bytes_read);
//         conn->bytes_in_buf += bytes_read;
//      }
   }
}

void onsocketwriteok(conn_t *conn) {
   printf("[%d] onsocketwriteok(%lx)\n", conn->type, (long unsigned int) conn);
   if(!conn->pipe_to) {
      printf("no process to write\n");
      return;
   }
   if(conn->bytes_in_buf == 0) {
      printf("no bytes to write\n");
      return;
   }
   while(conn->bytes_in_buf > 0) {
      printf("writing\n");
      int bytes_written = send(conn->pipe_to->fd, &conn->outBuf[conn->write_pos], conn->bytes_in_buf, 0);
      printf("wrote %d bytes to fcgi process\n", bytes_written);
      if(bytes_written <= 0) break;
      conn->write_pos += bytes_written;
      conn->bytes_in_buf -= bytes_written;
      if(conn->bytes_in_buf <= 0) conn->bytes_in_buf = 0;
   }
}

#define FCGI_PARAMS              4

void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata) {
   printf("[main] got fcgi message: type=%d request_id=%d\n", hdr->type, hdr->requestId);
   conn_t *conn = userdata;
   if(hdr->type == FCGI_PARAMS) fcgi_params_parser_write(conn->client->params_parser, data, hdr->contentLength);
}

void onfcgiparam(const char *key, const char *value, void *userdata) {
   conn_t *conn = userdata;
   if(!strcmp(key, "SCRIPT_FILENAME")) {
      printf("Got script filename: %s\n", value);
      if(!strcmp(value, "/var/www/html/test.fcgi")) {
         printf("Starting %s...\n", value);
         conn_t *new_conn = conn_new();
         new_conn->type = STDFPM_FCGI_PROCESS;
         new_conn->process = fcgi_spawn(value);
         new_conn->pipe_to = conn;
         printf("Connecting UNIX socket: %s\n", new_conn->process->s_un.sun_path);

         new_conn->fd = socket(AF_UNIX, SOCK_STREAM, 0);
         assert(new_conn->fd != -1);
         assert(connect(new_conn->fd, (struct sockaddr *) &new_conn->process->s_un, sizeof(new_conn->process->s_un)) != -1);
         printf("Connection ok\n");

         listen_ctx.ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
         listen_ctx.ev.data.ptr = new_conn;
         assert(epoll_ctl(listen_ctx.efd, EPOLL_CTL_ADD, new_conn->fd, &listen_ctx.ev) == 0);
      } else {
         printf("Unknown fastcgi process: %s\n", value);
      }
   }
}

void ondisconnect(struct epoll_event *evt) {
   printf("[main] connection closed, removing from interest: %d\n", evt->data.fd);
   conn_t *ctx = evt->data.ptr;
   assert(epoll_ctl(listen_ctx.efd, EPOLL_CTL_DEL, ctx->fd, NULL) == 0);
   conn_free(ctx);
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
         conn_t *ctx = pevents[i].data.ptr;
         if(pevents[i].data.fd == listen_ctx.sock) {
            onconnect(&pevents[i]);
            continue;
         }
         if(pevents[i].events & EPOLLIN) onsocketread(ctx);
         if(pevents[i].events & EPOLLOUT) onsocketwriteok(ctx);
         if(pevents[i].events & EPOLLHUP) ondisconnect(&pevents[i]);
      }
   }
}
