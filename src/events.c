#include "events.h"
#include "log.h"
#include "debug.h"
#include "fcgitypes.h"
#include "debug_utils.h"
#include "process_pool.h"
#include "fcgi_writer.h"

#include <sys/stat.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>

static void stdfpm_read_completed_cb(struct bufferevent *bev, void *ptr);
static void stdfpm_write_completed_cb(struct bufferevent *bev, void *ptr);
static void stdfpm_event_cb(struct bufferevent *bev, short what, void *ptr);
static void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata);
static void onfcgiparam(const char *key, const char *value, void *userdata);
static bool stdfpm_allowed_extension(const char *filename, char **extensions);
static void stdfpm_disconnect(conn_t *conn);
static void stdfpm_socket_accepted_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *a, int slen, void *p);

struct evconnlistener *stdfpm_create_listener(struct event_base *base, const char *sock_path, stdfpm_config_t *config) {
   struct sockaddr_un s_un;
   s_un.sun_family = AF_UNIX;
   strcpy(s_un.sun_path, sock_path);
   unlink(s_un.sun_path); // Socket is in use, prepare for binding

   struct evconnlistener *ret = evconnlistener_new_bind(base, stdfpm_socket_accepted_cb, config,
       LEV_OPT_CLOSE_ON_FREE|LEV_OPT_CLOSE_ON_EXEC,
       -1, (struct sockaddr*) &s_un, sizeof(s_un));
   chmod(s_un.sun_path, 0777);
   return ret;
}

static void stdfpm_socket_accepted_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *a, int slen, void *p) {
   DEBUG("stdfpm_accept_conn()");
   struct event_base *base = evconnlistener_get_base(listener);
   stdfpm_config_t *config = p;

   struct bufferevent *bev = bufferevent_socket_new(base, fd,
      BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);

   conn_t *conn = fd_new_client_conn(bev);
   conn->config = config;
   conn->client->msg_parser->callback = onfcgimessage;
   conn->client->params_parser->callback = onfcgiparam;

   bufferevent_setcb(bev, stdfpm_read_completed_cb, stdfpm_write_completed_cb, stdfpm_event_cb, conn);
   bufferevent_enable(bev, EV_READ|EV_WRITE);
}

static void stdfpm_read_completed_cb(struct bufferevent *bev, void *ptr) {
   conn_t *conn = ptr;
   struct evbuffer *src = bufferevent_get_input(bev);
   size_t len = evbuffer_get_length(src);
   DEBUG("[%s] %ld bytes received", conn->name, len);

   if(conn->type == STDFPM_FCGI_CLIENT && !conn->pairedWith) {
      DEBUG("[%s] exposed %d bytes to FastCGI parser", conn->name, len);
      unsigned char *bytes = evbuffer_pullup(src, -1);
      fcgi_parser_write(conn->client->msg_parser, bytes, len);
   }

   if(conn->pairedWith) {
      DEBUG("[%s] enqueued %d bytes into %s", conn->name, len, conn->pairedWith->name);
      struct evbuffer *dst = bufferevent_get_output(conn->pairedWith->bev);
	   evbuffer_add_buffer(dst, src);
   } else if(conn->type == STDFPM_FCGI_CLIENT) {
      DEBUG("[%s] enqueued %d bytes into inMemoryBuf", conn->name, len);
	   evbuffer_add_buffer(conn->client->inMemoryBuf, src);
   } else {
      DEBUG("[%s] discarded %d bytes", conn->name, len);
      evbuffer_drain(src, len);
   }
}

static void stdfpm_write_completed_cb(struct bufferevent *bev, void *ptr) {
   conn_t *conn = ptr;
   struct evbuffer *dst = bufferevent_get_output(conn->bev);
   size_t remains = evbuffer_get_length(dst);
   DEBUG("[%s] write completed, %d bytes remains in buffer. pairedWith = %08x", conn->name, remains, conn->pairedWith);
   if(conn->closeAfterWrite && !remains) {
      DEBUG("[%s] disconnecting by closeAfterWrite", conn->name);
      stdfpm_disconnect(conn);
   }
}

static void stdfpm_event_cb(struct bufferevent *bev, short what, void *ptr) {
   conn_t *conn = ptr;
   if(what & (BEV_EVENT_EOF|BEV_EVENT_ERROR)) {
      if(what & BEV_EVENT_EOF) DEBUG("[%s] EOF received", conn->name);
      if(what & BEV_EVENT_ERROR) DEBUG("[%s] ERROR received", conn->name);
      stdfpm_disconnect(conn);
   }
}

static void onfcgimessage(const fcgi_header_t *hdr, const char *data, void *userdata) {
   conn_t *conn = userdata;

   DEBUG("[%s] got fcgi message: { type = %s, requestId = 0x%02x, contentLength = %d }",
      conn->name, fcgitype_to_string(hdr->type), hdr->requestId, hdr->contentLength);

   if(hdr->contentLength) {
      char escaped_data[4*65536+1];
      escape(escaped_data, data, hdr->contentLength);
      DEBUG("[%s] message content: \"%s\"", conn->name, escaped_data);
   }

   if(hdr->type == FCGI_PARAMS) {
      fcgi_params_parser_write(conn->client->params_parser, data, hdr->contentLength);
   }
}

static void onfcgiparam(const char *key, const char *value, void *userdata) {
   conn_t *conn = userdata;
   if(!strcmp(key, "SCRIPT_FILENAME")) {
      DEBUG("[%s] got script filename: %s", conn->name, value);

      // Apache
      const char pre[] = "proxy:fcgi://localhost/";
      if(!strncmp(pre, value, strlen(pre))) value = &value[strlen(pre)];

      if(!stdfpm_allowed_extension(value, conn->config->extensions)) {
         char response[] = "Status: 403\nContent-type: text/html\n\nExtension is not allowed.";
         fcgi_send_response(conn, response, strlen(response));
         return;
      }

      struct event_base *base = bufferevent_get_base(conn->bev);
      fcgi_process_t *proc = pool_borrow_process(base, value);

      if(!proc) {
         DEBUG("[%s] couldn't acquire FastCGI process", conn->name);
         return;
      }

      conn_t *newconn = fd_new_process_conn(proc);
      newconn->pairedWith = conn;
      conn->pairedWith = newconn;
      DEBUG("Started child process: %s", newconn->name);

      struct evbuffer *dst = bufferevent_get_output(newconn->bev);
      DEBUG("[%s] took %d bytes from %s inMemoryBuf", newconn->name, evbuffer_get_length(dst), conn->name);
	   evbuffer_add_buffer(dst, conn->client->inMemoryBuf);

      bufferevent_setcb(newconn->bev, stdfpm_read_completed_cb, stdfpm_write_completed_cb, stdfpm_event_cb, newconn);
      bufferevent_enable(newconn->bev, EV_READ|EV_WRITE);
   }
}

static void stdfpm_disconnect(conn_t *conn) {
   // break the pipe so that the next write_cb() could close the connection if nothing remains
   DEBUG("[%s] stdfpm_disconnect()", conn->name);
   if(conn->process) {
      pool_release_process(conn->process);
   }
   if(conn->pairedWith) {
      conn->pairedWith->pairedWith = NULL;
      struct evbuffer *dst = bufferevent_get_output(conn->pairedWith->bev);
      size_t remains = evbuffer_get_length(dst);
      if(remains > 0) {
         DEBUG("[%s] partner %s has %d bytes remaining to write", conn->name, conn->pairedWith->name, remains);
         conn->pairedWith->closeAfterWrite = true;
      } else {
         DEBUG("[%s] partner %s has %d bytes remaining to write, disconnecting it", conn->name, conn->pairedWith->name, remains);
         stdfpm_disconnect(conn->pairedWith);
      }
   }
   bufferevent_setcb(conn->bev, NULL, NULL, NULL, NULL);
   DEBUG("Disconnecting %s", conn->name);
   bufferevent_free(conn->bev);
   conn_free(conn);
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
