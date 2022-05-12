Name:		klvanc-tools
Version:	1.2.0
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
/usr/local/bin/klvanc_smpte2038
#/usr/local/share/man/man8/tstools_nic_monitor.8

%changelog
* Thu May 12 2021 Steven Toth <steven.toth@ltnglobal.com> 
- v1.3.0
  klvanc_capture: Don't initialize nielsen API unless requested
  klvanc_capture: Add -B feature to measure a/v offsets from white flash to pulse tone
  klvanc_capture: Audio silence detection, reporting improvements.
  klvanc_capture: Improve SDI startup frame intervals to avoid potential skewing of numbers.
  klvanc_transmitter: Grow the output buffer so we can accomodate exceeding long messages.
  klvanc_smpte2038: New tool for debugging 2038 messages.

* Fri Jan 29 2021 Steven Toth <steven.toth@ltnglobal.com> 
- v1.2.0
  Added some hi-resolution frame timing measurement capability (-H)
  Added ability to measure linear trend, drift of received frames vs expected frames (-H)
  Merged the 'detect audio loss' feature from dev tree

* Wed Oct 21 2020 Steven Toth <steven.toth@ltnglobal.com> 
- v1.1.0
  Added Nielsen detection support to klvanc_capture

* Fri Sep 18 2020 Steven Toth <steven.toth@ltnglobal.com> 
- v1.0.0
  Initial RPM release

