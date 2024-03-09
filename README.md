STD-FPM
=============
A language-agnostic FastCGI process manager implementation

** Webserver configuration examples **

Nginx (*ngx_http_fastcgi_module*)
```nohighlight
location ~ \.fcgi$ {
   include fastcgi.conf;
   fastcgi_pass unix:/run/std-fpm/std-fpm.sock;
}
```

Apache (*mod_proxy_fcgi*)
```nohighlight
$ a2enmod proxy_fcgi
```

```nohighlight
<FilesMatch \.fcgi$>
     SetHandler "proxy:unix:/run/std-fpm/std-fpm.sock|fcgi://localhost/"
</FilesMatch>
```

** Build packages **
```nohighlight
$ fakeroot dpkg-buildpackage -nc    # .deb
$ rpmbuild -bb redhat/std-fpm.spec  # .rpm
```
