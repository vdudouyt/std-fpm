#pragma once

typedef struct conn_s conn_t;
typedef struct fcgi_process_s fcgi_process_t;
typedef struct fcgi_client_s fcgi_client_t;

struct fcgi_client_s {
   fcgi_parser_t *msg_parser;
   fcgi_params_parser_t *params_parser;
};

struct conn_s {
   int fd;
   int bytes_in_buf, write_pos;
   char outBuf[65536];
   conn_t *pipe_to;

   enum { STDFPM_LISTEN_SOCK = 1, STDFPM_FCGI_CLIENT, STDFPM_FCGI_PROCESS } type;
   fcgi_client_t *client;
   fcgi_process_t *process;
};

struct fcgi_process_s {
   char process_path[4096];
	struct sockaddr_un s_un;
   pid_t pid;
};
