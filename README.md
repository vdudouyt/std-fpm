** Config examples **

Nginx
```nohighlight
location ~ \.fcgi$ {
   include snippets/fastcgi-php.conf;
   fastcgi_pass unix:/tmp/std-fpm.sock;
}
```

Apache
```nohighlight
$ a2enmod proxy_fcgi

<FilesMatch \.fcgi$>
     SetHandler "proxy:unix:/run/std-fpm.sock|fcgi://localhost/"
</FilesMatch>
```
