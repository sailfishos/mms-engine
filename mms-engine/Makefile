# -*- Mode: makefile-gmake -*-

.PHONY: all debug release clean cleaner
.PHONY: mms_lib_debug_lib mms_lib_release_lib
.PHONY: mms_connman_debug_lib mms_connman_release_lib
.PHONY: mms_handler_debug_lib mms_handler_release_lib
.PHONY: mms_transfers_debug_lib mms_transfers_release_lib
.PHONY: mms_settings_debug_lib mms_settings_release_lib

#
# Pull in mms-lib configuration
#

include ../mms-lib/Config.mak

#
# Required packages
#

GMIME_PACKAGE ?= gmime-3.0
PKGS = gio-unix-2.0 gio-2.0 libdbusaccess libglibutil libdbuslogserver-gio
LIB_PKGS = $(GMIME_PACKAGE) libwspcodec libgofono libsoup-2.4 dconf

SUBMAKE_OPTS += GMIME_PACKAGE=$(GMIME_PACKAGE)

ifdef SAILFISH
LIB_PKGS += libgofonoext
endif

LIB_PKGS += $(RESIZE_PKG) $(PKGS)

#
# Default target
#

all: debug release

#
# Sources
#

SRC = main.c mms_engine.c mms_log.c
GEN_SRC = org.nemomobile.MmsEngine.c

#
# Directories
#

SRC_DIR = .
BUILD_DIR = build
SPEC_DIR = .
GEN_DIR = $(BUILD_DIR)
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# mms-lib
#

MMS_LIB_LIB = libmms.a
MMS_LIB_DIR = ../mms-lib
MMS_LIB_BUILD_DIR = $(MMS_LIB_DIR)/build
MMS_LIB_DEBUG_LIB = $(MMS_LIB_BUILD_DIR)/debug/$(MMS_LIB_LIB)
MMS_LIB_RELEASE_LIB = $(MMS_LIB_BUILD_DIR)/release/$(MMS_LIB_LIB)

#
# mms-connman
#

ifdef SAILFISH
MMS_CONNMAN = nemo
else
MMS_CONNMAN = ofono
endif
MMS_CONNMAN_LIB = libmms-connman-$(MMS_CONNMAN).a
MMS_CONNMAN_DIR = ../mms-connman-$(MMS_CONNMAN)
MMS_CONNMAN_BUILD_DIR = $(MMS_CONNMAN_DIR)/build
MMS_CONNMAN_DEBUG_LIB = $(MMS_CONNMAN_BUILD_DIR)/debug/$(MMS_CONNMAN_LIB)
MMS_CONNMAN_RELEASE_LIB = $(MMS_CONNMAN_BUILD_DIR)/release/$(MMS_CONNMAN_LIB)

#
# mms-handler-dbus
#

MMS_HANDLER_LIB = libmms-handler-dbus.a
MMS_HANDLER_DIR = ../mms-handler-dbus
MMS_HANDLER_BUILD_DIR = $(MMS_HANDLER_DIR)/build
MMS_HANDLER_DEBUG_LIB = $(MMS_HANDLER_BUILD_DIR)/debug/$(MMS_HANDLER_LIB)
MMS_HANDLER_RELEASE_LIB = $(MMS_HANDLER_BUILD_DIR)/release/$(MMS_HANDLER_LIB)

#
# mms-transfer-list-dbus
#

MMS_TRANSFERS_LIB = libmms-transfer-list-dbus.a
MMS_TRANSFERS_DIR = ../mms-transfer-list-dbus
MMS_TRANSFERS_BUILD_DIR = $(MMS_TRANSFERS_DIR)/build
MMS_TRANSFERS_DEBUG_LIB = $(MMS_TRANSFERS_BUILD_DIR)/debug/$(MMS_TRANSFERS_LIB)
MMS_TRANSFERS_RELEASE_LIB = $(MMS_TRANSFERS_BUILD_DIR)/release/$(MMS_TRANSFERS_LIB)

#
# mms-settings-dconf
#

MMS_SETTINGS_LIB = libmms-settings-dconf.a
MMS_SETTINGS_DIR = ../mms-settings-dconf
MMS_SETTINGS_BUILD_DIR = $(MMS_SETTINGS_DIR)/build
MMS_SETTINGS_DEBUG_LIB = $(MMS_SETTINGS_BUILD_DIR)/debug/$(MMS_SETTINGS_LIB)
MMS_SETTINGS_RELEASE_LIB = $(MMS_SETTINGS_BUILD_DIR)/release/$(MMS_SETTINGS_LIB)

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
DEBUG_FLAGS = -g
RELEASE_FLAGS =
DEBUG_DEFS = -DDEBUG
RELEASE_DEFS =
WARN = -Wall
BASE_FLAGS = -fPIC $(CFLAGS)
FULL_CFLAGS = $(BASE_FLAGS) -I. -I$(GEN_DIR) -I$(MMS_LIB_DIR)/include \
 -I$(MMS_CONNMAN_DIR)/include -I$(MMS_HANDLER_DIR)/include \
 -I$(MMS_SETTINGS_DIR)/include -I$(MMS_TRANSFERS_DIR)/include \
 -MMD -MP $(shell pkg-config --cflags $(PKGS))

KEEP_SYMBOLS ?= 0
ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
SUBMAKE_OPTS += KEEP_SYMBOLS=1
endif

ifdef MMS_ENGINE_VERSION
FULL_CFLAGS += -DMMS_ENGINE_VERSION="$(MMS_ENGINE_VERSION)"
endif

ifdef SAILFISH
FULL_CFLAGS += -DSAILFISH
endif

DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2
DEBUG_LDFLAGS = $(LDFLAGS) $(DEBUG_FLAGS)
RELEASE_LDFLAGS = $(LDFLAGS) $(RELEASE_FLAGS)

LIBS = $(shell pkg-config --libs $(LIB_PKGS)) -lmagic -ljpeg $(RESIZE_LIBS)
DEBUG_LIBS = \
  $(MMS_CONNMAN_DEBUG_LIB) \
  $(MMS_HANDLER_DEBUG_LIB) \
  $(MMS_TRANSFERS_DEBUG_LIB) \
  $(MMS_SETTINGS_DEBUG_LIB) \
  $(MMS_LIB_DEBUG_LIB) \
  $(LIBS)
RELEASE_LIBS = \
  $(MMS_CONNMAN_RELEASE_LIB) \
  $(MMS_HANDLER_RELEASE_LIB) \
  $(MMS_TRANSFERS_RELEASE_LIB) \
  $(MMS_SETTINGS_RELEASE_LIB) \
  $(MMS_LIB_RELEASE_LIB) \
  $(LIBS)

#
# Files
#

DEBUG_OBJS = \
  $(GEN_SRC:%.c=$(DEBUG_BUILD_DIR)/%.o) \
  $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = \
  $(GEN_SRC:%.c=$(RELEASE_BUILD_DIR)/%.o) \
  $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)
GEN_FILES = $(GEN_SRC:%=$(GEN_DIR)/%)
.PRECIOUS: $(GEN_FILES)

#
# Dependencies
#

DEBUG_DEPS = \
  mms_lib_debug_lib \
  mms_connman_debug_lib \
  mms_handler_debug_lib \
  mms_transfers_debug_lib \
  mms_settings_debug_lib
RELEASE_DEPS = \
  mms_lib_release_lib \
  mms_connman_release_lib \
  mms_handler_release_lib \
  mms_transfers_release_lib \
  mms_settings_release_lib
DEBUG_EXE_DEPS = \
  $(MMS_LIB_DEBUG_LIB) \
  $(MMS_CONNMAN_DEBUG_LIB) \
  $(MMS_HANDLER_DEBUG_LIB) \
  $(MMS_TRANSFERS_DEBUG_LIB) \
  $(MMS_SETTINGS_DEBUG_LIB)
RELEASE_EXE_DEPS = \
  $(MMS_LIB_RELEASE_LIB) \
  $(MMS_CONNMAN_RELEASE_LIB) \
  $(MMS_HANDLER_RELEASE_LIB) \
  $(MMS_TRANSFERS_RELEASE_LIB) \
  $(MMS_SETTINGS_RELEASE_LIB)
DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(GEN_FILES): | $(GEN_DIR)
$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#

EXE = mms-engine
DEBUG_EXE = $(DEBUG_BUILD_DIR)/$(EXE)
RELEASE_EXE = $(RELEASE_BUILD_DIR)/$(EXE)

debug: $(DEBUG_DEPS) $(DEBUG_EXE)

release: $(RELEASE_DEPS) $(RELEASE_EXE)

clean:
	rm -fr $(BUILD_DIR) $(SRC_DIR)/*~

cleaner: clean
	make -C $(MMS_LIB_DIR) $(SUBMAKE_OPTS) clean
	make -C $(MMS_CONNMAN_DIR) $(SUBMAKE_OPTS) clean
	make -C $(MMS_HANDLER_DIR) $(SUBMAKE_OPTS) clean
	make -C $(MMS_TRANSFERS_DIR) $(SUBMAKE_OPTS) clean
	make -C $(MMS_SETTINGS_DIR) $(SUBMAKE_OPTS) clean

$(GEN_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

mms_lib_debug_lib:
	make -C $(MMS_LIB_DIR) $(SUBMAKE_OPTS) debug

mms_lib_release_lib:
	make -C $(MMS_LIB_DIR) $(SUBMAKE_OPTS) release

mms_connman_debug_lib:
	make -C $(MMS_CONNMAN_DIR) $(SUBMAKE_OPTS) debug

mms_connman_release_lib:
	make -C $(MMS_CONNMAN_DIR) $(SUBMAKE_OPTS) release

mms_handler_debug_lib:
	make -C $(MMS_HANDLER_DIR) $(SUBMAKE_OPTS) debug

mms_handler_release_lib:
	make -C $(MMS_HANDLER_DIR) $(SUBMAKE_OPTS) release

mms_transfers_debug_lib:
	make -C $(MMS_TRANSFERS_DIR) $(SUBMAKE_OPTS) debug

mms_transfers_release_lib:
	make -C $(MMS_TRANSFERS_DIR) $(SUBMAKE_OPTS) release

mms_settings_debug_lib:
	make -C $(MMS_SETTINGS_DIR) $(SUBMAKE_OPTS) debug

mms_settings_release_lib:
	make -C $(MMS_SETTINGS_DIR) $(SUBMAKE_OPTS) release

$(MMS_LIB_DEBUG_LIB): mms_lib_debug_lib

$(MMS_LIB_RELEASE_LIB): mms_lib_release_lib

$(MMS_CONNMAN_DEBUG_LIB): mms_connman_debug_lib

$(MMS_CONNMAN_RELEASE_LIB): mms_connman_release_lib

$(MMS_HANDLER_DEBUG_LIB): mms_handler_debug_lib

$(MMS_HANDLER_RELEASE_LIB): mms_handler_release_lib

$(MMS_TRANSFERS_DEBUG_LIB): mms_transfers_debug_lib

$(MMS_TRANSFERS_RELEASE_LIB): mms_transfers_release_lib

$(MMS_SETTINGS_DEBUG_LIB): mms_settings_debug_lib

$(MMS_SETTINGS_RELEASE_LIB): mms_settings_release_lib

$(GEN_DIR)/%.c: $(SPEC_DIR)/%.xml
	gdbus-codegen --generate-c-code $(@:%.c=%) $<

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(WARN) $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(WARN) $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_BUILD_DIR)/%.o : $(GEN_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(GEN_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_EXE): $(DEBUG_EXE_DEPS) $(DEBUG_OBJS)
	$(LD) $(DEBUG_LDFLAGS) $(DEBUG_OBJS) $(DEBUG_LIBS) -o $@

$(RELEASE_EXE): $(RELEASE_EXE_DEPS) $(RELEASE_OBJS)
	$(LD) $(RELEASE_LDFLAGS) $(RELEASE_OBJS) $(RELEASE_LIBS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif
