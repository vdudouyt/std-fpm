#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <gmodule.h>
#include "fdutils.h"
#include "log.h"
#include "process_pool.h"
#include "fcgi_process.h"

static GHashTable *process_pool = NULL;

fcgi_process_t *pool_borrow_process(const char *path) {
   if(!process_pool) process_pool = g_hash_table_new(g_str_hash, g_str_equal);
   GList *bucket = g_hash_table_lookup(process_pool, path);
   if(!bucket) bucket = g_list_prepend(bucket, fcgi_spawn(path));

   fcgi_process_t *ret = bucket->data;
   ret->fd = socket(AF_UNIX, SOCK_STREAM, 0);
   assert(ret->fd != -1);
   assert(connect(ret->fd, (struct sockaddr *) &ret->s_un, sizeof(ret->s_un)) != -1);
   fd_setnonblocking(ret->fd);
   fd_setcloseonexec(ret->fd);
   log_write("[process pool] opened connection to %s", ret->s_un.sun_path);

   bucket = g_list_delete_link(bucket, bucket);
   g_hash_table_insert(process_pool, ret->filepath, bucket);
   return ret;
}

void pool_release_process(fcgi_process_t *proc) {
   log_write("[process pool] Releasing process %d to bucket %s", proc->pid, proc->filepath);
   GList *bucket = g_hash_table_lookup(process_pool, proc->filepath);
   bucket = g_list_prepend(bucket, proc);
   g_hash_table_insert(process_pool, proc->filepath, bucket);
   close(proc->fd);
}
