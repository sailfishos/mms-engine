TEMPLATE = lib
CONFIG += staticlib
CONFIG += link_pkgconfig
PKGCONFIG += libglibutil libwspcodec gmime-3.0 glib-2.0 libsoup-2.4
INCLUDEPATH += include
QMAKE_CFLAGS += -Wno-unused-parameter

include(mms-lib-config.pri)

ResizeImageMagick {
  CONFIG -= qt
  PKGCONFIG += ImageMagick
  DEFINES += MMS_RESIZE_IMAGEMAGICK
} else {
  ResizeQt {
    DEFINES += MMS_RESIZE_QT
  }
}

CONFIG(debug, debug|release) {
  DEFINES += DEBUG
  DESTDIR = $$_PRO_FILE_PWD_/build/debug
} else {
  DESTDIR = $$_PRO_FILE_PWD_/build/release
}

SOURCES += \
  src/mms_attachment.c \
  src/mms_attachment_image.c \
  src/mms_attachment_jpeg.c \
  src/mms_attachment_text.c \
  src/mms_attachment_qt.cpp \
  src/mms_codec.c \
  src/mms_connection.c \
  src/mms_connman.c \
  src/mms_error.c \
  src/mms_dispatcher.c \
  src/mms_file_util.c \
  src/mms_handler.c \
  src/mms_message.c \
  src/mms_lib_util.c \
  src/mms_settings.c \
  src/mms_task.c \
  src/mms_task_ack.c \
  src/mms_task_decode.c \
  src/mms_task_encode.c \
  src/mms_task_http.c \
  src/mms_task_notification.c \
  src/mms_task_notifyresp.c \
  src/mms_task_publish.c \
  src/mms_task_read.c \
  src/mms_task_retrieve.c \
  src/mms_task_send.c \
  src/mms_transfer_list.c \
  src/mms_util.c

HEADERS += \
  src/mms_attachment.h \
  src/mms_attachment_image.h \
  src/mms_codec.h \
  src/mms_error.h \
  src/mms_file_util.h \
  src/mms_task.h \
  src/mms_task_http.h \
  src/mms_util.h \

HEADERS += \
  include/mms_attachment_info.h \
  include/mms_connection.h \
  include/mms_connman.h \
  include/mms_dispatcher.h \
  include/mms_handler.h \
  include/mms_lib_log.h \
  include/mms_lib_types.h \
  include/mms_lib_util.h \
  include/mms_log.h \
  include/mms_message.h \
  include/mms_settings.h \
  include/mms_transfer_list.h
