/**
 *  Copyright (c) 2009-2013, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "Mp3Dec.h"
#include "Mp3FrameParser.h"
//#undef LOG_DEBUG
//#define LOG_DEBUG printf

#define MP3D_FRAME_SIZE  576
#define MP3_PUSH_MODE_LEN   (2048*4)

Mp3Dec::Mp3Dec()
{
    fsl_osal_strcpy((fsl_osal_char*)name, "OMX.Freescale.std.audio_decoder.mp3.sw-based");
    ComponentVersion.s.nVersionMajor = 0x0;
    ComponentVersion.s.nVersionMinor = 0x1;
    ComponentVersion.s.nRevision = 0x0;
    ComponentVersion.s.nStep = 0x0;
    role_cnt = 1;
    role[0] = (OMX_STRING)"audio_decoder.mp3";
    codingType = OMX_AUDIO_CodingMP3;
    nPushModeInputLen = MP3_PUSH_MODE_LEN;
    outputPortBufferSize = MP3D_FRAME_SIZE* 2*2;

    decoderLibName = "lib_mp3d_wrap_arm12_elinux_android.so";
    OMX_INIT_STRUCT(&Mp3Type, OMX_AUDIO_PARAM_MP3TYPE);
    Mp3Type.nPortIndex = AUDIO_FILTER_INPUT_PORT;
    Mp3Type.nChannels = 2;
    Mp3Type.nSampleRate = 44100;
    Mp3Type.eChannelMode = OMX_AUDIO_ChannelModeStereo;
    Mp3Type.eFormat = OMX_AUDIO_MP3StreamFormatMP1Layer3;
    LOG_DEBUG("Unia -> MP3");

}
OMX_ERRORTYPE Mp3Dec::AudioFilterGetParameter(OMX_INDEXTYPE nParamIndex, OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch (nParamIndex) {
        case OMX_IndexParamAudioMp3:
            {
                OMX_AUDIO_PARAM_MP3TYPE *pMp3Type;
                pMp3Type = (OMX_AUDIO_PARAM_MP3TYPE*)pComponentParameterStructure;
                OMX_CHECK_STRUCT(pMp3Type, OMX_AUDIO_PARAM_MP3TYPE, ret);
                if(ret != OMX_ErrorNone)
                    break;
                if (pMp3Type->nPortIndex != AUDIO_FILTER_INPUT_PORT)
                {
                    ret = OMX_ErrorBadPortIndex;
                    break;
                }
                fsl_osal_memcpy(pMp3Type, &Mp3Type, sizeof(OMX_AUDIO_PARAM_MP3TYPE));
                break;
            }
        default:
            ret = OMX_ErrorUnsupportedIndex;
            break;
    }

    return ret;
}
OMX_ERRORTYPE Mp3Dec::AudioFilterSetParameter(OMX_INDEXTYPE nParamIndex, OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch (nParamIndex) {
        case OMX_IndexParamAudioMp3:
            {
                OMX_AUDIO_PARAM_MP3TYPE *pMp3Type;
                pMp3Type = (OMX_AUDIO_PARAM_MP3TYPE*)pComponentParameterStructure;
                OMX_CHECK_STRUCT(pMp3Type, OMX_AUDIO_PARAM_MP3TYPE, ret);
                if(ret != OMX_ErrorNone)
                    break;
                if (pMp3Type->nPortIndex != AUDIO_FILTER_INPUT_PORT)
                {
                    ret = OMX_ErrorBadPortIndex;
                    break;
                }
                fsl_osal_memcpy(&Mp3Type, pMp3Type, sizeof(OMX_AUDIO_PARAM_MP3TYPE));
                break;
            }
        default:
            ret = OMX_ErrorUnsupportedIndex;
            break;
    }

    return ret;
}
OMX_ERRORTYPE Mp3Dec::UniaDecoderSetParameter(UA_ParaType index,OMX_S32 value)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    switch(index){
        case UNIA_SAMPLERATE:
            Mp3Type.nSampleRate = value;
            break;
        case UNIA_CHANNEL:
            Mp3Type.nChannels = value;
            break;
        case UNIA_BITRATE:
            Mp3Type.nBitRate = value;
            break;
        default:
            ret = OMX_ErrorNotImplemented;
            break;
    }
    return ret;
}
OMX_ERRORTYPE Mp3Dec::UniaDecoderGetParameter(UA_ParaType index,OMX_S32 * value)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    if(value == NULL){
        ret = OMX_ErrorBadParameter;
        return ret;
    }

    switch(index){
        case UNIA_SAMPLERATE:
            *value = Mp3Type.nSampleRate;
            break;
        case UNIA_CHANNEL:
            *value = Mp3Type.nChannels;
            break;
        case UNIA_BITRATE:
            *value = Mp3Type.nBitRate;
            break;
        case UNIA_FRAMED:
            *value = OMX_TRUE;
            break;
        default:
            ret = OMX_ErrorNotImplemented;
            break;
    }
    return ret;
}
OMX_ERRORTYPE Mp3Dec::AudioFilterCheckFrameHeader()
{
    OMX_ERRORTYPE ret = OMX_ErrorUndefined;
    OMX_U8 *pBuffer;
    OMX_U8 *pHeader;
    OMX_U32 nActuralLen;
    OMX_U32 nOffset=0;
    OMX_BOOL bFounded = OMX_FALSE;
    AUDIO_FRAME_INFO FrameInfo;
    fsl_osal_memset(&FrameInfo, 0, sizeof(AUDIO_FRAME_INFO));
    LOG_LOG("Mp3Dec AudioFilterCheckFrameHeader");

    do{
        AudioRingBuffer.BufferGet(&pBuffer, MP3_PUSH_MODE_LEN, &nActuralLen);
        LOG_LOG("Get stream length: %d\n", nActuralLen);

        if (nActuralLen < MP3_PUSH_MODE_LEN){
            ret = OMX_ErrorNone;
            break;
        }

        if(AFP_SUCCESS != Mp3CheckFrame(&FrameInfo, pBuffer, nActuralLen)){
            break;
        }

        if(FrameInfo.bGotOneFrame){
            bFounded = OMX_TRUE;
        }

        nOffset = FrameInfo.nConsumedOffset;

        if(nOffset < nActuralLen)
            LOG_LOG("buffer=%02x%02x%02x%02x",pBuffer[nOffset],pBuffer[nOffset+1],pBuffer[nOffset+2],pBuffer[nOffset+1]);

        LOG_LOG("Mp3 decoder skip %d bytes.\n", nOffset);
        AudioRingBuffer.BufferConsumered(nOffset);
        TS_Manager.Consumered(nOffset);

        if (bFounded == OMX_TRUE){
            ret = OMX_ErrorNone;
            break;
        }

    }while (0);

    return ret;
}
/**< C style functions to expose entry point for the shared library */
extern "C" {
    OMX_ERRORTYPE Mp3DecInit(OMX_IN OMX_HANDLETYPE pHandle)
    {
        OMX_ERRORTYPE ret = OMX_ErrorNone;
        Mp3Dec *obj = NULL;
        ComponentBase *base = NULL;

        obj = FSL_NEW(Mp3Dec, ());
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
