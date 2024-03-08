#pragma once
#include <stdbool.h>
#include <sys/types.h>

typedef struct {
   uid_t uid;
   gid_t gid;

   const char *pid;
   const char *error_log;
   const char *listen;
   const char *pool;

   bool foreground;
} stdfpm_config_t;

stdfpm_config_t *stdfpm_read_config(int argc, char **argv);