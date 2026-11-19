MODULE := engines/hook

MODULE_OBJS = \
	anim.o \
	console.o \
	hook.o \
	metaengine.o \
	player.o \
	resources.o \
	room.o \
	sprites.o \
	text.o \
	walk.o

# This module can be built as a plugin
ifeq ($(ENABLE_HOOK), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
