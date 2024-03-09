#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <gmodule.h>
#include <errno.h>

#include "fd_ctx.h"
#include "fdutils.h"
#include "fcgitypes.h"
#include "log.h"
#include "process_pool.h"
#include "debug_utils.h"
#include "fcgi_writer.h"
#include "debug.h"
#include "config.h"

GList *wheel = NULL;
stdfpm_config_t *cfg = NULL;

static void onconnect(fd_ctx_t *lctx);
static void onsocketread(fd_ctx_t *ctx);
static void onsocketwriteok(fd_ctx_t *ctx);
static void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata);
static void onfcgiparam(const char *key, const char *value, void *userdata);
static void fcgi_send_response(fd_ctx_t *ctx, const char *response, size_t size);

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

void onconnect(fd_ctx_t *listen_ctx) {
   fd_ctx_t *ctx = fd_ctx_client_accept(listen_ctx);
   if(!ctx) return;
   ctx->client->msg_parser->callback = onfcgimessage;
   ctx->client->params_parser->callback = onfcgiparam;
   DEBUG("[%s] new connection accepted: %s", listen_ctx->name, ctx->name);
   wheel = g_list_prepend(wheel, ctx);
}

void onsocketread(fd_ctx_t *ctx) {
   DEBUG("[%s] starting onsocketread()", ctx->name);

   buf_t *buf_to_write = NULL;
   if(ctx->pipeTo) buf_to_write = &ctx->pipeTo->outBuf;
   else if(ctx->client) buf_to_write = &ctx->client->inMemoryBuf;

   static char buf[4096];
   int bytes_read;
   while(buf_ready_write(buf_to_write, 4096) && (bytes_read = recv(ctx->fd, buf, sizeof(buf), 0)) >= 0) {
      DEBUG("[%s] received %d bytes", ctx->name, bytes_read);

      if(bytes_read == 0) {
         if(ctx->pipeTo) ctx->pipeTo->eof = true;
         ctx->eof = true;
         break;
      }

      #ifdef DEBUG_LOG
      hexdump(buf, bytes_read);
      #endif
      buf_write(buf_to_write, buf, bytes_read);

      if(ctx->type == STDFPM_FCGI_CLIENT && !ctx->pipeTo) {
         DEBUG("[%s] forwarded %d bytes to FastCGI parser", ctx->name, bytes_read);
         fcgi_parser_write(ctx->client->msg_parser, (unsigned char *) buf, bytes_read);
      }
   }
}

void onsocketwriteok(fd_ctx_t *ctx) {
   DEBUG("[%s] socket is ready for writing", ctx->name);
   size_t bytes_to_write = ctx->outBuf.writePos - ctx->outBuf.readPos;
   DEBUG("[%s] %d bytes to write", ctx->name, bytes_to_write);
   if(bytes_to_write > 0) {
      #ifdef DEBUG_LOG
      hexdump(&ctx->outBuf.data[ctx->outBuf.readPos], bytes_to_write);
      #endif

      int bytes_written = write(ctx->fd, &ctx->outBuf.data[ctx->outBuf.readPos], bytes_to_write);
      if(bytes_written == -1 && errno == EAGAIN) {
         return;
      }

      if(bytes_written <= 0) {
         DEBUG("[%s] write failed, discarding buffer", ctx->name);
         buf_reset(&ctx->outBuf);
         return;
      }

      ctx->outBuf.readPos += bytes_written;

      if(ctx->outBuf.readPos == ctx->outBuf.writePos) {
         DEBUG("[%s] %d bytes written", ctx->name, bytes_written);
         buf_reset(&ctx->outBuf);
      }
   }
}

void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata) {
   fd_ctx_t *ctx = userdata;

   DEBUG("[%s] got fcgi message: { type = %s, requestId = 0x%02x, contentLength = %d }",
      ctx->name, fcgitype_to_string(hdr->type), hdr->requestId, hdr->contentLength);

   if(hdr->contentLength) {
      char escaped_data[4*65536+1];
      escape(escaped_data, data, hdr->contentLength);
      DEBUG("[%s] message content: \"%s\"", ctx->name, escaped_data);
   }

   if(hdr->type == FCGI_PARAMS) {
      fcgi_params_parser_write(ctx->client->params_parser, data, hdr->contentLength);
   }
}

bool stdfpm_allowed_extension(const char *filename, char **extensions) {
   if(!extensions) {
      return false;
   }

   char *ext = strrchr(filename, '.');
   if(!ext) return false;

   unsigned int i = 0;
   for(char **s = extensions; s[i]; i++) {
      if(!strcmp(ext, s[i])) return true;
   }

   return false;
}

void onfcgiparam(const char *key, const char *value, void *userdata) {
   fd_ctx_t *ctx = userdata;
   if(!strcmp(key, "SCRIPT_FILENAME")) {
      DEBUG("[%s] got script filename: %s", ctx->name, value);

      // Apache
      const char pre[] = "proxy:fcgi://localhost/";
      if(!strncmp(pre, value, strlen(pre))) value = &value[strlen(pre)];

      if(!stdfpm_allowed_extension(value, cfg->extensions)) {
         char response[] = "Status: 403\nContent-type: text/html\n\nExtension is not allowed.";
         fcgi_send_response(ctx, response, strlen(response));
         return;
      }

      fcgi_process_t *proc = pool_borrow_process(value);

      if(!proc) {
         DEBUG("[%s] couldn't acquire FastCGI process", ctx->name);
         buf_reset(&ctx->outBuf);
         ctx->eof = true;
         return;
      }

      fd_ctx_t *newctx = fd_new_process_ctx(proc);
      fd_ctx_bidirectional_pipe(ctx, newctx);
      DEBUG("Started child process: %s", newctx->name);
      wheel = g_list_prepend(wheel, newctx);

      #ifdef DEBUG_LOG
      size_t bytes_written = buf_move(&newctx->outBuf, &ctx->client->inMemoryBuf);
      DEBUG("[%s] copied %d of buffered bytes to %s", ctx->name, bytes_written, newctx->name);
      #else
      buf_move(&newctx->outBuf, &ctx->client->inMemoryBuf);
      #endif
   }
}

static int stdfpm_prepare_fds(fd_set *read_fds, fd_set *write_fds) {
   int maxfd = 0;
   FD_ZERO(read_fds);
   FD_ZERO(write_fds);

   for(GList *it = wheel; it != NULL; it = it->next) {
      fd_ctx_t *ctx = it->data;
      DEBUG("Adding %s write=%d", ctx->name, buf_bytes_remaining(&ctx->outBuf));
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
   GList *it = wheel;
   while(it != NULL) {
      GList *next = it->next;
      fd_ctx_t *ctx = it->data;

      if(ctx->eof && buf_bytes_remaining(&ctx->outBuf) == 0) {
         if(ctx->process) {
            pool_release_process(ctx->process);
         }

         if(ctx->pipeTo) {
            ctx->pipeTo->pipeTo = NULL;
            ctx->pipeTo = NULL;
         }

         wheel = g_list_delete_link(wheel, it);
         DEBUG("[%s] connection closed, removing from interest", ctx->name);
         close(ctx->fd);
         fd_ctx_free(ctx);
      }

      it = next;
   }
}

static void fcgi_send_response(fd_ctx_t *ctx, const char *response, size_t size) {
   DEBUG("fcgi_send_response");
   buf_reset(&ctx->outBuf);
   fcgi_write_buf(&ctx->outBuf, 1, FCGI_STDOUT, response, size);
   fcgi_write_buf(&ctx->outBuf, 1, FCGI_STDOUT, "", 0);
   fcgi_write_buf(&ctx->outBuf, 1, FCGI_END_REQUEST, "\0\0\0\0\0\0\0\0", 8);
   if(ctx->pipeTo) ctx->pipeTo->eof = true;
   ctx->eof = true;
}

int main(int argc, char **argv) {
   log_set_echo(true);
   cfg = stdfpm_read_config(argc, argv);
   if(!cfg) EXIT_WITH_ERROR("Read config failed: %s", strerror(errno));
   if(!log_open(cfg->error_log)) EXIT_WITH_ERROR("Couldn't open %s: %s", cfg->error_log, strerror(errno));
   if(cfg->gid > 0 && setgid(cfg->gid) == -1) EXIT_WITH_ERROR("Couldn't set process gid: %s", strerror(errno));
   if(cfg->uid > 0 && setuid(cfg->uid) == -1) EXIT_WITH_ERROR("Couldn't set process uid: %s", strerror(errno));
   if(!pool_init(cfg->pool)) EXIT_WITH_ERROR("Pool initialization failed (failed malloc?)");

   int listen_sock = stdfpm_create_listening_socket(cfg->listen);

   if(!cfg->foreground) {
      log_set_echo(false);
      if(daemon(0, 0) != 0) EXIT_WITH_ERROR("Couldnt' daemonize");
   }

   signal(SIGPIPE, SIG_IGN);
   signal(SIGCHLD, SIG_IGN);

   fd_ctx_t *listen_ctx = fd_ctx_new(listen_sock, STDFPM_LISTEN_SOCK);
   if(!listen_ctx) exit(-1);

   fd_ctx_set_name(listen_ctx, "listen_sock");
   DEBUG("[%s] server created", listen_ctx->name);
   wheel = g_list_prepend(wheel, listen_ctx);

   struct timeval timeout;
   timeout.tv_sec  = 60;
   timeout.tv_usec = 0;

   time_t last_clean = time(NULL);

   while(1) {
      if(time(NULL) - last_clean >= 60) {
         last_clean = time(NULL);
         pool_shutdown_inactive_processes(60);
      }

      fd_set read_fds, write_fds;
      int maxfd = stdfpm_prepare_fds(&read_fds, &write_fds);

      // we don't have that much of idle file descriptors due to the way how FastCGI is working,
      // so no need of O(1) descriptor polling here
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
