STD-FPM
=============
A language-agnostic FastCGI process manager
* Highly efficient event-driven architecture based on non-blocking I/O
* Starts and manages everything that supports the [FastCGI Startup Protocol](https://www.mit.edu/~yandros/doc/specs/fcgi-spec.html#S2.2)
* Dynamically controls FastCGI process population depending on incoming traffic
* Compatible with Nginx (recommended) and Apache
* Almost no configuration required

## Webserver configuration examples

### Nginx (through *ngx_http_fastcgi_module*, recommended)
```nohighlight
location ~ \.fcgi$ {
   include fastcgi.conf;
   fastcgi_pass unix:/run/std-fpm/std-fpm.sock;
}
```

### Apache (through *mod_proxy_fcgi*)
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
