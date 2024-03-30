#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>

#include "log.h"
#include "debug.h"
#include "conn.h"
#include "fcgi_parser.h"
#include "fcgi_params_parser.h"
#include "fcgitypes.h"
#include "debug_utils.h"
#include "process_pool.h"
#include "fcgi_writer.h"
#include "config.h"
#include "events.h"

static stdfpm_config_t *cfg = NULL;

#define EXIT_WITH_ERROR(...) { log_write(__VA_ARGS__); exit(-1); }

static int stdfpm_create_listening_socket(const char *sock_path) {
   int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
   if(listen_sock == -1) perror("[main] couldn't create a listener socket");

   struct sockaddr_un s_un;
   s_un.sun_family = AF_UNIX;
   strcpy(s_un.sun_path, sock_path);

   if(connect(listen_sock, (struct sockaddr *) &s_un, sizeof(s_un)) == -1) {
      unlink(s_un.sun_path); // Socket is in use, prepare for binding
   }

   if(bind(listen_sock, (struct sockaddr *) &s_un, sizeof(s_un)) == -1) {
      perror("[main] failed to bind a unix domain socket");
   }

   chmod(s_un.sun_path, 0777);
   if(listen(listen_sock, 1024) == -1) {
      perror("[main] failed to listen a unix domain socket");
   }

   return listen_sock;
}

int main(int argc, char **argv) {
   log_set_echo(true);

   cfg = stdfpm_read_config(argc, argv);
   if(!cfg) EXIT_WITH_ERROR("Read config failed: %s", strerror(errno));
   if(!log_open(cfg->error_log)) EXIT_WITH_ERROR("Couldn't open %s: %s", cfg->error_log, strerror(errno));

   if(cfg->gid > 0 && setgid(cfg->gid) == -1) EXIT_WITH_ERROR("Couldn't set process gid: %s", strerror(errno));
   if(cfg->uid > 0 && setuid(cfg->uid) == -1) EXIT_WITH_ERROR("Couldn't set process uid: %s", strerror(errno));
   if(!pool_init(cfg->pool)) EXIT_WITH_ERROR("Pool initialization failed (malloc?)");

   if(!cfg->foreground) {
      log_set_echo(false);
      if(daemon(0, 0) != 0) EXIT_WITH_ERROR("Couldn't daemonize");
   }

   signal(SIGPIPE, SIG_IGN);
   signal(SIGCHLD, SIG_IGN);

   worker_t **workers = calloc(cfg->worker_threads, sizeof(worker_t*));
   if(!workers) EXIT_WITH_ERROR("Failed to allocate workers pool: %s", strerror(errno));
   if(cfg->worker_threads <= 0) EXIT_WITH_ERROR("worker_threads count should be greated than zero");

   for(unsigned int i = 0; i < cfg->worker_threads; i++) {
      workers[i] = start_worker(stdfpm_socket_accepted_cb);
      workers[i]->config = cfg;
   }

   struct sockaddr_un client_sockaddr;
   unsigned int len = sizeof(client_sockaddr);
   unsigned int ctr = 0;

   int listen_sock = stdfpm_create_listening_socket(cfg->listen);
   pool_start_inactivity_detector(cfg->process_idle_timeout);

   while (1) {
      int fd = accept(listen_sock, (struct sockaddr *) &client_sockaddr, &len);
      if(fd == -1)  {
         log_write("Dispatcher: failed while accepting connection: %s", strerror(errno));
         continue;
      }
      DEBUG("Dispatcher: connection accepted: %d", fd);
      unsigned int tid = (ctr++) % cfg->worker_threads;

      worker_t *worker = workers[tid];
      pthread_mutex_lock(&worker->conn_queue_mutex);
      g_queue_push_head(worker->conn_queue, GINT_TO_POINTER(fd));
      pthread_mutex_unlock(&worker->conn_queue_mutex);

      if(write(worker->notify_send_fd, "C", 1) != 1) {
         log_write("Worker %d wakeup pipe write failed: %s", worker->tid, strerror(errno));
      }
   }
}
