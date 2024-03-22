#include "events.h"
#include "log.h"
#include "debug.h"
#include "fcgitypes.h"
#include "debug_utils.h"
#include "process_pool.h"
#include "fcgi_writer.h"

static void stdfpm_read_completed_cb(struct bufferevent *bev, void *ptr);
static void stdfpm_write_completed_cb(struct bufferevent *bev, void *ptr);
static void stdfpm_event_cb(struct bufferevent *bev, short what, void *ptr);
static void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata);
static void onfcgiparam(const char *key, const char *value, void *userdata);
static bool stdfpm_allowed_extension(const char *filename, char **extensions);
static void stdfpm_disconnect(fd_ctx_t *ctx);

void stdfpm_socket_accepted_cb(worker_t *worker, int fd) {
   DEBUG("stdfpm_accept_conn()");

   struct bufferevent *bev = bufferevent_socket_new(worker->base, fd,
      BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);

   fd_ctx_t *ctx = fd_new_client_ctx(bev);
   ctx->worker = worker;
   ctx->client->msg_parser->callback = onfcgimessage;
   ctx->client->params_parser->callback = onfcgiparam;

   bufferevent_setcb(bev, stdfpm_read_completed_cb, stdfpm_write_completed_cb, stdfpm_event_cb, ctx);
   bufferevent_enable(bev, EV_READ|EV_WRITE);
}

static void stdfpm_read_completed_cb(struct bufferevent *bev, void *ptr) {
   fd_ctx_t *ctx = ptr;
   struct evbuffer *src = bufferevent_get_input(bev);
   size_t len = evbuffer_get_length(src);
   DEBUG("[%s] %ld bytes received", ctx->name, len);

   if(ctx->type == STDFPM_FCGI_CLIENT && !ctx->pipeTo) {
      DEBUG("[%s] exposed %d bytes to FastCGI parser", ctx->name, len);
      unsigned char *bytes = evbuffer_pullup(src, -1);
      fcgi_parser_write(ctx->client->msg_parser, bytes, len);
   }

   if(ctx->pipeTo) {
      DEBUG("[%s] enqueued %d bytes into %s", ctx->name, len, ctx->pipeTo->name);
      struct evbuffer *dst = bufferevent_get_output(ctx->pipeTo->bev);
	   evbuffer_add_buffer(dst, src);
   } else if(ctx->type == STDFPM_FCGI_CLIENT) {
      DEBUG("[%s] enqueued %d bytes into inMemoryBuf", ctx->name, len);
	   evbuffer_add_buffer(ctx->client->inMemoryBuf, src);
   } else {
      DEBUG("[%s] discarded %d bytes", ctx->name, len);
      evbuffer_drain(src, len);
   }
}

static void stdfpm_write_completed_cb(struct bufferevent *bev, void *ptr) {
   fd_ctx_t *ctx = ptr;
   struct evbuffer *dst = bufferevent_get_output(ctx->bev);
   size_t remains = evbuffer_get_length(dst);
   DEBUG("[%s] write completed, %d bytes remains in buffer. pipeTo = %08x", ctx->name, remains, ctx->pipeTo);
   if(ctx->closeAfterWrite && !remains) {
      DEBUG("[%s] disconnecting by closeAfterWrite", ctx->name);
      stdfpm_disconnect(ctx);
   }
}

static void stdfpm_event_cb(struct bufferevent *bev, short what, void *ptr) {
   fd_ctx_t *ctx = ptr;
   if(what & (BEV_EVENT_EOF|BEV_EVENT_ERROR)) {
      if(what & BEV_EVENT_EOF) DEBUG("[%s] EOF received", ctx->name);
      if(what & BEV_EVENT_ERROR) DEBUG("[%s] ERROR received", ctx->name);
      stdfpm_disconnect(ctx);
   }
}

static void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata) {
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

static void onfcgiparam(const char *key, const char *value, void *userdata) {
   fd_ctx_t *ctx = userdata;
   if(!strcmp(key, "SCRIPT_FILENAME")) {
      DEBUG("[%s] got script filename: %s", ctx->name, value);

      // Apache
      const char pre[] = "proxy:fcgi://localhost/";
      if(!strncmp(pre, value, strlen(pre))) value = &value[strlen(pre)];

      if(!stdfpm_allowed_extension(value, ctx->worker->config->extensions)) {
         char response[] = "Status: 403\nContent-type: text/html\n\nExtension is not allowed.";
         fcgi_send_response(ctx, response, strlen(response));
         return;
      }

      struct event_base *base = bufferevent_get_base(ctx->bev);
      fcgi_process_t *proc = pool_borrow_process(base, value);

      if(!proc) {
         DEBUG("[%s] couldn't acquire FastCGI process", ctx->name);
         return;
      }

      fd_ctx_t *newctx = fd_new_process_ctx(proc);
      fd_ctx_bidirectional_pipe(ctx, newctx);
      DEBUG("Started child process: %s", newctx->name);

      struct evbuffer *dst = bufferevent_get_output(newctx->bev);
      DEBUG("[%s] took %d bytes from %s inMemoryBuf", newctx->name, evbuffer_get_length(dst), ctx->name);
	   evbuffer_add_buffer(dst, ctx->client->inMemoryBuf);

      bufferevent_setcb(newctx->bev, stdfpm_read_completed_cb, stdfpm_write_completed_cb, stdfpm_event_cb, newctx);
      bufferevent_enable(newctx->bev, EV_READ|EV_WRITE);
   }
}

static void stdfpm_disconnect(fd_ctx_t *ctx) {
   // break the pipe so that the next write_cb() could close the connection if nothing remains
   DEBUG("[%s] stdfpm_disconnect()", ctx->name);
   if(ctx->process) {
      pool_release_process(ctx->process);
   }
   if(ctx->pipeTo) {
      ctx->pipeTo->pipeTo = NULL;
      struct evbuffer *dst = bufferevent_get_output(ctx->pipeTo->bev);
      size_t remains = evbuffer_get_length(dst);
      if(remains > 0) {
         DEBUG("[%s] partner %s has %d bytes remaining to write", ctx->name, ctx->pipeTo->name, remains);
         ctx->pipeTo->closeAfterWrite = true;
      } else {
         DEBUG("[%s] partner %s has %d bytes remaining to write, disconnecting it", ctx->name, ctx->pipeTo->name, remains);
         stdfpm_disconnect(ctx->pipeTo);
      }
   }
   bufferevent_setcb(ctx->bev, NULL, NULL, NULL, NULL);
   DEBUG("Disconnecting %s", ctx->name);
   bufferevent_free(ctx->bev);
   fd_ctx_free(ctx);
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
