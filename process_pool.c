#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "log.h"
#include "process_pool.h"
#include "fcgi_process.h"

static fcgi_process_bucket_t *process_pool = NULL;
static fcgi_process_bucket_t *pool_create_bucket(const char *path);

fcgi_process_t *pool_borrow_process(const char *path) {
   fcgi_process_bucket_t *bucket = NULL;

   for(fcgi_process_bucket_t *it = process_pool; it != NULL; it = it->next) {
      if(!strcmp(it->process_path, path)) {
         bucket = it;
         break;
      }
   }

   if(!bucket) bucket = pool_create_bucket(path);
   log_write("[process pool] Got a bucket: %08x", bucket);
   if(!bucket->proc_next) bucket->proc_next = fcgi_spawn(path);

   fcgi_process_t *ret = bucket->proc_next;
   bucket->proc_next = bucket->proc_next->next;
   ret->bucket = bucket;
   return ret;
}

void pool_release_process(fcgi_process_t *proc) {
   fcgi_process_bucket_t *bucket = proc->bucket;
   log_write("[process pool] Releasing process %d from bucket %s", proc->pid, bucket->process_path);

   proc->next = bucket->proc_next;
   bucket->proc_next = proc;
}

fcgi_process_bucket_t *pool_create_bucket(const char *path) {
   log_write("[process pool] Creating process bucket: %s", path);
   fcgi_process_bucket_t *ret = malloc(sizeof(fcgi_process_bucket_t));
   assert(ret);
   memset(ret, 0, sizeof(fcgi_process_bucket_t));
   strcpy(ret->process_path, path); // TODO: fix buffer overflow
   ret->next = process_pool;
   process_pool = ret;
   return ret;
}
