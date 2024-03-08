#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <gmodule.h>
#include "fdutils.h"
#include "log.h"
#include "debug.h"
#include "process_pool.h"
#include "fcgi_process.h"

#define RETURN_ERROR(msg) { log_write(msg); return NULL; }

static char *pool_path = NULL;
static GHashTable *process_pool = NULL;
static unsigned int startup_counter = 0;

static bool pool_connect_process(fcgi_process_t *proc);
static fcgi_process_t *pool_borrow_existing_process(const char *path);
static fcgi_process_t *pool_create_process(const char *path);

bool pool_init(const char *path) {
   process_pool = g_hash_table_new(g_str_hash, g_str_equal);
   if(!process_pool) return false;

   pool_path = strdup(path);
   if(!pool_path) return false;

   return true;
}

fcgi_process_t *pool_borrow_process(const char *path) {
   if(!g_hash_table_contains(process_pool, path)) {
      char *newkey = strdup(path);
      if(!newkey) RETURN_ERROR("[process pool] strdup failed");
      g_hash_table_insert(process_pool, newkey, NULL);
   }

   fcgi_process_t *proc = pool_borrow_existing_process(path);
   if(!proc) proc = pool_create_process(path);
   if(proc) proc->last_used = time(NULL);

   return proc;
}

void pool_release_process(fcgi_process_t *proc) {
   DEBUG("[process pool] Releasing process %d to bucket %s", proc->pid, proc->filepath);
   GList *bucket = g_hash_table_lookup(process_pool, proc->filepath);
   bucket = g_list_prepend(bucket, proc);
   g_hash_table_insert(process_pool, proc->filepath, bucket);
   close(proc->fd);
}

static bool pool_connect_process(fcgi_process_t *proc) {
   proc->fd = socket(AF_UNIX, SOCK_STREAM, 0);
   if(proc->fd == -1) return false;
   if(connect(proc->fd, (struct sockaddr *) &proc->s_un, sizeof(proc->s_un)) == -1) {
      close(proc->fd);
      return false;
   }
   fd_setnonblocking(proc->fd);
   fd_setcloseonexec(proc->fd);
   DEBUG("[process pool] opened connection to %s", proc->s_un.sun_path);
   return true;
}

// TODO: free dead processes

static fcgi_process_t *pool_borrow_existing_process(const char *path) {
   GList *bucket = g_hash_table_lookup(process_pool, path);
   fcgi_process_t *proc = NULL;

   while(bucket != NULL) {
      GList *next = bucket->next;
      proc = bucket->data;
      bucket = g_list_delete_link(bucket, bucket);
      bucket = next;

      if(pool_connect_process(proc)) {
         break;
      } else {
         free(proc);
         proc = NULL;
      }
   }

   g_hash_table_insert(process_pool, (char *) path, bucket);
   return proc;
}

static fcgi_process_t *pool_create_process(const char *path) {
   char socket_path[4096];
   startup_counter++;
   snprintf(socket_path, sizeof(socket_path), "%s/stdfpm-%d.sock", pool_path, startup_counter);

   fcgi_process_t *proc = fcgi_spawn(socket_path, path);
   if(!proc) return NULL;
   if(pool_connect_process(proc)) {
      return proc;
   } else {
      free(proc);
      return NULL;
   }
}

static void pool_hash_function(gpointer key, gpointer value, gpointer user_data);

void pool_shutdown_inactive_processes(unsigned int max_idling_time) {
   g_hash_table_foreach(process_pool, pool_hash_function, &max_idling_time);
}

static void pool_hash_function(gpointer key, gpointer value, gpointer user_data) {
   unsigned int max_idling_time = *(unsigned int*) user_data;
   GList *bucket = value;
   GList *it = bucket;
   time_t curtime = time(NULL);

   while(it != NULL) {
      GList *next = it->next;
      fcgi_process_t *proc = it->data;
      if(curtime - proc->last_used >= max_idling_time) {
         DEBUG("[process pool] removing idling process: %s", key);
         kill(proc->pid, SIGTERM);
         free(proc);
         bucket = g_list_delete_link(bucket, it);
      }
      it = next;
   }
   g_hash_table_insert(process_pool, key, bucket);
}