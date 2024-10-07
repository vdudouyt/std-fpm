STD-FPM
=============
![build-and-test](https://github.com/vdudouyt/std-fpm/actions/workflows/build.yml/badge.svg)  

A high-performance language-independent FastCGI process manager
* Written in memory-safe programming language
* Asynchronous & multithreaded
* Dynamic process management
* Tolerant to sleeps/long running IOs and unexpected exits
* Language-independent (with [C](/examples/c/), [C++](/examples/c++/), [Perl](/examples/perl/) and [LuaJIT](/examples/luajit/) examples available)
* Fully compatible with NGINX

## Server configuration examples
### Nginx
**Way 1**: just .fcgi files
```nohighlight
location ~ \.fcgi$ {
   include fastcgi.conf;
   fastcgi_pass unix:/run/std-fpm/std-fpm.sock;
}
```
**Way 2**: a cgi-bin directory
```nohighlight
location /cgi-bin/ {
   rewrite ^/cgi-bin/(.*)\.f?cgi$ /$1.fcgi break;
   include fastcgi.conf;
   fastcgi_pass unix:/run/std-fpm/std-fpm.sock;
}
```
Large file uploads:
```nohighlight
client_max_body_size 0;
fastcgi_request_buffering off;
```
## Build sources
```nohighlight
$ cargo build --release
```
## Build binary packages
```nohighlight
$ fakeroot dpkg-buildpackage        # Debian way
$ rpmbuild -bb redhat/std-fpm.spec  # RedHat way
```
