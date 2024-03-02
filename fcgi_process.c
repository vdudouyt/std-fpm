#include "fcgi_process.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <assert.h>
#include "log.h"

static unsigned int process_count = 0;

fcgi_process_t *fcgi_spawn(const char *path) {
   log_write("[fastcgi spawner] spawning new process: %s", path);

   int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
   assert(listen_sock != -1);
   fcgi_process_t *ret = malloc(sizeof(fcgi_process_t));
   memset(ret, sizeof(fcgi_process_t), 0);
   ret->s_un.sun_family = AF_UNIX;
   sprintf(ret->s_un.sun_path, "/tmp/stdfpm-%d.sock", process_count);
   unlink(ret->s_un.sun_path);

   process_count++;
   assert(bind(listen_sock, (struct sockaddr *) &ret->s_un, sizeof(ret->s_un)) != -1);
   chmod(ret->s_un.sun_path, 0777);
   log_write("[fastcgi spawner] Listening...");
   assert(listen(listen_sock, 1024) != -1);

   pid_t pid = fork();
   if(pid > 0) {
      close(listen_sock);
      log_write("[fastcgi spawner] FastCGI process %s is successfully started with PID=%d", path, pid);
      ret->pid = pid;
      return ret;
   } else {
      free(ret);
      dup2(listen_sock, STDIN_FILENO);
      char *argv[] = { (char*) path, NULL };
      execv(path, argv);
      log_write("execv failed");
   }
}
