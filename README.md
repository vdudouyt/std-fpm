STD-FPM
=============
A high-performance language-agnostic FastCGI process manager

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
