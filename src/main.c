#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <libgen.h>

#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>

#include "log.h"
#include "debug.h"
#include "conn.h"
#include "fcgi_parser.h"
#include "fcgitypes.h"
#include "debug_utils.h"
#include "process_pool.h"
#include "fcgi_writer.h"
#include "config.h"
#include "events.h"

#define EXIT_WITH_ERROR(...) { log_write(__VA_ARGS__); exit(-1); }

void check_dir_exists(const char *dirpath, mode_t mode, uid_t uid, gid_t gid) {
   if(access(dirpath, F_OK) == -1) {
      if(mkdir(dirpath, mode) != 0) {
         log_write("Failed to create %s: %s", dirpath, strerror(errno));
      }
   }
   chmod(dirpath, mode);
   if(uid) chown(dirpath, uid, gid);
}

int main(int argc, char **argv) {
   log_set_echo(true);

   stdfpm_config_t *cfg = stdfpm_read_config(argc, argv);
   if(!cfg) EXIT_WITH_ERROR("Read config failed: %s", strerror(errno));

   char *listenpath = strdup(cfg->listen);
   check_dir_exists(dirname(listenpath), 0755, cfg->uid, cfg->gid);
   check_dir_exists(cfg->pool, 0755, cfg->uid, cfg->gid);
   free(listenpath);

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

   struct event_base *base = event_base_new();
   struct evconnlistener *listener = stdfpm_create_listener(base, cfg->listen, cfg);
   event_base_dispatch(base);
   event_base_free(base);
}
