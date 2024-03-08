STD-FPM
=============
A language-agnostic FastCGI process manager implementation

** Webserver configuration examples **

Nginx
```nohighlight
location ~ \.fcgi$ {
   include snippets/fastcgi-php.conf;
   fastcgi_pass unix:/run/std-fpm/std-fpm.sock;
}
```

Apache
```nohighlight
$ a2enmod proxy_fcgi

<FilesMatch \.fcgi$>
     SetHandler "proxy:unix:/run/std-fpm/std-fpm.sock|fcgi://localhost/"
</FilesMatch>
```
