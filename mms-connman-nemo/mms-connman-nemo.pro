TEMPLATE = lib
CONFIG += staticlib
CONFIG -= qt
CONFIG += link_pkgconfig
PKGCONFIG += libgofono libgofonoext libglibutil glib-2.0 gio-2.0 gio-unix-2.0
DBUS_SPEC_DIR = $$_PRO_FILE_PWD_/spec
INCLUDEPATH += include
INCLUDEPATH += ../mms-lib/include
QMAKE_CFLAGS += -Wno-unused-parameter

CONFIG(debug, debug|release) {
  DEFINES += DEBUG
  DESTDIR = $$_PRO_FILE_PWD_/build/debug
} else {
  DESTDIR = $$_PRO_FILE_PWD_/build/release
}

SOURCES += \
  src/mms_connection_nemo.c \
  src/mms_connman_nemo.c

HEADERS += \
  src/mms_connection_nemo.h

HEADERS += \
  include/mms_connman_nemo.h \
  include/mms_connman_nemo_log.h
