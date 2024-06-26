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
#include "fcgi_writer.h"
#include "fcgitypes.h"
#include "debug.h"

#define RETURN_ERROR(msg) { log_write(msg); return NULL; }

static void parse_path(const char *path, char **dirname, char **basename, char **rel_basename);

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

   memset(ret, 0, sizeof(fcgi_process_t));
   ret->s_un.sun_family = AF_UNIX;
   strncpy(ret->s_un.sun_path, socketpath, sizeof(ret->s_un.sun_path));
   unlink(ret->s_un.sun_path);
   strncpy(ret->filepath, path, sizeof(ret->filepath));

   if(bind(listen_sock, (struct sockaddr *) &ret->s_un, sizeof(ret->s_un)) == -1) {
      log_write("[process_pool] failed to bind a unix socket at %s: %s", ret->s_un.sun_path, strerror(errno));
      close(listen_sock);
      free(ret);
      return NULL;
   }

   chmod(ret->s_un.sun_path, 0777);
   DEBUG("[fastcgi spawner] Listening...");
   if(listen(listen_sock, 1024) == -1) {
      close(listen_sock);
      free(ret);
      RETURN_ERROR("[process pool] failed to listen a unix socket");
   }

   pid_t pid = fork();
   if(pid > 0) {
      close(listen_sock);
      DEBUG("[fastcgi spawner] FastCGI process %s is listening at %s", path, ret->s_un.sun_path);
      ret->pid = pid;
      return ret;
   } else if(pid == 0) {
      char *dirpath, *basename, *rel_basename;
      parse_path(path, &dirpath, &basename, &rel_basename);
      DEBUG("Parsed path: path = %s, dirpath = %s, basename = %s, rel_basename = %s", path, dirpath, basename, rel_basename);

      if(!basename || !rel_basename) {
         log_write("[fcgi_process] strdup failed");
         return NULL;
      }

      if(dirpath) {
         if(chdir(dirpath) != 0) log_write("[process pool] failed to chdir: %s", dirpath);
      }

      free(dirpath);
      free(basename);

      dup2(listen_sock, STDIN_FILENO);
      char *argv[] = { (char*) rel_basename, NULL };
      prctl(PR_SET_PDEATHSIG, SIGHUP); // terminate if parent process exits
      execv(rel_basename, argv);

      // execve failed, send error response to std-fpm worker and terminate
      log_write("[fastcgi spawner] failed to start %s: %s", path, strerror(errno));
      char response[128];

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
      free(ret);
      exit(-1);
   } else {
      log_write("[fastcgi spawner] fork failed: %s", strerror(errno));
      close(listen_sock);
      free(ret);
      return NULL;
   }
}

static void parse_path(const char *path, char **dirname, char **basename, char **rel_basename) {
   char *sep = strrchr(path, '/');
   if(sep) {
      *dirname = strndup(path, sep - path + 1);
      *basename = strdup(sep + 1);
   } else {
      *basename = strdup(path);
      *dirname = NULL;
   }
   if(*basename && (*rel_basename = malloc(strlen(*basename) + 3))) {
      strcpy(*rel_basename, "./");
      strcat(*rel_basename, *basename);
   }
}
