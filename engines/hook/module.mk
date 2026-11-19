MODULE := engines/hook

MODULE_OBJS = \
	console.o \
	hook.o \
	metaengine.o \
	resources.o \
	room.o

# This module can be built as a plugin
ifeq ($(ENABLE_HOOK), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
