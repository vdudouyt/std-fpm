#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>

#include "fd_ctx.h"
#include "fcgitypes.h"
#include "log.h"
#include "process_pool.h"

static void onsocketread(fd_ctx_t *ctx);
static void onsocketwriteok(fd_ctx_t *ctx);

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
   switch(type) {
      case STDFPM_LISTEN_SOCK: return "LISTEN_SOCK";
      case STDFPM_FCGI_CLIENT: return "FCGI_CLIENT";
      case STDFPM_FCGI_PROCESS: return "FCGI_PROCESS";
      default: return "UNKNOWN";
   }
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

static void add_to_wheel(fd_ctx_t *ctx) {
   struct epoll_event ev;
   memset(&ev, 0, sizeof(struct epoll_event));
   ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
   ev.data.ptr = ctx;

   assert(epoll_ctl(epollfd, EPOLL_CTL_ADD, ctx->fd, &ev) == 0);
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
   log_write("[%s] new connection accepted: %s", lctx->name, ctx->name);

   ctx->client->msg_parser = fcgi_parser_new();
   ctx->client->msg_parser->callback = onfcgimessage;
   ctx->client->msg_parser->userdata = ctx;

   ctx->client->params_parser = fcgi_params_parser_new(4096);
   ctx->client->params_parser->callback = onfcgiparam;
   ctx->client->params_parser->userdata = ctx;

   add_to_wheel(ctx);
}

void hexdump(const unsigned char *buf, size_t size) {
   for(int i = 0; i < size; i++) {
      printf("%02x ", buf[i]);;
   }
   printf("\n");
}

void onsocketread(fd_ctx_t *ctx) {
   log_write("[%s] starting onsocketread()", ctx->name);

   buf_t *buf_to_write = NULL;
   if(ctx->type == STDFPM_FCGI_CLIENT) buf_to_write = ctx->pipeTo ? &ctx->pipeTo->outBuf : &ctx->client->inBuf;
   if(ctx->type == STDFPM_FCGI_PROCESS) buf_to_write = &ctx->pipeTo->outBuf;

   static char buf[4096];
   int bytes_read;
   while(buf_ready_write(buf_to_write, 4096) && (bytes_read = recv(ctx->fd, buf, sizeof(buf), 0)) >= 0) {
      log_write("[%s] received %d bytes", ctx->name, bytes_read);

      if(bytes_read == 0) {
         if(ctx->pipeTo) ctx->pipeTo->disconnectAfterWrite = true;
         close(ctx->fd);
         ondisconnect(ctx);
         break;
      }

      hexdump(buf, bytes_read);
      buf_write(buf_to_write, buf, bytes_read);

      if(ctx->type == STDFPM_FCGI_CLIENT && !ctx->pipeTo) {
         log_write("[%s] forwarded %d bytes to FastCGI parser", ctx->name, bytes_read);
         fcgi_parser_write(ctx->client->msg_parser, buf, bytes_read);
      }
   }

   if(ctx->pipeTo) onsocketwriteok(ctx->pipeTo);
}

void onsocketwriteok(fd_ctx_t *ctx) {
   log_write("[%s] socket is ready for writing", ctx->name);
   size_t bytes_to_write = ctx->outBuf.writePos - ctx->outBuf.readPos;
   log_write("[%s] %d bytes to write", ctx->name, bytes_to_write);
   if(bytes_to_write > 0) {
      hexdump(&ctx->outBuf.data[ctx->outBuf.readPos], bytes_to_write);
	   int bytes_written = write(ctx->fd, &ctx->outBuf.data[ctx->outBuf.readPos], bytes_to_write);

      if(bytes_written <= 0) {
         log_write("[%s] write failed, discarding buffer", ctx->name);
         buf_discard(&ctx->outBuf);
         return;
      }

      ctx->outBuf.readPos += bytes_written;

      if(ctx->outBuf.readPos == ctx->outBuf.writePos) {
         log_write("[%s] %d bytes written", ctx->name, bytes_written);
         buf_discard(&ctx->outBuf);

         if(ctx->disconnectAfterWrite) {
            close(ctx->fd);
            ondisconnect(ctx);
         }
      }
   }
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

void buf_fcgi_write(buf_t *outBuf, unsigned int requestId, unsigned int type, const char *content, size_t contentLength) {
   if(sizeof(outBuf->data) - outBuf->writePos < contentLength + 8 + 4) return;
   const unsigned char paddingLength = (4 - (contentLength % 4)) % 4;
   char *out = &outBuf->data[outBuf->writePos];
   out[0] = 0x01; // version
   out[1] = type;
   out[2] = (requestId & 0xff00) > 8;
   out[3] = requestId & 0xff;
   out[4] = (contentLength & 0xff00) > 8;
   out[5] = contentLength & 0xff;
   out[6] = paddingLength;
   out[7] = 0;    // reserved
   memcpy(&out[8], content, contentLength);
   memset(&out[8 + contentLength], 0, paddingLength);
   outBuf->writePos += 8 + contentLength + paddingLength;
}

void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata) {
   fd_ctx_t *ctx = userdata;

   log_write("[%s] got fcgi message: { type = %s, requestId = 0x%02x, contentLength = %d }",
      ctx->name, fcgitype_to_string(hdr->type), hdr->requestId, hdr->contentLength);

   if(hdr->contentLength) {
      char escaped_data[4*65536+1];
      escape(escaped_data, data, hdr->contentLength);
      log_write("[%s] message content: \"%s\"", ctx->name, escaped_data);
   }

   if(hdr->type == FCGI_PARAMS) {
      fcgi_params_parser_write(ctx->client->params_parser, data, hdr->contentLength);
   } else if(hdr->type == FCGI_STDIN) {
      /*
      static char not_found[] = "Status: 404\nContent-type: text/html\n\nFile not found.\n";
      buf_discard(&ctx->outBuf);
      buf_fcgi_write(&ctx->outBuf, hdr->requestId, FCGI_STDOUT, not_found, sizeof(not_found) - 1);
      buf_fcgi_write(&ctx->outBuf, hdr->requestId, FCGI_STDOUT, "", 0);
      buf_fcgi_write(&ctx->outBuf, hdr->requestId, FCGI_END_REQUEST, "\0\0\0\0\0\0\0\0", 8);
      log_write("wrote %d bytes", ctx->outBuf.writePos - ctx->outBuf.readPos);
      hexdump(&ctx->outBuf.data[ctx->outBuf.readPos], ctx->outBuf.writePos - ctx->outBuf.readPos);
      */
   }

/*
   if(ctx->client->process) {
      log_write("Writing %d bytes to client->process", 8 + hdr->contentLength);
   } else {
      log_write("FastCGI process is not started yet, writing %d bytes to client->inBuf", 8 + hdr->contentLength);
   }
   */
}

void onfcgiparam(const char *key, const char *value, void *userdata) {
   fd_ctx_t *ctx = userdata;
   if(!strcmp(key, "SCRIPT_FILENAME")) {
      log_write("[%s] got script filename: %s", ctx->name, value);
      static unsigned int ctr = 1;
      fcgi_process_t *proc = pool_borrow_process(value);
      fd_ctx_t *newctx = fd_ctx_new(proc->fd, STDFPM_FCGI_PROCESS);
      fd_ctx_set_name(newctx, "responder_%d", ctr++);

      newctx->process = proc;
      newctx->pipeTo = ctx;
      ctx->pipeTo = newctx;
      add_to_wheel(newctx);

      size_t bytes_written = buf_move(&newctx->outBuf, &ctx->client->inBuf);
      log_write("[%s] copied %d of buffered bytes to %s", ctx->name, bytes_written, newctx->name);
   }
}

void ondisconnect(fd_ctx_t *ctx) {
   log_write("[%s] connection closed, removing from interest", ctx->name);
   epoll_ctl(epollfd, EPOLL_CTL_DEL, ctx->fd, NULL);

   if(ctx->process) {
      pool_release_process(ctx->process);
   }

   if(ctx->pipeTo) {
      ctx->pipeTo->pipeTo = NULL;
   }

   fd_ctx_free(ctx);
}

int main() {
   int listen_sock = create_listening_socket();
   epollfd = epoll_create( 0xCAFE );

   assert(epollfd != -1);

   fd_ctx_t *ctx = fd_ctx_new(listen_sock, STDFPM_LISTEN_SOCK);
   fd_ctx_set_name(ctx, "listen_sock");
   log_set_echo(true);
   log_write("[%s] server created", ctx->name);

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
