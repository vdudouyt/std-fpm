Name:           std-fpm
Version:        1.0.0
Release:        1%{?dist}
Summary:        A language-agnostic FastCGI process manager

License:        0BSD
URL:            https://github.com/vdudouyt/%{name}
Source:         %{expand:%%(pwd)}

BuildRequires:  rust, cargo

%description
A language-agnostic FastCGI process manager

%prep
find . -mindepth 1 -delete # Clean out old files
cp -af %{SOURCEURL0}/. .
sed -i s/www-data/nobody/ systemd/system/std-fpm.service

%build
cargo build --release

%install
mkdir -p %{buildroot}/usr/sbin
mkdir -p %{buildroot}/etc
mkdir -p %{buildroot}/usr/lib/systemd/system/
cp target/release/std-fpm %{buildroot}/%{_sbindir}
cp conf/std-fpm.conf %{buildroot}/etc/std-fpm.conf
cp systemd/system/std-fpm.service %{buildroot}/usr/lib/systemd/system/

%files
%{_sbindir}/%{name}
/etc/std-fpm.conf
/usr/lib/systemd/system/std-fpm.service

%changelog
* Wed Jul 20 2024 Valentin Dudouyt <valentin.dudouyt@gmail.com> - 1.0.0
- Rust rewrite

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
