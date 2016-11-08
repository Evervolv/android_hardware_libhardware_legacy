# Copyright 2006 The Android Open Source Project

# Setting LOCAL_PATH will mess up all-subdir-makefiles, so do it beforehand.
legacy_modules := power uevent

SAVE_MAKEFILES := $(call all-named-subdir-makefiles,$(legacy_modules))
LEGACY_AUDIO_MAKEFILES := $(call all-named-subdir-makefiles,audio)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := libbase libcutils liblog libmedia
LOCAL_EXPORT_SHARED_LIBRARY_HEADERS := libmedia

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include

LOCAL_CFLAGS  += -DQEMU_HARDWARE -Wno-unused-parameter -Wno-gnu-designator
QEMU_HARDWARE := true

LOCAL_SHARED_LIBRARIES += libdl

include $(SAVE_MAKEFILES)

# TODO: Remove this line b/29915755
ifndef BRILLO
LOCAL_WHOLE_STATIC_LIBRARIES := libwifi-hal-common
endif

LOCAL_MODULE:= libhardware_legacy

include $(BUILD_SHARED_LIBRARY)

# legacy_audio builds it's own set of libraries that aren't linked into
# hardware_legacy
include $(LEGACY_AUDIO_MAKEFILES)
