#include "fdutils.h"
#include "log.h"
#include <sys/fcntl.h>

void fd_setnonblocking(int fd) {
   int flags = fcntl(fd, F_GETFL, 0);
   if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
      log_write("[fdutils] failed to set non-blocking flag");
   }
}

void fd_setcloseonexec(int fd) {
   int flags = fcntl(fd, F_GETFD, 0);
   if(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
      log_write("[fdutils] failed to set closeonexec flag");
   }
}
