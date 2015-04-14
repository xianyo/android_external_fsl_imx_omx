ifeq ($(HAVE_FSL_IMX_CODEC),true)


LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	ID3.cpp

LOCAL_CFLAGS += $(FSL_OMX_CFLAGS)
LOCAL_LDFLAGS += $(FSL_OMX_LDFLAGS)

LOCAL_SHARED_LIBRARIES := libstagefright libstagefright_foundation libutils libc liblog

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE:= lib_id3_parser_arm11_elinux

include $(BUILD_SHARED_LIBRARY)

endif
