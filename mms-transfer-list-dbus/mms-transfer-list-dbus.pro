TEMPLATE = lib
CONFIG += staticlib
CONFIG -= qt
CONFIG += link_pkgconfig
PKGCONFIG += libglibutil glib-2.0 gio-2.0 gio-unix-2.0
DBUS_SPEC_DIR = $$_PRO_FILE_PWD_/spec
INCLUDEPATH += . include
INCLUDEPATH += ../mms-lib/include
QMAKE_CFLAGS += -Wno-unused-parameter
BUILD_DIR = $$_PRO_FILE_PWD_/build

CONFIG(debug, debug|release) {
  DEFINES += DEBUG
  DESTDIR = $$BUILD_DIR/debug
} else {
  DESTDIR = $$BUILD_DIR/release
}

SOURCES += \
  src/mms_transfer_dbus.c \
  src/mms_transfer_list_dbus.c

HEADERS += \
    src/mms_transfer_dbus.h \
    src/mms_transfer_list_dbus_log.h \
    include/mms_transfer_list_dbus.h

SPEC = $$DBUS_SPEC_DIR/org.nemomobile.MmsEngine.TransferList.xml
OTHER_FILES += $$SPEC

# org.nemomobile.MmsEngine.Transfer
STUB_GENERATE = gdbus-codegen --generate-c-code \
  org.nemomobile.MmsEngine.TransferList $$SPEC
STUB_H = org.nemomobile.MmsEngine.TransferList.h
stub_h.input = SPEC
stub_h.output = $$STUB_H
stub_h.commands = $$STUB_GENERATE
stub_h.CONFIG = no_link
QMAKE_EXTRA_COMPILERS += stub_h

STUB_C = org.nemomobile.MmsEngine.TransferList.c
stub_c.input = SPEC
stub_c.output = $$STUB_C
stub_c.commands = $$STUB_GENERATE
stub_c.CONFIG = no_link
QMAKE_EXTRA_COMPILERS += stub_c
GENERATED_SOURCES += $$STUB_C
