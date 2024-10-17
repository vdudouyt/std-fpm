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

## Installation
1. Install one of our binary packages from releases (recommended), or build from sources with ```cargo build --release ```. If needed, change the running user in ```/usr/lib/systemd/system/std-fpm.service```
2. Start with ```service std-fpm start```. Check for logs in ```/var/log/std-fpm.log```
3. Pick up one of server configuration examples below to configure your Nginx location

Notice: there are some known logging problems with pre-v240 systemd releases.   
If you're experiencing any troubles, starting in foreground mode with ```/usr/sbin/std-fpm -f -c /etc/std-fpm.conf``` is advised.
   
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
   root /var/www/cgi-bin/;
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
## Build binary packages
```nohighlight
$ fakeroot dpkg-buildpackage        # Debian way
$ rpmbuild -bb redhat/std-fpm.spec  # RedHat way
```
