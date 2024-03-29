#include <fcgio.h>
#include <fcgiapp.h>
#include <ostream>

// compile: g++ hello.cpp -lfcgi -lfcgi++ -o hello.fcgi

int main()
{
   FCGX_Request request;
   FCGX_Init();
   FCGX_InitRequest(&request, 0, 0);

   unsigned int i = 0;

   while (FCGX_Accept_r(&request) == 0) {
      fcgi_streambuf cout_fcgi_streambuf(request.out);
      std::ostream os{&cout_fcgi_streambuf};
      os << "Content-type: text/html" << std::endl << std::endl;
      os << "Hello! You have requested this page " << i << " times<br>" << std::endl;
      os << "Your IP address is: " << FCGX_GetParam("REMOTE_ADDR", request.envp);
      i++;
   }
}
