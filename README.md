STD-FPM
=============
A language-independent FastCGI process manager
* Written in memory-safe programming language
* Asynchronous & multithreaded
* Implements dynamic process management
* Tolerant to sleeps/long running IOs and unexpected exits
* Language-independent (with [C](/examples/c/), [C++](/examples/c++/), [Perl](/examples/perl/) and [LuaJIT](/examples/luajit/) examples available)
* Fully compatible with NGINX

## Server configuration examples
### Nginx (recommended)
```nohighlight
location ~ \.fcgi$ {
   include fastcgi.conf;
   fastcgi_pass unix:/run/std-fpm/std-fpm.sock;
}
```
### Apache
```nohighlight
$ a2dismod fcgid # or untie .fcgi handler in Apache configuration
$ a2enmod proxy_fcgi
```
```nohighlight
<FilesMatch \.fcgi$>
     SetHandler "proxy:unix:/run/std-fpm/std-fpm.sock|fcgi://localhost/"
</FilesMatch>
```
## Build sources
```nohighlight
$ cargo build --release
```
## Build binary packages
```nohighlight
$ fakeroot dpkg-buildpackage -nc    # Debian way
$ rpmbuild -bb redhat/std-fpm.spec  # RedHat way
```
