TEMPLATE = subdirs
CONFIG += ordered
SUBDIRS += \
  mms-lib \
  mms-connman-ofono \
  mms-handler-dbus \
  mms-settings-dconf \
  mms-engine \
  mms-dump \
  mms-send
OTHER_FILES += \
  rpm/mms-engine.spec \
  README
