Name:		klvanc-tools
Version:	1.0.0
Release:	1
Summary:	SDI Capture and Analysis tools

License:	GPLv2+
URL:		www.ltnglobal.com

#BuildRequires:	
BuildRequires:	zlib-devel
BuildRequires:	ncurses-devel

Requires:	zlib
Requires:	ncurses

%description
A tool to capture, inspect or monitor Blackmagic SDI signals.

%files
/usr/local/bin/klvanc_capture
/usr/local/bin/klvanc_transmitter
#/usr/local/share/man/man8/tstools_nic_monitor.8

%changelog
* Wed Oct 21 2020 Steven Toth <steven.toth@ltnglobal.com> 
- v1.1.0
  Added Nielsen detection support to klvanc_capture

* Fri Sep 18 2020 Steven Toth <steven.toth@ltnglobal.com> 
- v1.0.0
  Initial RPM release

