#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "log.h"

static bool log_echo = false;
FILE *f = NULL;

bool log_open(const char *path) {
   f = fopen(path, "w+");
   return f != NULL;
}

void log_set_echo(bool new_value) {
   log_echo = true;
}

void log_write(const char *fmt, ...) {
   time_t rawtime;
   time(&rawtime);
   struct tm *info = localtime(&rawtime);
   char buf[256];
   strftime(buf, sizeof(buf), "[%Y-%m-%d %H:%M:%S]", info);
   if(f) fprintf(f, "%s[%d] ", buf, getpid());
   if(log_echo) printf("%s[%d] ", buf, getpid());
   
   va_list args, args2;
   va_start(args, fmt);
   va_copy(args2, args);
   if(f) vfprintf(f, fmt, args);
   if(log_echo) vprintf(fmt, args2);
   va_end(args);
   va_end(args2);
   
   if(f) {
      fprintf(f, "\n");
      fflush(f);
   }
   
   if(log_echo) printf("\n");
}
