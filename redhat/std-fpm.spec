Name:           std-fpm
Version:        0.1
Release:        1%{?dist}
Summary:        A language-agnostic FastCGI process manager

License:        0BSD
URL:            https://github.com/vdudouyt/%{name}
Source:         %{expand:%%(pwd)}

BuildRequires:  gcc, cmake, glib2-devel

%description
A language-agnostic FastCGI process manager

%prep
find . -mindepth 1 -delete # Clean out old files
cp -af %{SOURCEURL0}/. .

%build
%cmake

%install
%make_install

%files
%{_sbindir}/%{name}
/etc/std-fpm.conf
/etc/systemd/system/std-fpm.service
%dir /run/std-fpm/
%attr(0777,apache,apache) %dir /run/std-fpm/

%changelog
* Sat Mar 09 2024 Valentin Dudouyt <valentin.dudouyt@gmail.com> - 0.1-1
- First std-fpm package
