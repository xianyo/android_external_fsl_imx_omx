/**
 *  Copyright (c) 2010-2013, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#ifndef JB
#include <AudioSystem.h>
#endif
#include "AndroidAudioRender.h"
#include <cutils/properties.h>

using namespace android;

AndroidAudioRender::AndroidAudioRender()
{ 
    fsl_osal_strcpy((fsl_osal_char*)name, "OMX.Freescale.std.audio_render.android.sw-based");
    ComponentVersion.s.nVersionMajor = 0x1;
    ComponentVersion.s.nVersionMinor = 0x1;
    ComponentVersion.s.nRevision = 0x2;
    ComponentVersion.s.nStep = 0x0;
    role_cnt = 1;
    role[0] = (OMX_STRING)"audio_render.android";
    bInContext = OMX_FALSE;
    nPorts = NUM_PORTS;
    mAudioSink = NULL;
    bNativeDevice = OMX_FALSE;
    nWrited = 0;
    audioType = OMX_AUDIO_CodingPCM;
    bOpened = OMX_FALSE;
}

OMX_ERRORTYPE AndroidAudioRender::SelectDevice(OMX_PTR device)
{
    sp<MediaPlayerBase::AudioSink> *pSink;

    pSink = (sp<MediaPlayerBase::AudioSink> *)device;
    mAudioSink = *pSink;
    LOG_DEBUG("AndroidAudioRender Set AudioSink %p\n", &mAudioSink);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE AndroidAudioRender::OpenDevice()
{
    status_t ret = NO_ERROR;

    LOG_DEBUG("AndroidAudioRender OpenDevice\n");

    if(mAudioSink == NULL) {
#ifdef RUN_WITH_GM
        /* this is used for run with oxmgm, 
         * you need to define MediaPlayerService::AudioOutput
         * as public class in file MediaPlayerService.h */
        mAudioSink = FSL_NEW(MediaPlayerService::AudioOutput, ());
        if(mAudioSink == NULL) {
            LOG_ERROR("Failed to create AudioSink.\n");
            return OMX_ErrorInsufficientResources;
        }
        bNativeDevice = OMX_TRUE;
#else
        LOG_ERROR("Can't open mAudioSink device.\n");
        return OMX_ErrorHardware;
#endif

    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE AndroidAudioRender::CloseDevice()
{
    LOG_DEBUG("AndroidAudioRender CloseDevice.\n");

    if(mAudioSink == NULL)
        return OMX_ErrorBadParameter;

    mAudioSink->stop();
    mAudioSink->close();
    nWrited = 0;
    bOpened = OMX_FALSE;
    if(bNativeDevice == OMX_TRUE) {
        LOG_DEBUG("clear mAudioSink.\n");
        mAudioSink.clear();
    }

    return OMX_ErrorNone;
}
OMX_ERRORTYPE AndroidAudioRender::GetChannelMask(OMX_U32* mask)
{
    OMX_U32 i = 0;
    *mask = 0;
    if(PcmMode.nChannels > OMX_AUDIO_MAXCHANNELS){
        return OMX_ErrorBadParameter;
    }

    for(i = 0; i < PcmMode.nChannels; i++){

        switch(PcmMode.eChannelMapping[i]){
            case OMX_AUDIO_ChannelLF:
                *mask |= AUDIO_CHANNEL_OUT_FRONT_LEFT;
                break;
            case OMX_AUDIO_ChannelRF:
                *mask |= AUDIO_CHANNEL_OUT_FRONT_RIGHT;
                break;
            case OMX_AUDIO_ChannelCF:
                *mask |= AUDIO_CHANNEL_OUT_FRONT_CENTER;
                break;
            case OMX_AUDIO_ChannelLS:
                *mask |= AUDIO_CHANNEL_OUT_SIDE_LEFT;
                break;
            case OMX_AUDIO_ChannelRS:
                *mask |= AUDIO_CHANNEL_OUT_SIDE_RIGHT;
                break;
            case OMX_AUDIO_ChannelLFE:
                *mask |= AUDIO_CHANNEL_OUT_LOW_FREQUENCY;
                break;
            case OMX_AUDIO_ChannelCS:
                *mask |= AUDIO_CHANNEL_OUT_BACK_CENTER;
                break;
            case OMX_AUDIO_ChannelLR:
                *mask |= AUDIO_CHANNEL_OUT_BACK_LEFT;
                break;
            case OMX_AUDIO_ChannelRR:
                *mask |= AUDIO_CHANNEL_OUT_BACK_RIGHT;
                break;
            default:
                break;
        }

    }
    return OMX_ErrorNone;
}
OMX_ERRORTYPE AndroidAudioRender::SetDevice()
{
    status_t status = NO_ERROR;
    OMX_U32 nSamplingRate;

    if(mAudioSink == NULL)
        return OMX_ErrorBadParameter;

    nChannelsOut = PcmMode.nChannels;
    nBitPerSampleOut = PcmMode.nBitPerSample;
#ifdef OMX_STEREO_OUTPUT
    if (nChannelsOut > 2)
        nChannelsOut = 2;
#endif

    if (nBitPerSampleOut > 16)
        nBitPerSampleOut = 16;

    LOG_DEBUG("SetDevice: SampleRate: %d, Channels: %d, formats: %d, nClockScale %x\n", 
            PcmMode.nSamplingRate, nChannelsOut, nBitPerSampleOut, nClockScale);

    nSamplingRate = (OMX_U64)PcmMode.nSamplingRate * nClockScale / Q16_SHIFT;


    switch(nBitPerSampleOut) {
        case 8:
#ifdef JB
            format = AUDIO_FORMAT_PCM_8_BIT;
#else
#ifdef ICS
            format = AUDIO_FORMAT_PCM_SUB_8_BIT;
#else
            format = AudioSystem::PCM_8_BIT;
#endif
#endif
            break;
        case 16:
#ifdef JB
            format = AUDIO_FORMAT_PCM_16_BIT;
#else
#ifdef ICS
            format = AUDIO_FORMAT_PCM_SUB_16_BIT;
#else
            format = AudioSystem::PCM_16_BIT;
#endif
#endif
            break;
        default:
            return OMX_ErrorBadParameter;
    }

    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;
    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    sPortDef.nPortIndex = IN_PORT;
    ports[IN_PORT]->GetPortDefinition(&sPortDef);
    audioType = sPortDef.format.audio.eEncoding;
    if(audioType == OMX_AUDIO_CodingIEC937)
        format = AUDIO_FORMAT_IEC937;


#ifdef JB
    if(audioType == OMX_AUDIO_CodingIEC937) 
        format = AUDIO_FORMAT_PCM_16_BIT;
#ifdef JB4_3
    unsigned
#endif
        int afFrameCount, afSampleRate, bufferCount;

    if (AudioSystem::getOutputFrameCount(&afFrameCount, AUDIO_STREAM_MUSIC) != NO_ERROR) 
        return OMX_ErrorUndefined;

    if (AudioSystem::getOutputSamplingRate(&afSampleRate, AUDIO_STREAM_MUSIC) != NO_ERROR) 
        return OMX_ErrorUndefined;

    bufferCount = DEFAULT_AUDIOSINK_BUFFERCOUNT;
    char value[PROPERTY_VALUE_MAX];
    if (property_get("ro.kernel.qemu", value, 0)) 
        bufferCount  = 12;  // to prevent systematic buffer underrun for emulator
    
    int frameCount = (nSamplingRate*afFrameCount*bufferCount)/afSampleRate;
    int frameSize;
    if (audio_is_linear_pcm(format)) {
        frameSize = nChannelsOut * audio_bytes_per_sample(format);
    } else {
        frameSize = sizeof(uint8_t);
    }

    nBufferSize = frameCount * frameSize * PcmMode.nChannels \
                  * PcmMode.nBitPerSample / nChannelsOut / nBitPerSampleOut;
    nSampleSize = frameSize * PcmMode.nChannels \
                  * PcmMode.nBitPerSample / nChannelsOut / nBitPerSampleOut;

    // clockscale changed during playback, reopen audiosink
    if (bOpened == OMX_TRUE) {
        OMX_U32 nSamplingRate;

        mAudioSink->stop();
        mAudioSink->close();
        nWrited = 0;

        // Need get from decoder.
        OMX_U32 channelMask = 0;
        GetChannelMask(&channelMask);

        LOG_DEBUG("SetDevice: SampleRate: %d, Channels: %d, nClockScale %x\n", 
                PcmMode.nSamplingRate, nChannelsOut, nClockScale);

        nSamplingRate = (OMX_U64)PcmMode.nSamplingRate * nClockScale / Q16_SHIFT;

        audio_output_flags_t flags = (audioType == OMX_AUDIO_CodingIEC937) ? AUDIO_OUTPUT_FLAG_DIRECT : AUDIO_OUTPUT_FLAG_NONE;

        if(NO_ERROR != mAudioSink->open(nSamplingRate, nChannelsOut, \
                    channelMask, format, DEFAULT_AUDIOSINK_BUFFERCOUNT, NULL, NULL, flags)) {
            LOG_ERROR("Failed to open AudioOutput device.\n");
            return OMX_ErrorHardware;
        }
        
    }
    
#else
    if (bOpened == OMX_TRUE) {
        mAudioSink->stop();
        mAudioSink->close();
        bOpened = OMX_FALSE;
        nWrited = 0;
    }

    if(NO_ERROR != mAudioSink->open(nSamplingRate, nChannelsOut, \
                format, DEFAULT_AUDIOSINK_BUFFERCOUNT)) {
        LOG_ERROR("Failed to open AudioOutput device.\n");
        return OMX_ErrorHardware;
    }
    LOG_DEBUG("buffersize: %d, frameSize: %d, frameCount: %d\n", 
            mAudioSink->bufferSize(), mAudioSink->frameSize(), mAudioSink->frameCount());

    nBufferSize = mAudioSink->bufferSize() * PcmMode.nChannels \
                  * PcmMode.nBitPerSample / nChannelsOut / nBitPerSampleOut;
    nSampleSize = mAudioSink->frameSize() * PcmMode.nChannels \
                  * PcmMode.nBitPerSample / nChannelsOut / nBitPerSampleOut;
    
    bOpened == OMX_TRUE;
#endif

    nPeriodSize = nBufferSize/nSampleSize/DEFAULT_AUDIOSINK_BUFFERCOUNT;

    if(audioType == OMX_AUDIO_CodingIEC937)
        nFadeInFadeOutProcessLen = 0;
    else {
        nFadeInFadeOutProcessLen = nSamplingRate * FADEPROCESSTIME / 1000 * nSampleSize; 
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE AndroidAudioRender::WriteDevice(OMX_U8 *pBuffer, OMX_U32 nActuralLen)
{
    if(mAudioSink == NULL)
        return OMX_ErrorBadParameter;

    OMX_U32 nActuralLenOrg = nActuralLen;

#ifdef OMX_STEREO_OUTPUT
	if (PcmMode.nChannels > 2)
	{
		OMX_U32 i, j, Len;

		Len = nActuralLen / (PcmMode.nBitPerSample>>3) / PcmMode.nChannels;
		nActuralLen = Len * (PcmMode.nBitPerSample>>3) * nChannelsOut;

		switch(PcmMode.nBitPerSample)
		{
			case 8:
				{
					OMX_S8 *pSrc = (OMX_S8 *)pBuffer, *pDst = (OMX_S8 *)pBuffer;
					for (i = 0; i < Len; i ++)
					{
						*pDst++ = *pSrc++;
						*pDst++ = *pSrc++;
						pSrc += PcmMode.nChannels - 2;
					}
				}
				break;
			case 16:
				{
					OMX_S16 *pSrc = (OMX_S16 *)pBuffer, *pDst = (OMX_S16 *)pBuffer;
					for (i = 0; i < Len; i ++)
					{
						*pDst++ = *pSrc++;
						*pDst++ = *pSrc++;
						pSrc += PcmMode.nChannels - 2;
					}
				}
				break;
			case 24:
				{
					OMX_U8 *pSrc = (OMX_U8 *)pBuffer, *pDst = (OMX_U8 *)pBuffer;
					for (i = 0; i < Len; i ++)
					{
						*pDst++ = *pSrc++;
						*pDst++ = *pSrc++;
						*pDst++ = *pSrc++;
						*pDst++ = *pSrc++;
						*pDst++ = *pSrc++;
						*pDst++ = *pSrc++;
						pSrc += (PcmMode.nChannels - 2)*3;
					}
				}
				break;
		}
	}
#endif

	if(PcmMode.nBitPerSample == 8) {
		// Convert to U8
		OMX_S32 i,Len;
		OMX_U8 *pDst =(OMX_U8 *)pBuffer;
		OMX_S8 *pSrc =(OMX_S8 *)pBuffer;
		Len = nActuralLen;
		for(i=0;i<Len;i++) {
				*pDst++ = *pSrc++ + 128;
		}
	} else if(PcmMode.nBitPerSample == 24) {
		OMX_S32 i,j,Len;
		OMX_U8 *pDst =(OMX_U8 *)pBuffer;
		OMX_U8 *pSrc =(OMX_U8 *)pBuffer;
		Len = nActuralLen / (PcmMode.nBitPerSample>>3) / nChannelsOut;
		for(i=0;i<Len;i++) {
			for(j=0;j<(OMX_S32)PcmMode.nChannels;j++) {
				pDst[0] = pSrc[1];
				pDst[1] = pSrc[2];
				pDst+=2;
				pSrc+=3;
			}
		}
		nActuralLen = Len * (nBitPerSampleOut>>3) * nChannelsOut;
	}

    LOG_LOG("AndroidAudioRender write: %d\n", nActuralLen);

    mAudioSink->write(pBuffer, nActuralLen);

    if(nWrited == 0){
        mAudioSink->start();
        LOG_DEBUG("WRITE DEVICE START");
    }
    nWrited += nActuralLenOrg;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE AndroidAudioRender::DrainDevice()
{
    return OMX_ErrorNone;
}

OMX_ERRORTYPE AndroidAudioRender::DeviceDelay(OMX_U32 *nDelayLen)
{
    OMX_U32 nLatency;
    OMX_U32 nPSize = nPeriodSize * nSampleSize;

    if(nWrited > nBufferSize + nPSize)
        *nDelayLen = nBufferSize + nPSize;
    else
        *nDelayLen = nWrited;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE AndroidAudioRender::ResetDevice()
{
    if(mAudioSink == NULL)
        return OMX_ErrorBadParameter;

    mAudioSink->flush();
    if(audioType == OMX_AUDIO_CodingIEC937) {
        mAudioSink->stop();
    }
    LOG_DEBUG("AndroidAudioRender::ResetDevice");
    nWrited = 0;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE AndroidAudioRender::AudioRenderDoExec2Pause()
{
    LOG_DEBUG("AndroidAudioRender::AudioRenderDoExec2Pause");
    if(mAudioSink == NULL)
        return OMX_ErrorBadParameter;

	if (bSendEOS == OMX_TRUE) {
#ifdef FROYO
		OMX_S64 latency = mAudioSink->latency() * 1000;
		printf("Add latency: %lld\n", latency);
		fsl_osal_sleep(latency);
#endif
        LOG_DEBUG("AndroidAudioRender::DoExec2Pause stop");
		mAudioSink->stop();
	} else {
	    LOG_DEBUG("AndroidAudioRender::DoExec2Pause pause");
		mAudioSink->pause();
	}
    return OMX_ErrorNone;
}

OMX_ERRORTYPE AndroidAudioRender::AudioRenderDoPause2Exec()
{
    if(mAudioSink == NULL)
        return OMX_ErrorBadParameter;
    LOG_DEBUG("AndroidAudioRender::AudioRenderDoPause2Exec");

#ifdef JB

    OMX_U32 nSamplingRate;

    if (bOpened == OMX_FALSE) {
        // Need get from decoder.
        OMX_U32 channelMask = 0;
        GetChannelMask(&channelMask);

        LOG_DEBUG("Pause2Exec: SampleRate: %d, Channels: %d, nClockScale %x\n", 
                PcmMode.nSamplingRate, nChannelsOut, nClockScale);

        nSamplingRate = (OMX_U64)PcmMode.nSamplingRate * nClockScale / Q16_SHIFT;

        audio_output_flags_t flags = (audioType == OMX_AUDIO_CodingIEC937) ? AUDIO_OUTPUT_FLAG_DIRECT : AUDIO_OUTPUT_FLAG_NONE;

        if(NO_ERROR != mAudioSink->open(nSamplingRate, nChannelsOut, \
                    channelMask, format, DEFAULT_AUDIOSINK_BUFFERCOUNT, NULL, NULL, flags)) {
            LOG_ERROR("Failed to open AudioOutput device.\n");
            return OMX_ErrorHardware;
        }
        bOpened = OMX_TRUE;
    }
#endif
    
    mAudioSink->start();
    return OMX_ErrorNone;
}


/**< C style functions to expose entry point for the shared library */
extern "C" {
    OMX_ERRORTYPE AndroidAudioRenderInit(OMX_IN OMX_HANDLETYPE pHandle)
    {
        OMX_ERRORTYPE ret = OMX_ErrorNone;
        AndroidAudioRender *obj = NULL;
        ComponentBase *base = NULL;

        obj = FSL_NEW(AndroidAudioRender, ());
        if(obj == NULL)
            return OMX_ErrorInsufficientResources;

        base = (ComponentBase*)obj;
        ret = base->ConstructComponent(pHandle);
        if(ret != OMX_ErrorNone)
            return ret;

        return ret;
    }
}

/* File EOF */
