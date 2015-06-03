ifeq ($(HAVE_FSL_IMX_CODEC),true)


LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	avcodec.c

LOCAL_C_INCLUDES += $(FSL_OMX_INCLUDES) \
	$(LOCAL_PATH)/../../../../../device/fsl-codec/ghdr/libav
		
LOCAL_SHARED_LIBRARIES := libavcodec-55 \
	libavutil-53
	
LOCAL_MODULE:= libavcodec_test_arm11_elinux

LOCAL_MODULE_TAGS := eng
include $(BUILD_EXECUTABLE)

endif
