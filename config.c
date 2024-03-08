#include "config.h"
#include "log.h"
#include <stdlib.h>
#include <getopt.h>
#include <gmodule.h>
#include <pwd.h>
#include <grp.h>

#define SHOW_ERROR_AND_EXIT(...) { log_write(__VA_ARGS__); exit(-1); }

stdfpm_config_t *stdfpm_read_config(int argc, char **argv) {
   stdfpm_config_t *cfg = malloc(sizeof(stdfpm_config_t));
   if(!cfg) SHOW_ERROR_AND_EXIT("[config] malloc failed")
   memset(cfg, 0, sizeof(stdfpm_config_t));

   int c;
   char *cfgpath = "/etc/std-fpm.conf";
   while ((c = getopt (argc, argv, "fc:")) != -1) {
      switch (c) {
         case 'f':
            cfg->foreground = true;
            break;
         case 'c':
            cfgpath = optarg;
            break;
      }
   }

   GError *error = NULL;
   GKeyFile *localini = g_key_file_new();
   if (g_key_file_load_from_file(localini, cfgpath, G_KEY_FILE_KEEP_COMMENTS, &error) == FALSE) {
      SHOW_ERROR_AND_EXIT("Failed to open %s: %s", cfgpath, error->message);
   }

   const char* user = g_key_file_get_string(localini, "global", "user", &error);
   if(!user) SHOW_ERROR_AND_EXIT("[config] user not specified");

   const char* group = g_key_file_get_string(localini, "global", "group", &error);
   if(!user) SHOW_ERROR_AND_EXIT("[config] group not specified");

   const struct passwd *pwd = getpwnam(user);
   if(!pwd) SHOW_ERROR_AND_EXIT("[config] user not found: %s", user);

   const struct group *grp = getgrnam(group);
   if(!grp) SHOW_ERROR_AND_EXIT("[config] group not found: %s", group);

   cfg->pid       = g_key_file_get_string(localini, "global", "pid", &error);
   cfg->error_log = g_key_file_get_string(localini, "global", "error_log", &error);
   cfg->listen    = g_key_file_get_string(localini, "global", "listen", &error);
   cfg->pool      = g_key_file_get_string(localini, "global", "pool", &error);
   cfg->uid       = pwd->pw_uid;
   cfg->gid       = grp->gr_gid;

   return cfg;
}
