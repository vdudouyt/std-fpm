#pragma once
#include <sys/un.h>
#include <unistd.h>

typedef struct fcgi_process_s fcgi_process_t;

struct fcgi_process_s {
	struct sockaddr_un s_un;
   pid_t pid;
   struct fcgi_process_s *next;

   int fd;
   void *bucket;
};

fcgi_process_t *fcgi_spawn(const char *path);
