STD-FPM
=============
A high-performance language-agnostic FastCGI process manager
* Highly efficient asynchronous event-driven architecture
* Works with almost any programming language ([C](/examples/c/), [C++](/examples/c++/), [Perl](/examples/perl/) and [LuaJIT](/examples/luajit/) examples available)
* Dynamically controls FastCGI process population depending on incoming traffic
* Not getting blocked by frequently exiting FastCGI handlers / slow-running I/Os
* Compatible with the most of HTTP server software available
* Almost no configuration required (see below)

## Comparison with similar software
|                            | STD-FPM | PHP-FPM | mod_fcgid | spawn-fcgi |
| -------------------------- | ------- | ------- | --------- | -----------|
| Languages                  | Any     | PHP     | Any       | Any        |
| Web Servers                | Any     | Any     | Apache    | Any        |
| Dynamic process management | Y       | Y       | Y         | N          |
| SCRIPT_FILENAME routing    | Y       | Y       | Y         | N          |
| Unexpected exits tolerance | Y       | ?       | N         | ?          |
| Long-running I/O tolerance | Y       | ?       | N         | N          |

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
## Build binary packages
```nohighlight
$ fakeroot dpkg-buildpackage -nc    # Debian way
$ rpmbuild -bb redhat/std-fpm.spec  # RedHat way
```
