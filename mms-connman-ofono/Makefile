# -*- Mode: makefile-gmake -*-

.PHONY: clean all debug release

# Required packages
PKGS = libgofono libglibutil glib-2.0 gio-2.0 gio-unix-2.0

#
# Default target
#

all: debug release

#
# Sources
#

SRC = \
  mms_connection_ofono.c \
  mms_connman_ofono.c

#
# Directories
#

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
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
DEBUG_DEFS = -DDEBUG
RELEASE_DEFS =
WARNINGS = -Wall
INCLUDES = -I$(SRC_DIR) -I$(INCLUDE_DIR) -I$(MMS_LIB_INCLUDE) -I.
CFLAGS += $(WARNINGS) $(INCLUDES) $(shell pkg-config --cflags $(PKGS)) \
  -fPIC -MMD

ifndef KEEP_SYMBOLS
KEEP_SYMBOLS = 0
endif

ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
endif

DEBUG_CFLAGS = $(DEBUG_FLAGS) $(DEBUG_DEFS) $(CFLAGS)
RELEASE_CFLAGS = $(RELEASE_FLAGS) $(RELEASE_DEFS) $(CFLAGS)

#
# Files
#

DEBUG_OBJS = $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

#
# Dependencies
#

DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(DEBUG_OBJS): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS): | $(RELEASE_BUILD_DIR)

#
# Rules
#
LIB = libmms-connman-ofono.a
DEBUG_LIB = $(DEBUG_BUILD_DIR)/$(LIB)
RELEASE_LIB = $(RELEASE_BUILD_DIR)/$(LIB)

debug: $(DEBUG_LIB)

release: $(RELEASE_LIB) 

clean:
	rm -fr $(BUILD_DIR) *~ $(SRC_DIR)/*~ $(INCLUDE_DIR)/*~

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(WARN) $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(WARN) $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_LIB): $(DEBUG_OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(RELEASE_LIB): $(RELEASE_OBJS)
	$(AR) $(ARFLAGS) $@ $^
