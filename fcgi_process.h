#pragma once

typedef struct fcgi_process_s fcgi_process_t;

struct fcgi_process_s {
   char process_path[4096];
	struct sockaddr_un s_un;
   pid_t pid;
};
