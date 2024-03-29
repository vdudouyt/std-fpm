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
* **PHP-FPM**: PHP-FPM is a great and mature piece of software on which STD-FPM is ideologically based. The only problem is that, just as it's name suggests, it's designed to only run the PHP scripts. By contrary, STD-FPM is designed to run everything that supports FastCGI startup protocol (just as **mod_fcgid** or **spawn-fcgi**)
* **Apache mod_fcgid**: mod_fcgid is designed to only run with Apache webserver. STD-FPM can run with everything that can run with PHP-FPM (including but not limited to Nginx). Another concern is that mod_fcgid is infamously known for rapid performance degrade in the scenario where .fcgi handlers are performing some long-running I/O operations or unexpectedly exiting. By contrast, STD-FPM is designed to tolerate that.
