#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <gmodule.h>
#include <unistd.h>
#include <event2/bufferevent.h>
#include "fdutils.h"
#include "log.h"
#include "debug.h"
#include "process_pool.h"
#include "fcgi_process.h"

#define RETURN_ERROR(msg) { log_write(msg); return NULL; }

static char *pool_path = NULL;
static GHashTable *process_pool = NULL;
static unsigned int startup_counter = 0;
pthread_mutex_t pool_mutex;
pthread_t inactivity_detector;

static bool pool_connect_process(struct event_base *base, fcgi_process_t *proc);
static fcgi_process_t *pool_borrow_existing_process(struct event_base *base, const char *path);
static fcgi_process_t *pool_create_process(struct event_base *base, const char *path);
static void pool_shutdown_inactive_processes(evutil_socket_t fd, short what, void *arg);
static void pool_shutdown_bucket_inactive_processes(gpointer key, gpointer value, gpointer user_data);
static void *pool_rip_idling_processes(void *ptr);
static void rmsocket(unsigned int socket_id);

bool pool_init(const char *path) {
   process_pool = g_hash_table_new(g_str_hash, g_str_equal);
   if(!process_pool) return false;

   pool_path = strdup(path);
   if(!pool_path) return false;

   return true;
}

fcgi_process_t *pool_borrow_process(struct event_base *base, const char *path) {
   if(g_hash_table_contains(process_pool, path)) {
      pthread_mutex_lock(&pool_mutex);
   } else {
      char *newkey = strdup(path);
      if(!newkey) RETURN_ERROR("[process pool] strdup failed");
      GQueue *q = g_queue_new();
      if(!q) RETURN_ERROR("[process_pool] queue allocation failed");
      pthread_mutex_lock(&pool_mutex);
      g_hash_table_insert(process_pool, newkey, q);
   }

   fcgi_process_t *proc = pool_borrow_existing_process(base, path);
   pthread_mutex_unlock(&pool_mutex);

   if(!proc) proc = pool_create_process(base, path);
   return proc;
}

void pool_release_process(fcgi_process_t *proc) {
   DEBUG("[process pool] Releasing process %d to bucket %s", proc->pid, proc->filepath);
   proc->last_used = time(NULL);
   pthread_mutex_lock(&pool_mutex);
   GQueue *q = g_hash_table_lookup(process_pool, proc->filepath);
   g_queue_push_head(q, proc);
   pthread_mutex_unlock(&pool_mutex);
}

static bool pool_connect_process(struct event_base *base, fcgi_process_t *proc) {
   int fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if(fd == -1) return false;
   if(connect(fd, (struct sockaddr *) &proc->s_un, sizeof(proc->s_un)) == -1) {
      close(fd);
      return false;
   }
   fd_setnonblocking(fd);
   fd_setcloseonexec(fd);

   struct bufferevent *bev = bufferevent_socket_new(base, fd,
      BEV_OPT_CLOSE_ON_FREE|BEV_OPT_DEFER_CALLBACKS);

   if(!bev) {
      log_write("bufferevent allocation failed");
      return false;
   }

   DEBUG("[process pool] opened connection to %s", proc->s_un.sun_path);
   proc->bev = bev;
   return true;
}

static fcgi_process_t *pool_borrow_existing_process(struct event_base *base, const char *path) {
   GQueue *q = g_hash_table_lookup(process_pool, path);
   fcgi_process_t *proc;

   while(proc = g_queue_pop_head(q)) {
      if(pool_connect_process(base, proc)) {
         return proc;
      }
      rmsocket(proc->socket_id);
      free(proc);
   }

   return NULL;
}

static fcgi_process_t *pool_create_process(struct event_base *base, const char *path) {
   char socket_path[4096];
   startup_counter++;
   snprintf(socket_path, sizeof(socket_path), "%s/stdfpm-%d.sock", pool_path, startup_counter);

   fcgi_process_t *proc = fcgi_spawn(socket_path, path);
   proc->socket_id = startup_counter;
   if(!proc) return NULL;
   if(pool_connect_process(base, proc)) {
      return proc;
   } else {
      free(proc);
      return NULL;
   }
}

void pool_start_inactivity_detector(unsigned int process_idle_timeout) {
   pthread_create(&inactivity_detector, NULL, pool_rip_idling_processes, GINT_TO_POINTER(process_idle_timeout));
}

static void *pool_rip_idling_processes(void *ptr) {
   unsigned int process_idle_timeout = GPOINTER_TO_INT(ptr);
   while(1) {
      DEBUG("pool_rip_idling_processes()");
      pthread_mutex_lock(&pool_mutex);
      g_hash_table_foreach(process_pool, pool_shutdown_bucket_inactive_processes, &process_idle_timeout);
      pthread_mutex_unlock(&pool_mutex);
      sleep(process_idle_timeout >= 60 ? 60 : 1);
   }
}

static void rmsocket(unsigned int socket_id) {
   char socket_path[4096];
   snprintf(socket_path, sizeof(socket_path), "%s/stdfpm-%d.sock", pool_path, socket_id);
   unlink(socket_path);
}

static void pool_shutdown_bucket_inactive_processes(gpointer key, gpointer value, gpointer user_data) {
   unsigned int max_idling_time = *(unsigned int*) user_data;
   GQueue *q = value;
   fcgi_process_t *proc;
   time_t curtime = time(NULL);

   while(proc = g_queue_peek_tail(q)) {
      if(curtime - proc->last_used < max_idling_time) {
         break;
      }
      DEBUG("[process pool] removing idle process: %s", key);
      kill(proc->pid, SIGTERM);
      rmsocket(proc->socket_id);
      free(proc);
      g_queue_pop_tail(q);
   }
}
