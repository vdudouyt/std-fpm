#pragma once
#include <sys/un.h>
#include <unistd.h>

typedef struct fcgi_process_s fcgi_process_t;

struct fcgi_process_s {
	struct sockaddr_un s_un;
   pid_t pid;
};
