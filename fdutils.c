#include "fdutils.h"
#include <sys/fcntl.h>
#include <assert.h>

void fd_setnonblocking(int fd) {
   int flags = fcntl(fd, F_GETFL, 0);
   assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
}

void fd_setcloseonexec(int fd) {
   int flags = fcntl(fd, F_GETFD, 0);
   assert(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1);
}
