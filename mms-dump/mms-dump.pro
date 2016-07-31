TEMPLATE = app
TARGET = mms-dump
CONFIG -= qt
QMAKE_CFLAGS += -Wno-unused-parameter

CONFIG += link_pkgconfig
PKGCONFIG += libwspcodec glib-2.0

SOURCES += mms-dump.c
