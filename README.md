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
**A**: Absolutely yes, this won't cause a worker thread block to occur.   

**Q**: What if my .fcgi process unexpectedly exits?   
**A**: STD-FPM frees the allocated resources and spawns a new one in the case of need.  

**Q**: Does this software suffers from the infamous ``mod_fcgid: can't apply process slot`` problem?  
**A**: It absolutely doesn't. This can be regarded as a motivation to use it as mod_fcgid replacement even with Apache.

## Comparison with the similar software
**PHP-FPM** is a great piece of software on which STD-FPM is ideologically based. But, just as it's name suggests, it's designed to only run the PHP scripts. By contrary, STD-FPM is designed to run everything that supports FastCGI startup protocol (just as mod_fcgid or spawn-fcgi).  

**Mod_fcgid** is designed as a part of Apache webserver. Rather that, STD-FPM can run with everything that runs with PHP-FPM. In addition to that. mod_fcgid is infamously known for rapid performance degrade under the circumstances when .fcgi handlers are performing some long-running I/O operations, or unexpectedly exiting. By contrast, STD-FPM is designed to completely tolerate that.  

**Spawn-fcgi** spawns a preconfigured number of predefined .fcgi handler instances, then binds to a listener port. STD-FPM gets a SCRIPT_FILENAME from downstream HTTP server just as PHP-FPM does, then dynamically controls a population of automatically spawned .fcgi handlers just as mod_fcgi does.  

**Puskach** ([link](http://falstart.com/puskach/docs/nginx_perl.html)) was an early attempt to built up something like STD-FPM from the different developers. As per my experience, it had some real software maturity problems. Also, it was single-threaded and using an O(n) descriptor polling method, while STD-FPM is multithreaded and using **epoll** through libevent2.
