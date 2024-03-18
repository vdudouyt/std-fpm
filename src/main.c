#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/un.h>
#include <sys/socket.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>

#include "log.h"
#include "debug.h"

static struct evconnlistener *stdfpm_create_listener(struct event_base *base, const char *sock_path);
static void stdfpm_accept_conn(struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *a, int slen, void *p);
static void stdfpm_read(struct bufferevent *bev, void *ctx);
static void stdfpm_eventcb(struct bufferevent *bev, short what, void *ctx);

static struct evconnlistener *stdfpm_create_listener(struct event_base *base, const char *sock_path) {
   struct sockaddr_un s_un;
   s_un.sun_family = AF_UNIX;
   strcpy(s_un.sun_path, sock_path);
   unlink(s_un.sun_path); // Socket is in use, prepare for binding

   struct evconnlistener *ret = evconnlistener_new_bind(base, stdfpm_accept_conn, NULL,
       LEV_OPT_CLOSE_ON_FREE|LEV_OPT_CLOSE_ON_EXEC,
       -1, (struct sockaddr*) &s_un, sizeof(s_un));
   return ret;
}

static void stdfpm_accept_conn(struct evconnlistener *listener, evutil_socket_t fd,
   struct sockaddr *a, int slen, void *p) {
   DEBUG("stdfpm_accept_conn()");
   struct event_base *base = evconnlistener_get_base(listener);
   struct bufferevent *b_in = bufferevent_socket_new(base, fd,
      BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);
   bufferevent_setcb(b_in, stdfpm_read, NULL, stdfpm_eventcb, NULL);
   bufferevent_enable(b_in, EV_READ|EV_WRITE);
}

static void stdfpm_read(struct bufferevent *bev, void *ctx) {
   struct evbuffer *src = bufferevent_get_input(bev);
   size_t len = evbuffer_get_length(src);
   DEBUG("stdfpm_read(): %ld bytes received", len);
   evbuffer_drain(src, len);
}

static void stdfpm_eventcb(struct bufferevent *bev, short what, void *ctx) {
   if(what & BEV_EVENT_EOF) {
      DEBUG("stdfpm_eventcb(): EOF received");
   }
}

int main() {
   log_set_echo(true);
   struct event_base *base = event_base_new();
   struct evconnlistener *listener = stdfpm_create_listener(base, "/tmp/std-fpm.sock");
   event_base_dispatch(base);
   event_base_free(base);
}
