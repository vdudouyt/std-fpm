STD-FPM
=============
A high-performance language-agnostic FastCGI process manager
* Highly efficient event-driven architecture based on non-blocking I/O
* Built on the top of [libevent](https://en.wikipedia.org/wiki/Libevent), known for powering such software as Google Chrome or [Memcached](https://en.wikipedia.org/wiki/Memcached)
* Starts and manages everything that supports the [FastCGI Startup Protocol](https://www.mit.edu/~yandros/doc/specs/fcgi-spec.html#S2.2) (refer to available [examples](/examples/))
* Dynamically controls FastCGI process population depending on incoming traffic patterns
* Compatible with the most of HTTP server software available
* Almost no configuration required

## Webserver configuration examples

### Nginx (through *ngx_http_fastcgi_module*)
Nginx is a recommended way to run ``std-fpm``. Configuring it up is as easy as adding the following lines into your nginx.conf (in the fact, it is not different from PHP-FPM configuration):
```nohighlight
location ~ \.fcgi$ {
   include fastcgi.conf;
   fastcgi_pass unix:/run/std-fpm/std-fpm.sock;
}
```

### Apache (through *mod_proxy_fcgi*)
Unlike for the native ``mod_fcgid``, ``std-fpm`` is completely free from that nasty *can't apply process slot* problem and can be easily used as a drop-in replacement (or in the environments where enabling ``mod_fcgid`` is complicated):
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
