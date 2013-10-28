/**
 *  Copyright (c) 2010-2013, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/**
 *  @file AndroidAudioRender.h
 *  @brief Class definition of AndroidAudioRender Component
 *  @ingroup AndroidAudioRender
 */

#ifndef AndroidAudioRender_h
#define AndroidAudioRender_h

#ifdef JB
#include <utils/Errors.h>
#else
#include <Errors.h>
#endif
#include <AudioTrack.h>
#include <MediaPlayerInterface.h>
#ifndef JB
#include <MediaPlayerService.h>
#endif
#include "ComponentBase.h"
#include "AudioRender.h"

namespace android {

class AndroidAudioRender : public AudioRender {
    public:
        AndroidAudioRender();
    private:
        OMX_ERRORTYPE SelectDevice(OMX_PTR device);
        OMX_ERRORTYPE OpenDevice();
        OMX_ERRORTYPE CloseDevice();
        OMX_ERRORTYPE SetDevice();
        OMX_ERRORTYPE ResetDevice();
        OMX_ERRORTYPE DrainDevice();
        OMX_ERRORTYPE DeviceDelay(OMX_U32 *nDelayLen);
        OMX_ERRORTYPE WriteDevice(OMX_U8 *pBuffer, OMX_U32 nActuralLen);
        OMX_ERRORTYPE AudioRenderDoExec2Pause();
        OMX_ERRORTYPE AudioRenderDoPause2Exec();
        OMX_ERRORTYPE GetChannelMask(OMX_U32* mask);
        OMX_BOOL bNativeDevice;
        OMX_BOOL bOpened;
        sp<MediaPlayerBase::AudioSink> mAudioSink;

        OMX_AUDIO_CODINGTYPE audioType;
        OMX_U32 nWrited;
        OMX_U32 nBufferSize;
        OMX_U32 nChannelsOut;
        OMX_U32 nBitPerSampleOut;
#ifdef JB
        audio_format_t format;
#else
        OMX_S32 format;
#endif
};

}

#endif
/* File EOF */
