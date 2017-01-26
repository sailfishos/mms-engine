TEMPLATE = subdirs

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

# Dependencies between the subprojects

mms-lib.target = mms-lib-target

mms-connman-nemo.target = mms-connman-nemo-target
mms-connman-nemo.depends = mms-lib-target

mms-connman-ofono.target = mms-connman-ofono-target
mms-connman-ofono.depends = mms-lib-target

mms-handler-dbus.target = mms-handler-dbus-target
mms-handler-dbus.depends = mms-lib-target

mms-settings-dconf.target = mms-settings-dconf-target
mms-settings-dconf.depends = mms-lib-target

mms-transfer-list-dbus.target = mms-transfer-list-dbus-target
mms-transfer-list-dbus.depends = mms-lib-target

mms-engine.depends = \
    mms-lib-target \
    mms-connman-nemo-target \
    mms-connman-ofono-target \
    mms-handler-dbus-target \
    mms-settings-dconf-target \
    mms-transfer-list-dbus-target
