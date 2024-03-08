#include <stdio.h>
#include <fcgi_stdio.h>

// gcc env.c -lfcgi -o env.fcgi

int main(int argc, char **argv, char **envp) {
   while (FCGI_Accept() >= 0) {
      printf("Content-type: text/plain\n\n");
      for (char **env = envp; *env != 0; env++) {
         printf("%s\n", (*env));    
      }
   }
}
