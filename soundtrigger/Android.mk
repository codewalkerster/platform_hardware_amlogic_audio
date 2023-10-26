# The default sound_trigger HAL module, which is a stub, that is loaded if no other
# device specific modules are present. The exact load order can be seen in
# libhardware/hardware.c
#
# The format of the name is sound_trigger.<type>.<hardware/etc>.so where the only
# required type is 'primary'.
ifeq ($(BOARD_ENABLE_DSP_FFV), true)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-BSD
LOCAL_LICENSE_CONDITIONS := notice
LOCAL_MODULE := sound_trigger.primary.amlogic
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := \
    sound_trigger_hw.c \
    ../audio_hal/audio_hw_dsp.c

LOCAL_C_INCLUDES += \
    vendor/amlogic/common/dsp/dsp_util/include \
    $(LOCAL_PATH)/../audio_hal \
    $(LOCAL_PATH)/../utils/include \


LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libhifi4rpc_client \
    libhifi4rpc \
    libmp3tools \

LOCAL_HEADER_LIBRARIES := libhardware_headers
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Wno-error=incompatible-pointer-types \
                -Wno-unused-function \
                -Wno-unused-parameter \
                -Wno-unused-variable
# LOCAL_32_BIT_ONLY := true
include $(BUILD_SHARED_LIBRARY)
endif
