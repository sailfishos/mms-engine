TEMPLATE = lib
CONFIG += staticlib
CONFIG -= qt
CONFIG += link_pkgconfig
PKGCONFIG += libgofono libglibutil glib-2.0 gio-2.0 gio-unix-2.0
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
  src/mms_ofono_connection.c \
  src/mms_ofono_connman.c

HEADERS += \
  src/mms_ofono_connection.h \
  src/mms_ofono_context.h

HEADERS += \
  include/mms_ofono_connman.h \
  include/mms_ofono_log.h
