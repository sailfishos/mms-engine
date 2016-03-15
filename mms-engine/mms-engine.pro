TEMPLATE = app
CONFIG += link_pkgconfig
PKGCONFIG += gmime-2.6 gio-unix-2.0 gio-2.0 glib-2.0 libsoup-2.4 dconf
PKGCONFIG += libwspcodec libgofono libglibutil
QMAKE_CFLAGS += -Wno-unused-parameter

include(../mms-lib/mms-lib-config.pri)

ResizeImageMagick {
  CONFIG -= qt
  PKGCONFIG += ImageMagick
}

ConnManNemo {
  PKGCONFIG += libgofonoext
  DEFINES += SAILFISH
  MMS_CONNMAN = mms-connman-nemo
} else {
  MMS_CONNMAN = mms-connman-ofono
}

DBUS_INTERFACE_DIR = $$_PRO_FILE_PWD_
MMS_LIB_DIR = $$_PRO_FILE_PWD_/../mms-lib
MMS_HANDLER_DIR = $$_PRO_FILE_PWD_/../mms-handler-dbus
MMS_SETTINGS_DIR = $$_PRO_FILE_PWD_/../mms-settings-dconf
MMS_TRANSFER_LIST_DIR = $$_PRO_FILE_PWD_/../mms-transfer-list-dbus
MMS_CONNMAN_DIR = $$_PRO_FILE_PWD_/../$$MMS_CONNMAN

INCLUDEPATH += $$MMS_LIB_DIR/include
INCLUDEPATH += $$MMS_HANDLER_DIR/include
INCLUDEPATH += $$MMS_SETTINGS_DIR/include
INCLUDEPATH += $$MMS_CONNMAN_DIR/include
INCLUDEPATH += $$MMS_TRANSFER_LIST_DIR/include

SOURCES += \
  main.c \
  mms_engine.c
HEADERS += \
  mms_engine.h \
  mms_version.h
OTHER_FILES += \
  org.nemomobile.MmsEngine.push.conf \
  org.nemomobile.MmsEngine.dbus.conf \
  org.nemomobile.MmsEngine.service \
  org.nemomobile.MmsEngine.xml

CONFIG(debug, debug|release) {
    DEFINES += DEBUG
    DESTDIR = $$_PRO_FILE_PWD_/build/debug
    LIBS += $$MMS_CONNMAN_DIR/build/debug/lib$${MMS_CONNMAN}.a
    LIBS += $$MMS_HANDLER_DIR/build/debug/libmms-handler-dbus.a
    LIBS += $$MMS_TRANSFER_LIST_DIR/build/debug/libmms-transfer-list-dbus.a
    LIBS += $$MMS_LIB_DIR/build/debug/libmms-lib.a
    LIBS += $$MMS_SETTINGS_DIR/build/debug/libmms-settings-dconf.a
} else {
    DESTDIR = $$_PRO_FILE_PWD_/build/release
    LIBS += $$MMS_CONNMAN_DIR/build/release/lib$${MMS_CONNMAN}.a
    LIBS += $$MMS_HANDLER_DIR/build/release/libmms-handler-dbus.a
    LIBS += $$MMS_TRANSFER_LIST_DIR/build/release/libmms-transfer-list-dbus.a
    LIBS += $$MMS_LIB_DIR/build/release/libmms-lib.a
    LIBS += $$MMS_SETTINGS_DIR/build/release/libmms-settings-dconf.a
}

LIBS += -lmagic -ljpeg

MMS_ENGINE_DBUS_XML = $$DBUS_INTERFACE_DIR/org.nemomobile.MmsEngine.xml
MMS_ENGINE_DBUS_H = org.nemomobile.MmsEngine.h
org_nemomobile_mmsengine_h.input = MMS_ENGINE_DBUS_XML
org_nemomobile_mmsengine_h.output = $$MMS_ENGINE_DBUS_H
org_nemomobile_mmsengine_h.commands = gdbus-codegen --generate-c-code \
  org.nemomobile.MmsEngine $$MMS_ENGINE_DBUS_XML
org_nemomobile_mmsengine_h.CONFIG = no_link
QMAKE_EXTRA_COMPILERS += org_nemomobile_mmsengine_h

MMS_ENGINE_DBUS_C = org.nemomobile.MmsEngine.c
org_nemomobile_mmsengine_c.input = MMS_ENGINE_DBUS_XML
org_nemomobile_mmsengine_c.output = $$MMS_ENGINE_DBUS_C
org_nemomobile_mmsengine_c.commands = gdbus-codegen --generate-c-code \
  org.nemomobile.MmsEngine $$MMS_ENGINE_DBUS_XML
org_nemomobile_mmsengine_c.CONFIG = no_link
QMAKE_EXTRA_COMPILERS += org_nemomobile_mmsengine_c
GENERATED_SOURCES += $$MMS_ENGINE_DBUS_C
