#include "fcgi_process.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <signal.h>
#include <errno.h>
#include <libgen.h>
#include "log.h"
#include "buf.h"
#include "fcgi_writer.h"
#include "fcgitypes.h"
#include "debug.h"

static void fcgi_serve_response(int listen_sock, const char *response, size_t size);

#define RETURN_ERROR(msg) { log_write(msg); return NULL; }

fcgi_process_t *fcgi_spawn(const char *socketpath, const char *path) {
   DEBUG("[fastcgi spawner] spawning new process: %s", path);

   int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
   if(listen_sock == -1) {
      perror("couldn't create a listener socket");
      RETURN_ERROR("[process pool] couldn't create a listener socket");
   }

   fcgi_process_t *ret = malloc(sizeof(fcgi_process_t));
   if(!ret) {
      close(listen_sock);
      RETURN_ERROR("[process pool] malloc failed");
   }

   memset(ret, sizeof(fcgi_process_t), 0);
   ret->s_un.sun_family = AF_UNIX;
   strncpy(ret->s_un.sun_path, socketpath, sizeof(ret->s_un.sun_path));
   unlink(ret->s_un.sun_path);
   strncpy(ret->filepath, path, sizeof(ret->filepath));

   if(bind(listen_sock, (struct sockaddr *) &ret->s_un, sizeof(ret->s_un)) == -1) {
      close(listen_sock);
      RETURN_ERROR("[process_pool] failed to bind a unix socket");
   }

   chmod(ret->s_un.sun_path, 0777);
   DEBUG("[fastcgi spawner] Listening...");
   if(listen(listen_sock, 1024) == -1) {
      close(listen_sock);
      RETURN_ERROR("[process pool] failed to listen a unix socket");
   }

   pid_t pid = fork();
   if(pid > 0) {
      close(listen_sock);
      DEBUG("[fastcgi spawner] FastCGI process %s is listening at %s", path, ret->s_un.sun_path);
      ret->pid = pid;
      return ret;
   } else if(pid == 0) {
      free(ret);

      char *pathname_copy = strdup(path);
      char *directory_path = pathname_copy ? dirname(pathname_copy) : NULL;
      if(directory_path) chdir(directory_path);
      free(directory_path);

      dup2(listen_sock, STDIN_FILENO);
      char *argv[] = { (char*) path, NULL };
      prctl(PR_SET_PDEATHSIG, SIGHUP); // terminate if parent process exits
      execv(path, argv);

      // execve failed, send error response to std-fpm worker and terminate
      log_write("[fastcgi spawner] failed to start %s: %s", path, strerror(errno));
      char response[128], fcgi_response[256];

      switch(errno) {
         case ENOENT:
            snprintf(response, sizeof(response), "Status: 404\nContent-type: text/html\n\nFile not found." );
            break;
         case EACCES:
            snprintf(response, sizeof(response), "Status: 403\nContent-type: text/html\n\nPermission denied." );
            break;
         default:
            snprintf(response, sizeof(response), "Status: 500\nContent-type: text/html\n\nStartup error: %s(%d)", strerror(errno), errno);
      }

      fcgi_serve_response(listen_sock, response, strlen(response));
      close(listen_sock);
      close(STDIN_FILENO);
      unlink(ret->s_un.sun_path);
      exit(-1);
   } else {
      log_write("[fastcgi spawner] fork failed: %s", strerror(errno));
      close(listen_sock);
      return NULL;
   }
}

static void fcgi_serve_response(int listen_sock, const char *response, size_t size) {
   struct sockaddr_un client_sockaddr;
   unsigned int len = sizeof(client_sockaddr);

   int client_sock = accept(listen_sock, (struct sockaddr *) &client_sockaddr, &len);
   DEBUG("Accepted client sock: sck%d", client_sock);
   if(client_sock == -1) {
      log_write("[fastcgi spawner] socket accept failed, unable to report an error to std-fpm worker");
      return;
   }

   // TODO: read incoming FastCGI messages
   DEBUG("[fastcgi spawner] socket accepted");

   buf_t outBuf;
   buf_reset(&outBuf);
   fcgi_write_buf(&outBuf, 1, FCGI_STDOUT, response, size);
   fcgi_write_buf(&outBuf, 1, FCGI_STDOUT, "", 0);
   fcgi_write_buf(&outBuf, 1, FCGI_END_REQUEST, "\0\0\0\0\0\0\0\0", 8);

   size_t bytes_remaining = buf_bytes_remaining(&outBuf);

   char *buf = outBuf.data;
   while(bytes_remaining > 0) {
      int wr = write(client_sock, buf, bytes_remaining);
      if(wr <= 0) break;
      bytes_remaining -= wr;
      buf = &buf[wr];
   }

   close(client_sock);
}
