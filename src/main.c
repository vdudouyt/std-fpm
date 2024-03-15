#define _GNU_SOURCE
#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <gmodule.h>
#include <errno.h>
#include <assert.h>

#include "fd_ctx.h"
#include "fdutils.h"
#include "fcgitypes.h"
#include "log.h"
#include "process_pool.h"
#include "debug_utils.h"
#include "fcgi_writer.h"
#include "debug.h"
#include "config.h"

stdfpm_config_t *cfg = NULL;
int epollfd;

static void onconnect(fd_ctx_t *lctx);
static void onsocketread(fd_ctx_t *ctx);
static void onsocketwriteok(fd_ctx_t *ctx);
static void ondisconnect(fd_ctx_t *ctx);
static void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata);
static void onfcgiparam(const char *key, const char *value, void *userdata);
static void fcgi_send_response(fd_ctx_t *ctx, const char *response, size_t size);

#define EXIT_WITH_ERROR(...) { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); exit(-1); }

#define SWAP(x,y) do {   \
  typeof(x) _x = x;      \
  typeof(y) _y = y;      \
  x = _y;                \
  y = _x;                \
} while(0)

static void add_to_wheel(fd_ctx_t *ctx) {
   struct epoll_event ev;
   memset(&ev, 0, sizeof(struct epoll_event));
   ev.events = EPOLLIN | EPOLLRDHUP;
   ev.data.ptr = ctx;

   assert(epoll_ctl(epollfd, EPOLL_CTL_ADD, ctx->fd, &ev) == 0);
}

static void set_writing(fd_ctx_t *ctx, bool value) {
   struct epoll_event ev;
   memset(&ev, 0, sizeof(struct epoll_event));
   ev.events = EPOLLIN | EPOLLRDHUP;
   if(value) ev.events |= EPOLLOUT;
   ev.data.ptr = ctx;

   assert(epoll_ctl(epollfd, EPOLL_CTL_MOD, ctx->fd, &ev) == 0);
}

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

   add_to_wheel(ctx);
}

void onsocketread(fd_ctx_t *ctx) {
   DEBUG("[%s] starting onsocketread()", ctx->name);
   buf_t *targetBuf = ctx->pipeTo ? ctx->pipeTo->memBuf : ctx->memBuf;
   if(ctx->type != STDFPM_FCGI_CLIENT && targetBuf == ctx->memBuf) return;

   if(!buf_ready_write(targetBuf, 16)) {
      DEBUG("[%s] buf is not ready for writing", ctx->name);
      return;
   }

   ssize_t bytes_read = buf_read_fd(targetBuf, ctx->fd);
   DEBUG("[%s] received %d bytes", ctx->name, bytes_read);

   if(bytes_read < 0) {
      DEBUG("[%s] buf_read_fd() failed", ctx->name);
      return;
   } else if(bytes_read == 0) {
      DEBUG("[%s] disconnected", ctx->name);
      return;
   }

   #ifdef DEBUG_LOG
   hexdump((const char *) &targetBuf->data, targetBuf->writePos);
   #endif

   if(ctx->type == STDFPM_FCGI_CLIENT && !ctx->pipeTo) {
      DEBUG("[%s] forwarded %d bytes to FastCGI parser", ctx->name, bytes_read);
      fcgi_parser_write(ctx->client->msg_parser, (unsigned char *) &targetBuf->data[targetBuf->writePos - bytes_read], bytes_read);
   }

   if(ctx->pipeTo) {
      set_writing(ctx->pipeTo, true);
   }
}

void onsocketwriteok(fd_ctx_t *ctx) {
   DEBUG("[%s] starting onsocketwriteok()", ctx->name);

   if(buf_bytes_remaining(ctx->memBuf)) {
      int bytes_written = buf_write_fd(ctx->memBuf, ctx->fd);
      DEBUG("[%s] bytes_written = %d", ctx->name, bytes_written);
   } else {
      set_writing(ctx, false);
      DEBUG("[%s] nothing to write", ctx->name);
      if(!ctx->pipeTo) ondisconnect(ctx);
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
         return;
      }

      fd_ctx_t *newctx = fd_new_process_ctx(proc);
      fd_ctx_bidirectional_pipe(ctx, newctx);
      SWAP(ctx->memBuf, newctx->memBuf); // write the bytes accumulated before startup
      DEBUG("Started child process: %s", newctx->name);
      add_to_wheel(newctx);
   }
}

static void ondisconnect(fd_ctx_t *ctx) {
   DEBUG("[%s] ondisconnect()", ctx->name);
   close(ctx->fd);

   if(ctx->process) {
      pool_release_process(ctx->process);
   }

   if(ctx->pipeTo) {
      ctx->pipeTo->pipeTo = NULL;
      if(!buf_bytes_remaining(ctx->pipeTo->memBuf)) {
         ondisconnect(ctx->pipeTo);
      }
   }

   fd_ctx_free(ctx);
}

static void stdfpm_process_events(struct epoll_event *pevents, int event_count) {
   for(int i = 0; i < event_count; i++) {
      fd_ctx_t *ctx = pevents[i].data.ptr;
      if(ctx->type == STDFPM_LISTEN_SOCK) {
         onconnect(ctx);
         continue;
      }
      if(pevents[i].events & EPOLLIN) onsocketread(ctx);
      if(pevents[i].events & EPOLLOUT) onsocketwriteok(ctx);
      if(pevents[i].events & EPOLLRDHUP) ondisconnect(ctx);
   }
}

static void fcgi_send_response(fd_ctx_t *ctx, const char *response, size_t size) {
   DEBUG("fcgi_send_response");
   buf_reset(ctx->memBuf);
   fcgi_write_buf(ctx->memBuf, 1, FCGI_STDOUT, response, size);
   fcgi_write_buf(ctx->memBuf, 1, FCGI_STDOUT, "", 0);
   fcgi_write_buf(ctx->memBuf, 1, FCGI_END_REQUEST, "\0\0\0\0\0\0\0\0", 8);
   set_writing(ctx, true);
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

   epollfd = epoll_create( 0xCAFE );
   struct epoll_event ev;
   memset(&ev, 0, sizeof(struct epoll_event));
   ev.events = EPOLLIN;
   ev.data.ptr = listen_ctx;

   assert(epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_ctx->fd, &ev) == 0);

   time_t last_clean = time(NULL);

   const unsigned int EVENTS_COUNT = 20;
   struct epoll_event pevents[EVENTS_COUNT];

   while(1) {
      if(time(NULL) - last_clean >= 60) {
         last_clean = time(NULL);
         pool_shutdown_inactive_processes(60);
      }

      int event_count = epoll_wait(epollfd, pevents, EVENTS_COUNT, 10000);
      if(event_count < 0) {
         perror("epoll");
         exit(-1);
      }

      if(event_count == 0) continue;
      stdfpm_process_events(pevents, event_count);
   }
}
