#include <fcgio.h>
#include <fcgiapp.h>
#include <ostream>
#include <fstream>

// compile: g++ put.cpp -lfcgi -lfcgi++ -o put.fcgi
// Usage example:
// $ curl -X PUT 'http://localhost/put.fcgi' --data-binary "@/tmp/1mb.bin"
// OK

int main()
{
   FCGX_Request request;
   FCGX_Init();
   FCGX_InitRequest(&request, 0, 0);

   while (FCGX_Accept_r(&request) == 0) {
      fcgi_streambuf cin_fcgi_streambuf(request.in);
      fcgi_streambuf cout_fcgi_streambuf(request.out);
      std::istream is{&cin_fcgi_streambuf};
      std::ostream os{&cout_fcgi_streambuf};
      std::ofstream file("/tmp/file.bin", std::ios::out | std::ios::binary);

      char buf[1024 * 64];
      while(!is.eof()) {
         size_t bytes_read = is.read(buf, sizeof(buf)).gcount();
         file.write(buf, bytes_read);
      }

      os << "Content-type: text/html" << std::endl << std::endl << "OK";
   }
}
