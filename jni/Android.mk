LOCAL_PATH := $(call my-dir)
PKG_CONFIG ?= pkg-config

#INCLUDES = jni

#L_CFLAGS := -mabi=aapcs-linux
#L_CFLAGS += -fpermissive
#L_CFLAGS += -fexceptions
#L_CFLAGS += -fexceptions

include $(CLEAR_VARS)
LOCAL_MODULE := suinput
LOCAL_SRC_FILES := suinput.c
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := platform
LOCAL_STATIC_LIBRARIES := libsuinput
LOCAL_SRC_FILES := android.c
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := micro
LOCAL_STATIC_LIBRARIES := libplatform
LOCAL_SRC_FILES := uSynergy.c
include $(BUILD_STATIC_LIBRARY)


include $(CLEAR_VARS)
LOCAL_MODULE := usynergy
LOCAL_STATIC_LIBRARIES := libmicro
LOCAL_SRC_FILES := uSynergyUnix.c
include $(BUILD_EXECUTABLE)
