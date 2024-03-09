#pragma once
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

typedef struct fcgi_process_s fcgi_process_t;

struct fcgi_process_s {
	char filepath[4096];
	struct sockaddr_un s_un;
   pid_t pid;
   int fd;
   time_t last_used;
};

fcgi_process_t *fcgi_spawn(const char *socketpath, const char *path);
