#pragma once
#include "fcgi_process.h"

typedef struct fcgi_process_bucket_s {
   char process_path[4096];
   struct fcgi_process_bucket_s *next;
   fcgi_process_t *proc_list, *available_list;
} fcgi_process_bucket_t;
