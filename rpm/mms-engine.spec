Name:     mms-engine
Summary:  MMS engine
Version:  1.0.74
Release:  1
License:  GPLv2
URL:      https://git.sailfishos.org/mer-core/mms-engine
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

BuildRequires: systemd
BuildRequires: file-devel
BuildRequires: libjpeg-turbo-devel
BuildRequires: pkgconfig(dconf)
BuildRequires: pkgconfig(libpng)
BuildRequires: pkgconfig(libexif)
BuildRequires: pkgconfig(gmime-2.6)
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

%define src mms-engine
%define exe mms-engine
%define schema org.nemomobile.mms.sim
%define dbusname org.nemomobile.MmsEngine
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
make -C %{src} KEEP_SYMBOLS=1 MMS_ENGINE_VERSION="%{version}" SAILFISH=1 release
make -C mms-dump KEEP_SYMBOLS=1 release
make -C mms-send KEEP_SYMBOLS=1 release

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_sbindir}
mkdir -p %{buildroot}%{_unitdir}
mkdir -p %{buildroot}%{dbusconfig}
mkdir -p %{buildroot}%{dbuspolicy}
mkdir -p %{buildroot}%{pushconfig}
mkdir -p %{buildroot}%{glibschemas}
mkdir -p %{buildroot}%{_prefix}/bin/
cp %{src}/build/release/%{exe} %{buildroot}%{_sbindir}/
cp %{src}/dbus-%{dbusname}.service %{buildroot}%{_unitdir}/
cp %{src}/%{dbusname}.service %{buildroot}%{dbusconfig}/
cp %{src}/%{dbusname}.dbus.conf %{buildroot}%{dbuspolicy}/%{dbusname}.conf
cp %{src}/%{dbusname}.push.conf %{buildroot}%{pushconfig}/%{dbusname}.conf
cp mms-settings-dconf/spec/%{schema}.gschema.xml %{buildroot}%{glibschemas}/
cp mms-dump/build/release/mms-dump %{buildroot}%{_prefix}/bin/
cp mms-send/build/release/mms-send %{buildroot}%{_prefix}/bin/

%post
glib-compile-schemas %{glibschemas}

%postun
glib-compile-schemas %{glibschemas}

%check
make -C mms-lib/test test

%files
%defattr(-,root,root,-)
%config %{glibschemas}/%{schema}.gschema.xml
%config %{dbuspolicy}/%{dbusname}.conf
%config %{pushconfig}/%{dbusname}.conf
%{dbusconfig}/%{dbusname}.service
%{_unitdir}/dbus-%{dbusname}.service
%{_sbindir}/%{exe}

%files tools
%defattr(-,root,root,-)
%{_prefix}/bin/mms-dump
%{_prefix}/bin/mms-send
