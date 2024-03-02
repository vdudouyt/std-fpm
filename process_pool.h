#pragma once
#include "fcgi_process.h"

typedef struct fcgi_process_bucket_s {
   char process_path[4096];
   struct fcgi_process_bucket_s *next;
   fcgi_process_t *proc_next;
} fcgi_process_bucket_t;

fcgi_process_t *pool_borrow_process(const char *path);
void pool_release_process(fcgi_process_t *proc);
