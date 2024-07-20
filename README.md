STD-FPM
=============
A high-performance language-independent FastCGI process manager
* Written in memory-safe programming language
* Asynchronous & multithreaded
* Dynamic process management
* Tolerant to sleeps/long running IOs and unexpected exits
* Language-independent (with [C](/examples/c/), [C++](/examples/c++/), [Perl](/examples/perl/) and [LuaJIT](/examples/luajit/) examples available)
* Fully compatible with NGINX

## Server configuration examples
### Nginx
```nohighlight
location ~ \.fcgi$ {
   include fastcgi.conf;
   fastcgi_pass unix:/run/std-fpm/std-fpm.sock;
}
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
