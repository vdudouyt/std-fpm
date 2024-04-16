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

static GHashTable *process_pool = NULL;
static unsigned int startup_counter = 0;
pthread_mutex_t pool_mutex;

static bool pool_connect_process(struct event_base *base, fcgi_process_t *proc);
static fcgi_process_t *pool_borrow_existing_process(struct event_base *base, const char *path);
static fcgi_process_t *pool_create_process(struct event_base *base, const char *path);
static void pool_shutdown_inactive_processes(evutil_socket_t fd, short what, void *arg);
static void pool_shutdown_bucket_inactive_processes(gpointer key, gpointer value, gpointer user_data);
static void *pool_rip_idling_processes(void *ptr);

bool pool_init() {
   process_pool = g_hash_table_new(g_str_hash, g_str_equal);
   if(!process_pool) return false;
   return true;
}

fcgi_process_t *pool_borrow_process(const char *path) {
   pthread_mutex_lock(&pool_mutex);
   fcgi_process_t *proc = NULL;

   if(g_hash_table_contains(process_pool, path)) {
      GQueue *q = g_hash_table_lookup(process_pool, path);
      proc = q ? g_queue_pop_head(q) : NULL;
   }

   pthread_mutex_unlock(&pool_mutex);
   return proc;
}

void pool_return_process(fcgi_process_t *proc) {
   DEBUG("[process pool] Returning process %d to bucket %s", proc->pid, proc->filepath);
   proc->last_used = time(NULL);
   pthread_mutex_lock(&pool_mutex);

   GQueue *q = g_hash_table_lookup(process_pool, proc->filepath);
   if(!q) {
      char *newkey = strdup(proc->filepath);
      if(!newkey) {
         log_write("[process pool] strdup failed");
         return;
      }
      q = g_queue_new();
      if(!q) {
         log_write("[process_pool] queue allocation failed");
         return;
      }
      g_hash_table_insert(process_pool, newkey, q);
   }

   g_queue_push_head(q, proc);
   pthread_mutex_unlock(&pool_mutex);
}
