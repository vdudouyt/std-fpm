#include <stdio.h>
#include <fcgi_stdio.h>

// compile: gcc test.c -lfcgi -o test.fcgi

static int count = 1;

int main() {
   while (FCGI_Accept() >= 0) {
      printf("Content-type: text/html\n\n");
      printf("Hello! You have requested this page %d time(s)", count);
      count++;
   }
}

