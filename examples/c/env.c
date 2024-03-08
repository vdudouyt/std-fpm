#include <stdio.h>
#include <fcgi_stdio.h>
#include <stdlib.h>

// gcc env.c -lfcgi -o env.fcgi

extern char **environ;

int main(int argc, char **argv, char **envp) {
   while (FCGI_Accept() >= 0) {
      printf("Content-type: text/plain\n\n");

      char **env = environ;
      for(env; *env; env++) {
         printf("%s\n", (*env));    
      }
   }
}
