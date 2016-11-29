# Copyright 2011 The Android Open Source Project

#AUDIO_POLICY_TEST := true
#ENABLE_AUDIO_DUMP := true

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    AudioHardwareInterface.cpp \
    audio_hw_hal.cpp

LOCAL_MODULE := libaudiohw_legacy
LOCAL_SHARED_LIBRARIES := libmedia
LOCAL_STATIC_LIBRARIES := libmedia_helper
LOCAL_CFLAGS := -Wno-unused-parameter -Wno-gnu-designator
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/../include

include $(BUILD_STATIC_LIBRARY)


#ifeq ($(ENABLE_AUDIO_DUMP),true)
#  LOCAL_SRC_FILES += AudioDumpInterface.cpp
#  LOCAL_CFLAGS += -DENABLE_AUDIO_DUMP
#endif
#
#ifeq ($(strip $(BOARD_USES_GENERIC_AUDIO)),true)
#  LOCAL_CFLAGS += -D GENERIC_AUDIO
#endif

#ifeq ($(BOARD_HAVE_BLUETOOTH),true)
#  LOCAL_SRC_FILES += A2dpAudioInterface.cpp
#  LOCAL_SHARED_LIBRARIES += liba2dp
#  LOCAL_C_INCLUDES += $(call include-path-for, bluez)
#
#  LOCAL_CFLAGS += \
#      -DWITH_BLUETOOTH \
#endif
#
#include $(BUILD_SHARED_LIBRARY)

#    AudioHardwareGeneric.cpp \
#    AudioHardwareStub.cpp \
