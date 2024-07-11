Name:     mms-engine
Summary:  MMS engine
Version:  1.0.81
Release:  1
License:  GPLv2
URL:      https://github.com/sailfishos/mms-engine
Source0:  %{name}-%{version}.tar.bz2

%define glib_version 2.32
%define libsoup_version 2.38
%define libwspcodec_version 2.2.3
%define libgofono_version 2.0.0
%define libgofonoext_version 1.0.4
%define libglibutil_version 1.0.47
%define libdbusaccess_version 1.0.10
%define libdbuslog_version 1.0.19

Requires: dbus
Requires: ofono
Requires: systemd
Requires: mapplauncherd
Requires: glib2 >= %{glib_version}
Requires: libsoup >= %{libsoup_version}
Requires: libwspcodec >= %{libwspcodec_version}
Requires: libgofono >= %{libgofono_version}
Requires: libgofonoext >= %{libgofonoext_version}
Requires: libglibutil >= %{libglibutil_version}
Requires: libdbusaccess >= %{libdbusaccess_version}
Requires: libdbuslogserver-gio >= %{libdbuslog_version}
Requires(post): glib2
Requires(postun): glib2

BuildRequires: file-devel
BuildRequires: libjpeg-turbo-devel
BuildRequires: gmime-devel
BuildRequires: pkgconfig
BuildRequires: pkgconfig(systemd)
BuildRequires: pkgconfig(dconf)
BuildRequires: pkgconfig(libpng)
BuildRequires: pkgconfig(libexif)
BuildRequires: pkgconfig(glib-2.0) >= %{glib_version}
BuildRequires: pkgconfig(libsoup-2.4) >= %{libsoup_version}
BuildRequires: pkgconfig(libwspcodec) >= %{libwspcodec_version}
BuildRequires: pkgconfig(libgofono) >= %{libgofono_version}
BuildRequires: pkgconfig(libgofonoext) >= %{libgofonoext_version}
BuildRequires: pkgconfig(libglibutil) >= %{libglibutil_version}
BuildRequires: pkgconfig(libdbusaccess) >= %{libdbusaccess_version}
BuildRequires: pkgconfig(libdbuslogserver-gio) >= %{libdbuslog_version}
#BuildRequires: pkgconfig(ImageMagick)
BuildRequires: pkgconfig(Qt5Gui)

# license macro requires rpm >= 4.11
BuildRequires: pkgconfig(rpm)
%define license_support %(pkg-config --exists 'rpm >= 4.11'; echo $?)

# Choose gmime version
%define gmime_v2 %(rpm -q gmime | grep -q gmime-2; echo $?)
%if %{gmime_v2} == 0
%define gmime_package gmime-2.6
%else
%define gmime_package gmime-3.0
%endif

%define src mms-engine
%define exe mms-engine
%define schema org.nemomobile.mms.sim
%define dbusname org.nemomobile.MmsEngine
%define userservice mms-engine
%define privilegesfile mms-engine.privileges
%define privilegesdir %{_datadir}/mapplauncherd/privileges.d
%define dbusconfig %{_datadir}/dbus-1/system-services
%define dbuspolicy %{_sysconfdir}/dbus-1/system.d
%define glibschemas  %{_datadir}/glib-2.0/schemas

# Activation method:
%define pushconfig %{_sysconfdir}/ofono/push_forwarder.d

%description
MMS engine handles encoding, decoding, uploading and downloading
of MMS messages.

%package tools
Summary:    MMS tools

%description tools
MMS command line utilities

%prep
%setup -q -n %{name}-%{version}

%build
make -C %{src} \
  SAILFISH=1 \
  KEEP_SYMBOLS=1 \
  MMS_ENGINE_VERSION="%{version}" \
  GMIME_PACKAGE="%{gmime_package}" \
  release
make -C mms-dump KEEP_SYMBOLS=1 release
make -C mms-send KEEP_SYMBOLS=1 release

%install
rm -rf %{buildroot}
install -d %{buildroot}%{_bindir}
install -d %{buildroot}%{_sbindir}
install -d %{buildroot}%{_unitdir}
install -d %{buildroot}%{_userunitdir}
install -d %{buildroot}%{dbusconfig}
install -d %{buildroot}%{dbuspolicy}
install -d %{buildroot}%{pushconfig}
install -d %{buildroot}%{glibschemas}
install -d %{buildroot}%{privilegesdir}
install -m 755 %{src}/build/release/%{exe} %{buildroot}%{_sbindir}/
install -m 644 %{src}/%{userservice}.service %{buildroot}%{_userunitdir}/
install -m 644 %{src}/%{dbusname}.service %{buildroot}%{dbusconfig}/
install -m 644 %{src}/%{dbusname}.dbus.conf %{buildroot}%{dbuspolicy}/%{dbusname}.conf
install -m 644 %{src}/%{dbusname}.push.conf %{buildroot}%{pushconfig}/%{dbusname}.conf
install -m 644 %{src}/%{privilegesfile} %{buildroot}%{privilegesdir}
install -m 644 mms-settings-dconf/spec/%{schema}.gschema.xml %{buildroot}%{glibschemas}/
install -m 755 mms-dump/build/release/mms-dump %{buildroot}%{_bindir}
install -m 755 mms-send/build/release/mms-send %{buildroot}%{_bindir}

%post
glib-compile-schemas %{glibschemas}

%postun
glib-compile-schemas %{glibschemas}

%ifarch %{ix86} x86_64
%check
make -C mms-lib/test GMIME_PACKAGE="%{gmime_package}" test
%endif

%files
%defattr(-,root,root,-)
%config %{glibschemas}/%{schema}.gschema.xml
%config %{dbuspolicy}/%{dbusname}.conf
%config %{pushconfig}/%{dbusname}.conf
%{privilegesdir}/%{privilegesfile}
%{dbusconfig}/%{dbusname}.service
%{_userunitdir}/%{userservice}.service
%{_sbindir}/%{exe}
%if %{license_support} == 0
%license LICENSE.GPL2
%endif

%files tools
%defattr(-,root,root,-)
%{_bindir}/mms-dump
%{_bindir}/mms-send
