#include "config.h"
#include "log.h"
#include "units.h"
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <gmodule.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#define SHOW_ERROR_AND_EXIT(...) { log_write(__VA_ARGS__); exit(-1); }

static void print_help_and_exit(int argc, char **argv);

stdfpm_config_t *stdfpm_read_config(int argc, char **argv) {
   stdfpm_config_t *cfg = malloc(sizeof(stdfpm_config_t));
   if(!cfg) SHOW_ERROR_AND_EXIT("[config] malloc failed")
   memset(cfg, 0, sizeof(stdfpm_config_t));

   int c;
   char *cfgpath = "/etc/std-fpm.conf";
   while ((c = getopt (argc, argv, "hfc:")) != -1) {
      switch (c) {
         case 'f':
            cfg->foreground = true;
            break;
         case 'c':
            cfgpath = optarg;
            break;
         case 'h':
         default:
            print_help_and_exit(argc, argv);
      }
   }

   GError *error = NULL;
   GKeyFile *localini = g_key_file_new();

   if (g_key_file_load_from_file(localini, cfgpath, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
      SHOW_ERROR_AND_EXIT("Failed to open %s: %s", cfgpath, error->message);
   }

   const char* user = g_key_file_get_string(localini, "global", "user", NULL);
   if(!user && getuid() == 0) SHOW_ERROR_AND_EXIT("[config] user not specified");

   const char* group = g_key_file_get_string(localini, "global", "group", NULL);
   if(!group && getgid() == 0) SHOW_ERROR_AND_EXIT("[config] group not specified");

   if(user) {
      const struct passwd *pwd = getpwnam(user);
      if(!pwd) SHOW_ERROR_AND_EXIT("[config] user not found: %s", user);
      cfg->uid = pwd->pw_uid;
   }

   if(group) {
      const struct group *grp = getgrnam(group);
      if(!grp) SHOW_ERROR_AND_EXIT("[config] group not found: %s", group);
      cfg->gid = grp->gr_gid;
   }

   cfg->error_log    = g_key_file_get_string(localini, "global", "error_log", &error);
   cfg->listen       = g_key_file_get_string(localini, "global", "listen", &error);
   cfg->pool         = g_key_file_get_string(localini, "global", "pool", &error);
   cfg->extensions   = g_key_file_get_string_list(localini, "global", "extensions", NULL, NULL);
   cfg->worker_threads = g_key_file_get_integer(localini, "global", "worker_threads", &error);
   cfg->process_idle_timeout = parse_time(g_key_file_get_string(localini, "global", "process_idle_timeout", &error));

   if(!cfg->listen) SHOW_ERROR_AND_EXIT("[config] listen not specified");
   if(!cfg->pool) SHOW_ERROR_AND_EXIT("[config] pool not specified");
   if(!cfg->process_idle_timeout) SHOW_ERROR_AND_EXIT("[config] process_idle_timeout not specified");

   return cfg;
}

static void print_help_and_exit(int argc, char **argv) {
   fprintf(stderr, "Usage: %s [-h] [-f] [-c filename]\n", argv[0]);
   fprintf(stderr, "Options:\n");
   fprintf(stderr, "  -f:      foreground mode\n");
   fprintf(stderr, "  -c file: specify an alternate config\n");
   exit(-1);
}
