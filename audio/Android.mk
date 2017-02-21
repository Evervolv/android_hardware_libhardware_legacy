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


