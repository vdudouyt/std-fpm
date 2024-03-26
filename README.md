STD-FPM
=============
A high-performance language-agnostic FastCGI process manager
* Highly efficient asynchronous event-driven architecture
* Starts and manages everything that supports the [FastCGI Startup Protocol](https://www.mit.edu/~yandros/doc/specs/fcgi-spec.html#S2.2) (refer to available [examples](/examples/))
* Dynamically controls FastCGI process population depending on incoming traffic patterns
* Compatible with the most of HTTP server software available
* Almost no configuration required

## Q&A
**Q**: Can I use long-running I/O in my .fcgi scripts?  
**A**: Absolutely yes, our worker wouldn't be blocked by that. If a new request comes in meanwhile, it gets served with another .fcgi process, or with a new one if there's no processes left.

**Q**: What if my FastCGI process exits (e.g. by literally calling ``exit()`` or due to some runtime failure)?  
**A**: This software isn't suffering from the infamous ``mod_fcgid: can't apply process slot for`` problem. If some .fcgi process is found dead, it just frees the resources and uses a first alive one, or starts a new one if there's no processes left. But, keeping process starts count as low as possible may be good for your performance.

## Webserver configuration examples

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
