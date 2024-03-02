#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "log.h"
#include "process_pool.h"

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
   if(!bucket->next) bucket->next = fcgi_spawn(path);
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

void pool_release_process(fcgi_process_t *proc) {
}
