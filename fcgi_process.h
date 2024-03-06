#pragma once
#include <sys/un.h>
#include <unistd.h>

typedef struct fcgi_process_s fcgi_process_t;

struct fcgi_process_s {
	char filepath[4096];
	struct sockaddr_un s_un;
   pid_t pid;
   int fd;
};

fcgi_process_t *fcgi_spawn(const char *path);
