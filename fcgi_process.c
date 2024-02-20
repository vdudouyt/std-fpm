#include "fcgi_process.h"

fcgi_process_t *fcgi_spawn(const char *path) {
   int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
   assert(listen_sock != -1);
   fcgi_process_t *ret = malloc(sizeof(fcgi_process_t));
   ret->s_un.sun_family = AF_UNIX;
   sprintf(ret->s_un.sun_path, "/tmp/stdfpm-%d.sock", process_count);
   unlink(ret->s_un.sun_path);

   process_count++;
   assert(bind(listen_sock, (struct sockaddr *) &ret->s_un, sizeof(ret->s_un)) != -1);
   chmod(ret->s_un.sun_path, 0777);
   printf("Listening...\n");
   assert(listen(listen_sock, 1024) != -1);

   pid_t pid = fork();
   if(pid > 0) {
      close(listen_sock);
      printf("FastCGI process %s is successfully started with PID=%d\n", path, pid);
      strcpy(ret->process_path, path); // TODO: fix buffer overflow
      ret->pid = pid;
      return ret;
   } else {
      free(ret);
      dup2(listen_sock, STDIN_FILENO);
      char *argv[] = { (char*) path, NULL };
      execv(path, argv);
      printf("execv failed\n");
   }
}
