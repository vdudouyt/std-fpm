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
## Comparison with similar software
|                            | STD-FPM | PHP-FPM | mod_fcgid | spawn-fcgi |
| -------------------------- | ------- | ------- | --------- | -----------|
| Languages                  | Any     | PHP     | Any       | Any        |
| Web Servers                | Any     | Any     | Apache    | Any        |
| Process management         | Y       | Y       | Y         | N          |
| SCRIPT_FILENAME routing    | Y       | Y       | Y         | N          |
| Unexpected exits tolerance | Y       | N/A (2) | N (1)     | ?          |
| Slow I/O tolerance         | Y       | ?       | N (1)     | N          |

*(1): Locks Apache if happens requently*  
*(2): Not applicable here*  
*Notice: we have also tried somewhat named [Puskach](http://falstart.com/puskach/docs/nginx_perl.html) which appears to be an early attempt to build up something like STD-FPM. However, we failed to gather much information about it as it somehow caused our developer's workstation to freeze.*
