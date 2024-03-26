STD-FPM
=============
A high-performance language-agnostic FastCGI process manager
* Highly efficient asynchronous event-driven architecture
* Starts and manages everything that supports the [FastCGI Startup Protocol](https://www.mit.edu/~yandros/doc/specs/fcgi-spec.html#S2.2) (refer to available [examples](/examples/))
* Dynamically controls FastCGI process population depending on incoming traffic patterns
* Compatible with the most of HTTP server software available
* Almost no configuration required

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
## Q&A
**Q**: Can I use long-running I/O in my .fcgi scripts?  
**A**: Absolutely yes, this wouldn't even cause a worker thread block to occur.

**Q**: What if my .fcgi process unexpectedly exits?   
**A**: STD-FPM will free the allocated resources and spawn a new one in the case of need.  

**Q**: Does this software suffers from the infamous ``mod_fcgid: can't apply process slot`` problem?  
**A**: It absolutely doesn't. This can be regarded as a motivation to use it as a drop-in replacement event with Apache.
