#include "events.h"
#include "log.h"
#include "debug.h"
#include "fcgitypes.h"
#include "debug_utils.h"
#include "process_pool.h"
#include "fcgi_writer.h"
#include "fdutils.h"

#include <sys/stat.h>
#include <stdlib.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>

static void stdfpm_read_completed_cb(struct bufferevent *bev, void *ptr);
static void stdfpm_write_completed_cb(struct bufferevent *bev, void *ptr);
static void stdfpm_event_cb(struct bufferevent *bev, short what, void *ptr);
static bool stdfpm_allowed_extension(const char *filename, char **extensions);
static void stdfpm_disconnect(conn_t *conn);
static void stdfpm_socket_accepted_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *a, int slen, void *p);
static void stdfpm_connect_process(conn_t *conn, const char *scriptFilename);

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

   bufferevent_setcb(bev, stdfpm_read_completed_cb, stdfpm_write_completed_cb, stdfpm_event_cb, conn);
   bufferevent_enable(bev, EV_READ|EV_WRITE);
}

static void stdfpm_read_completed_cb(struct bufferevent *bev, void *ptr) {
   conn_t *conn = ptr;
   struct evbuffer *src = bufferevent_get_input(bev);
   size_t len = evbuffer_get_length(src);
   DEBUG("[%s] %ld bytes received", conn->name, len);

   if(conn->type == STDFPM_FCGI_CLIENT && !conn->pairedWith && !conn->client->scriptFilename) {
      DEBUG("[%s] exposed %d bytes to FastCGI parser", conn->name, len);
      char *bytes = (char*) evbuffer_pullup(src, -1);
      fcgi_parse(&conn->client->fcgiParser, bytes, len);
      conn->client->scriptFilename = fcgi_get_script_filename(&conn->client->fcgiParser);

      if(conn->client->scriptFilename) {
         stdfpm_connect_process(conn, conn->client->scriptFilename);
      }
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

static fcgi_process_t *stdfpm_connect_existing_process(const char *scriptFilename, int fd) {
   fcgi_process_t *proc;
   while(proc = pool_borrow_process(scriptFilename)) {
      int status = connect(fd, (struct sockaddr *) &proc->s_un, sizeof(proc->s_un));
      if(status == 0 || errno == EINPROGRESS) {
         return proc;
      } else {
         free(proc);
      }
   }
   return NULL;
}

static fcgi_process_t *stdfpm_connect_new_process(const char *pool, const char *scriptFilename, int fd) {
   char socketPath[4096];
   static unsigned int startup_counter = 0;
   startup_counter++;
   snprintf(socketPath, sizeof(socketPath), "%s/stdfpm-%d.sock", pool, startup_counter);

   fcgi_process_t *proc = fcgi_spawn(socketPath, scriptFilename);
   if(!proc) return NULL;

   int status = connect(fd, (struct sockaddr *) &proc->s_un, sizeof(proc->s_un));
   if(status != 0 && errno != EINPROGRESS) {
      log_write("Couldn't connect to just started process: %s (sock=%s, script_filename = %s)",
         strerror(errno), socketPath, scriptFilename);
      free(proc);
      return NULL;
   }

   return proc;
}

static void stdfpm_connect_process(conn_t *conn, const char *scriptFilename) {
   DEBUG("[%s] got script filename: %s", conn->name, scriptFilename);

   // Apache
   const char pre[] = "proxy:fcgi://localhost/";
   if(!strncmp(pre, scriptFilename, strlen(pre))) scriptFilename = &scriptFilename[strlen(pre)];

   if(!stdfpm_allowed_extension(scriptFilename, conn->config->extensions)) {
      char response[] = "Status: 403\nContent-type: text/html\n\nExtension is not allowed.";
      fcgi_send_response(conn, response, strlen(response));
      return;
   }

   struct event_base *base = bufferevent_get_base(conn->bev);

   int newfd = socket(AF_UNIX, SOCK_STREAM, 0);
   if(newfd == -1) {
      log_write("Couldn't create socket: %s", strerror(errno));
      return;
   }

   fd_setnonblocking(newfd);
   fd_setcloseonexec(newfd);

   fcgi_process_t *proc = stdfpm_connect_existing_process(scriptFilename, newfd);
   if(!proc) proc = stdfpm_connect_new_process(conn->config->pool, scriptFilename, newfd);

   if(!proc) {
      DEBUG("[%s] couldn't acquire FastCGI process", conn->name);
      close(newfd);
      return;
   }

   struct bufferevent *bev = bufferevent_socket_new(base, newfd,
      BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);
   
   if(!bev) {
      log_write("bufferevent allocation failed");
      return;
   }

   conn_t *newconn = fd_new_process_conn(proc, bev);
   newconn->pairedWith = conn;
   conn->pairedWith = newconn;
   DEBUG("Started child process: %s", newconn->name);

   struct evbuffer *dst = bufferevent_get_output(newconn->bev);
   DEBUG("[%s] took %d bytes from %s inMemoryBuf", newconn->name, evbuffer_get_length(dst), conn->name);
   evbuffer_add_buffer(dst, conn->client->inMemoryBuf);

   bufferevent_setcb(newconn->bev, stdfpm_read_completed_cb, stdfpm_write_completed_cb, stdfpm_event_cb, newconn);
   bufferevent_enable(newconn->bev, EV_READ|EV_WRITE);
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
      conn->pairedWith = NULL;
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
