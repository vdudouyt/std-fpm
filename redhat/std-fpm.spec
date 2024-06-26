Name:           std-fpm
Version:        0.3.1
Release:        1%{?dist}
Summary:        A language-agnostic FastCGI process manager

License:        0BSD
URL:            https://github.com/vdudouyt/%{name}
Source:         %{expand:%%(pwd)}

BuildRequires:  gcc, cmake, libevent-devel, glib2-devel

%description
A language-agnostic FastCGI process manager

%prep
find . -mindepth 1 -delete # Clean out old files
cp -af %{SOURCEURL0}/. .
sed -i s/www-data/nobody/ conf/std-fpm.conf

%build
%cmake

%install
%make_install

%files
%{_sbindir}/%{name}
/etc/std-fpm.conf
/etc/systemd/system/std-fpm.service

%changelog
* Wed May 29 2024 Valentin Dudouyt <valentin.dudouyt@gmail.com> - 0.3.1
- Shutdown idle processes
- Clean unused UNIX sockets
- Check run directories at startup

* Sat Mar 30 2024 Valentin Dudouyt <valentin.dudouyt@gmail.com> - 0.2-3
- v0.2-3

* Wed Mar 20 2024 Valentin Dudouyt <valentin.dudouyt@gmail.com> - 0.2-libevent
- v0.2-libevent

* Sat Mar 09 2024 Valentin Dudouyt <valentin.dudouyt@gmail.com> - 0.1-1
- First std-fpm package
