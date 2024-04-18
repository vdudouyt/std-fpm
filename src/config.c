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
#define CFG_LOAD_OPTION(option, section, method) {\
   cfg->option = (method)(localini, (section), #option, &error); \
   if(error) SHOW_ERROR_AND_EXIT("[config] %s not specified", #option);\
}

static void print_help_and_exit(int argc, char **argv);
static char **cfg_get_string_list(GKeyFile *keyfile, const gchar* group_name, const gchar *key, GError **error);
static uid_t cfg_getuid(const char *user);
static gid_t cfg_getgid(const char *group);

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

   if(getuid() == 0) {
      CFG_LOAD_OPTION(user, "global", g_key_file_get_string);
      CFG_LOAD_OPTION(group, "global", g_key_file_get_string);
      cfg->uid = cfg_getuid(cfg->user);
      cfg->gid = cfg_getgid(cfg->group);
   }

   CFG_LOAD_OPTION(error_log, "global", g_key_file_get_string);
   CFG_LOAD_OPTION(listen, "global", g_key_file_get_string);
   CFG_LOAD_OPTION(pool, "global", g_key_file_get_string);
   CFG_LOAD_OPTION(extensions, "global", cfg_get_string_list);
   CFG_LOAD_OPTION(process_idle_timeout, "global", g_key_file_get_integer);
   CFG_LOAD_OPTION(error_log, "global", g_key_file_get_string);

   return cfg;
}

static char **cfg_get_string_list(GKeyFile *keyfile, const gchar* group_name, const gchar *key, GError **error) {
   return g_key_file_get_string_list(keyfile, group_name, key, NULL, error);
}

static uid_t cfg_getuid(const char *user) {
   const struct passwd *pwd = getpwnam(user);
   if(!pwd) SHOW_ERROR_AND_EXIT("[config] user not found: %s", user);
   return pwd->pw_uid;
}

static gid_t cfg_getgid(const char *group) {
   const struct group *grp = getgrnam(group);
   if(!grp) SHOW_ERROR_AND_EXIT("[config] group not found: %s", group);
   return grp->gr_gid;
}

static void print_help_and_exit(int argc, char **argv) {
   fprintf(stderr, "Usage: %s [-h] [-f] [-c filename]\n", argv[0]);
   fprintf(stderr, "Options:\n");
   fprintf(stderr, "  -f:      foreground mode\n");
   fprintf(stderr, "  -c file: specify an alternate config\n");
   exit(-1);
}
