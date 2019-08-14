LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES := $(LOCAL_PATH)/common $(LOCAL_PATH)/boost_1_70_0
LOCAL_CFLAGS := -fexceptions -frtti -Wno-error=non-virtual-dtor

LOCAL_SRC_FILES := \
	main.cpp

LOCAL_SHARED_LIBRARIES := \
	libcutils liblog

LOCAL_STATIC_LIBRARIES := \
	libselinux

LOCAL_MODULE := device_client
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := location_finder
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := location_finder
include $(BUILD_PREBUILT)
