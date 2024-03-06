#include "fcgi_process.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include "log.h"
#include "debug.h"

static unsigned int process_count = 0;

#define RETURN_ERROR(msg) { log_write(msg); return NULL; }

fcgi_process_t *fcgi_spawn(const char *path) {
   DEBUG("[fastcgi spawner] spawning new process: %s", path);

   int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
   if(listen_sock == -1) RETURN_ERROR("[process pool] couldn't create a listener socket");

   fcgi_process_t *ret = malloc(sizeof(fcgi_process_t));
   if(!ret) RETURN_ERROR("[process pool] malloc failed");

   memset(ret, sizeof(fcgi_process_t), 0);
   ret->s_un.sun_family = AF_UNIX;
   sprintf(ret->s_un.sun_path, "/tmp/stdfpm-%d.sock", process_count);
   unlink(ret->s_un.sun_path);

   strncpy(ret->filepath, path, sizeof(ret->filepath));

   process_count++;
   if(bind(listen_sock, (struct sockaddr *) &ret->s_un, sizeof(ret->s_un)) == -1) {
      RETURN_ERROR("[process_pool] failed to bind a unix socket");
   }

   chmod(ret->s_un.sun_path, 0777);
   DEBUG("[fastcgi spawner] Listening...");
   if(listen(listen_sock, 1024) == -1) {
      RETURN_ERROR("[process pool] failed to listen a unix socket");
   }

   pid_t pid = fork();
   if(pid > 0) {
      close(listen_sock);
      DEBUG("[fastcgi spawner] FastCGI process %s is listening at %s", path, ret->s_un.sun_path);
      ret->pid = pid;
      return ret;
   } else {
      free(ret);
      dup2(listen_sock, STDIN_FILENO);
      char *argv[] = { (char*) path, NULL };
      execv(path, argv);
      DEBUG("[process_pool] failed to start %s: %s", path, strerror(errno));
      exit(-1);
   }
}
