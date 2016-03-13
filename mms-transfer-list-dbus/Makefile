# -*- Mode: makefile-gmake -*-

.PHONY: clean all debug release

# Required packages
PKGS = libglibutil glib-2.0 gio-2.0 gio-unix-2.0

#
# Default target
#

all: debug release

#
# Sources
#

SRC = mms_transfer_dbus.c mms_transfer_list_dbus.c
GEN_SRC = org.nemomobile.MmsEngine.TransferList.c

#
# Directories
#

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
GEN_DIR = $(BUILD_DIR)
SPEC_DIR = spec
MMS_LIB_INCLUDE = ../mms-lib/include
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
ARFLAGS = rc
DEBUG_FLAGS = -g
RELEASE_FLAGS = -O2
WARNINGS = -Wall
BASE_FLAGS = -fPIC $(CFLAGS)
INCLUDES = -I$(SRC_DIR) -I$(INCLUDE_DIR) -I$(MMS_LIB_INCLUDE) -I$(GEN_DIR) -I.
FULL_CFLAGS = $(BASE_FLAGS) $(WARNINGS) $(INCLUDES) -fPIC -MMD \
  $(shell pkg-config --cflags $(PKGS))

ifndef KEEP_SYMBOLS
KEEP_SYMBOLS = 0
endif

ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
endif

DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2

#
# Files
#

.PRECIOUS: $(GEN_SRC:%=$(GEN_DIR)/%)

DEBUG_OBJS = \
  $(GEN_SRC:%.c=$(DEBUG_BUILD_DIR)/%.o) \
  $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = \
  $(GEN_SRC:%.c=$(RELEASE_BUILD_DIR)/%.o) \
  $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

#
# Dependencies
#

DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

#
# Rules
#
LIB = libmms-transfer-list-dbus.a
DEBUG_LIB = $(DEBUG_BUILD_DIR)/$(LIB)
RELEASE_LIB = $(RELEASE_BUILD_DIR)/$(LIB)

debug: $(DEBUG_LIB)

release: $(RELEASE_LIB) 

clean:
	rm -fr $(BUILD_DIR) *~ $(SRC_DIR)/*~ $(INCLUDE_DIR)/*~

$(GEN_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(GEN_DIR)/%.c: $(SPEC_DIR)/%.xml
	gdbus-codegen --generate-c-code $(@:%.c=%) $<

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_BUILD_DIR)/%.o : $(GEN_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(GEN_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_LIB): $(DEBUG_BUILD_DIR) $(DEBUG_OBJS)
	$(AR) $(ARFLAGS) $@ $(DEBUG_OBJS)

$(RELEASE_LIB): $(RELEASE_BUILD_DIR) $(RELEASE_OBJS)
	$(AR) $(ARFLAGS) $@ $(RELEASE_OBJS)
