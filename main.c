#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <gmodule.h>

#include "fd_ctx.h"
#include "fdutils.h"
#include "fcgitypes.h"
#include "log.h"
#include "process_pool.h"
#include "debug_utils.h"

GList *wheel = NULL;

static void onconnect(fd_ctx_t *lctx);
static void onsocketread(fd_ctx_t *ctx);
static void onsocketwriteok(fd_ctx_t *ctx);
static void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata);
static void onfcgiparam(const char *key, const char *value, void *userdata);

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
   fd_setnonblocking(listen_sock);
   fd_setcloseonexec(listen_sock);

   return listen_sock;
}

void onconnect(fd_ctx_t *lctx) {
   fd_ctx_t *ctx = fd_ctx_client_accept(lctx);
   ctx->client->msg_parser->callback = onfcgimessage;
   ctx->client->params_parser->callback = onfcgiparam;
   log_write("[%s] new connection accepted: %s", lctx->name, ctx->name);
   wheel = g_list_prepend(wheel, ctx);
}

void onsocketread(fd_ctx_t *ctx) {
   log_write("[%s] starting onsocketread()", ctx->name);

   buf_t *buf_to_write = NULL;
   if(ctx->pipeTo) buf_to_write = &ctx->pipeTo->outBuf;
   else if(ctx->client) buf_to_write = &ctx->client->inMemoryBuf;

   fd_ctx_t *pipeTo = ctx->pipeTo;

   static char buf[4096];
   int bytes_read;
   if(!buf_ready_write(buf_to_write, 4096)) {
      log_write("Buf is not ready: %08x", buf_to_write);
   }
   while(buf_ready_write(buf_to_write, 4096) && (bytes_read = recv(ctx->fd, buf, sizeof(buf), 0)) >= 0) {
      log_write("[%s] received %d bytes", ctx->name, bytes_read);

      if(bytes_read == 0) {
         if(ctx->pipeTo) ctx->pipeTo->eof = true;
         ctx->eof = true;
         break;
      }

      hexdump(buf, bytes_read);
      buf_write(buf_to_write, buf, bytes_read);

      if(ctx->type == STDFPM_FCGI_CLIENT && !ctx->pipeTo) {
         log_write("[%s] forwarded %d bytes to FastCGI parser", ctx->name, bytes_read);
         fcgi_parser_write(ctx->client->msg_parser, buf, bytes_read);
      }
   }
}

void onsocketwriteok(fd_ctx_t *ctx) {
   log_write("[%s] socket is ready for writing", ctx->name);
   size_t bytes_to_write = ctx->outBuf.writePos - ctx->outBuf.readPos;
   log_write("[%s] %d bytes to write", ctx->name, bytes_to_write);
   if(bytes_to_write > 0) {
      hexdump(&ctx->outBuf.data[ctx->outBuf.readPos], bytes_to_write);
      int bytes_written = write(ctx->fd, &ctx->outBuf.data[ctx->outBuf.readPos], bytes_to_write);
      if(bytes_written == -1 && errno == EAGAIN) {
         return;
      }

      if(bytes_written <= 0) {
         log_write("[%s] write failed, discarding buffer", ctx->name);
         buf_reset(&ctx->outBuf);
         return;
      }

      ctx->outBuf.readPos += bytes_written;

      if(ctx->outBuf.readPos == ctx->outBuf.writePos) {
         log_write("[%s] %d bytes written", ctx->name, bytes_written);
         buf_reset(&ctx->outBuf);
      }
   }
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
   }
}

void onfcgiparam(const char *key, const char *value, void *userdata) {
   fd_ctx_t *ctx = userdata;
   if(!strcmp(key, "SCRIPT_FILENAME")) {
      log_write("[%s] got script filename: %s", ctx->name, value);
      fcgi_process_t *proc = pool_borrow_process(value);
      fd_ctx_t *newctx = fd_new_process_ctx(proc);
      fd_ctx_bidirectional_pipe(ctx, newctx);
      log_write("Started child process: %s", newctx->name);
      wheel = g_list_prepend(wheel, newctx);

      size_t bytes_written = buf_move(&newctx->outBuf, &ctx->client->inMemoryBuf);
      log_write("[%s] copied %d of buffered bytes to %s", ctx->name, bytes_written, newctx->name);
   }
}

static int stdfpm_prepare_fds(fd_set *read_fds, fd_set *write_fds) {
   int maxfd = 0;
   FD_ZERO(read_fds);
   FD_ZERO(write_fds);

   for(GList *it = wheel; it != NULL; it = it->next) {
      fd_ctx_t *ctx = it->data;
      log_write("Adding %s write=%d", ctx->name, buf_bytes_remaining(&ctx->outBuf));
      FD_SET(ctx->fd, read_fds);
      if(buf_bytes_remaining(&ctx->outBuf)) FD_SET(ctx->fd, write_fds);
      if(ctx->fd > maxfd) maxfd = ctx->fd;
   }
   return maxfd;
}

static void stdfpm_process_events(fd_set *read_fds, fd_set *write_fds) {
   for(GList *it = wheel; it != NULL; it = it->next) {
      fd_ctx_t *ctx = it->data;
      if(FD_ISSET(ctx->fd, read_fds)) {
         ctx->type == STDFPM_LISTEN_SOCK ? onconnect(ctx) : onsocketread(ctx);
      }
      if(FD_ISSET(ctx->fd, write_fds)) {
         onsocketwriteok(ctx);
      }
   }
}

static void stdfpm_cleanup() {
   for(GList *it = wheel; it != NULL; it = it->next) {
      fd_ctx_t *ctx = it->data;
      if(!ctx->eof) continue;
      if(buf_bytes_remaining(&ctx->outBuf) > 0) continue;
   
      if(ctx->process) {
         pool_release_process(ctx->process);
      }
   
      if(ctx->pipeTo) {
         ctx->pipeTo->pipeTo = NULL;
         ctx->pipeTo = NULL;
      }
   
      wheel = g_list_delete_link(wheel, it);
      log_write("[%s] connection closed, removing from interest", ctx->name);
      log_write("[%s] connection time elapsed: %d", ctx->name, time(NULL) - ctx->started_at);
      close(ctx->fd);
      fd_ctx_free(ctx);
   }
}

int main() {
   int listen_sock = create_listening_socket();

   fd_ctx_t *ctx = fd_ctx_new(listen_sock, STDFPM_LISTEN_SOCK);
   fd_ctx_set_name(ctx, "listen_sock");
   log_open("/tmp/std-fpm.log");
   log_set_echo(true);
   log_write("[%s] server created", ctx->name);
   wheel = g_list_prepend(wheel, ctx);

   struct timeval timeout;
   timeout.tv_sec  = 3 * 60;
   timeout.tv_usec = 0;

   while(1) {
      fd_set read_fds, write_fds;
      int maxfd = stdfpm_prepare_fds(&read_fds, &write_fds);

      int ret = select(maxfd + 1, &read_fds, &write_fds, NULL, &timeout);
      if(ret < 0) {
         perror("select");
         exit(-1);
      }
      if(ret == 0) continue;
      stdfpm_process_events(&read_fds, &write_fds);
      stdfpm_cleanup();
   }
}
