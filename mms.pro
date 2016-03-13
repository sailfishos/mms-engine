TEMPLATE = subdirs
CONFIG += ordered
SUBDIRS += \
  mms-lib \
  mms-connman-nemo \
  mms-connman-ofono \
  mms-handler-dbus \
  mms-settings-dconf \
  mms-transfer-list-dbus \
  mms-engine \
  mms-dump \
  mms-send
OTHER_FILES += \
  rpm/mms-engine.spec \
  README
