%global debug_package %{nil}

Name:    lfscan
Version: 0.1.0
Release: 1%{?dist}
Summary: Temporary & orphaned file scanner for Linux

License: MIT
URL:     https://github.com/user/lfscan
Source0: %{name}-%{version}.tar.gz

BuildRequires: cmake >= 3.16
BuildRequires: gcc-c++
Requires:      rpm

%description
lfscan scans the filesystem for temporary files, cache files, orphaned
package files, broken symlinks, and stale logs. Safe by default — nothing
is deleted without explicit confirmation. Uses /proc/[pid]/fd to skip
files currently in use by running processes.

%prep
%setup -q

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

%files
%{_bindir}/lfscan

%doc README.md

%changelog
* Thu Jul 30 2026  <user@example.com> 0.1.0-1
- Initial release
