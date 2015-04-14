/**
 *  Copyright (c) 2014, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "LibavAudioDec.h"

#define LIBAV_COMP_NAME_ADPCMDEC "OMX.Freescale.std.audio_decoder.adpcm.sw-based"
#define LIBAV_COMP_NAME_G711DEC "OMX.Freescale.std.audio_decoder.g711.sw-based"

static void libav_log_callback (void *ptr, int level, const char *fmt, va_list vl)
{
    LogLevel log_level;

    switch (level) {
        case AV_LOG_QUIET:
            log_level = LOG_LEVEL_NONE;
            break;
        case AV_LOG_ERROR:
            log_level = LOG_LEVEL_ERROR;
            break;
        case AV_LOG_INFO:
            log_level = LOG_LEVEL_INFO;
            break;
        case AV_LOG_DEBUG:
            log_level = LOG_LEVEL_DEBUG;
            break;
        default:
            log_level = LOG_LEVEL_INFO;
            break;
    }

    LOG2(log_level, fmt, vl);
}

OMX_ERRORTYPE  LibavAudioDec::GetCodecID()
{
    switch ((OMX_U32)CodingType) {
        case OMX_AUDIO_CodingG711:
        {
            if (PcmModeIn.ePCMMode == OMX_AUDIO_PCMModeALaw)
                codecID = AV_CODEC_ID_PCM_ALAW;
            else if (PcmModeIn.ePCMMode == OMX_AUDIO_PCMModeMULaw)
                codecID = AV_CODEC_ID_PCM_MULAW;
        }
            break;
        case OMX_AUDIO_CodingADPCM:
        {
	    if (AdpcmMode.CodecID == ADPCM_MS)
                codecID = AV_CODEC_ID_ADPCM_MS;
            else if (AdpcmMode.CodecID == ADPCM_IMA_WAV)
                codecID = AV_CODEC_ID_ADPCM_IMA_WAV;
        }
            break;
        default:
            return OMX_ErrorUndefined;
    }
    return OMX_ErrorNone;
}

LibavAudioDec::LibavAudioDec()
{
    ComponentVersion.s.nVersionMajor = 0x1;
    ComponentVersion.s.nVersionMinor = 0x1;
    ComponentVersion.s.nRevision = 0x2;
    ComponentVersion.s.nStep = 0x0;
    role_cnt = 1;
    role[0] = (OMX_STRING)"audio_decoder.adpcm";
    bInContext = OMX_FALSE;
    nPorts = AUDIO_FILTER_PORT_NUMBER;
    nPushModeInputLen = 4096;
    nRingBufferScale = RING_BUFFER_SCALE;
    CodingType = OMX_AUDIO_CodingUnused;
    codecID = AV_CODEC_ID_NONE;
    codecContext = NULL;
    frame = NULL;
}

OMX_ERRORTYPE LibavAudioDec::InitComponent()
{
    /*set default definition*/
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    sPortDef.nPortIndex = AUDIO_FILTER_INPUT_PORT;
    sPortDef.eDir = OMX_DirInput;
    sPortDef.eDomain = OMX_PortDomainAudio;
    sPortDef.format.audio.eEncoding = CodingType;
    sPortDef.bPopulated = OMX_FALSE;
    sPortDef.bEnabled = OMX_TRUE;
    sPortDef.nBufferCountMin = 1;
    sPortDef.nBufferCountActual = 3;
    sPortDef.nBufferSize = 1024;
    ret = ports[AUDIO_FILTER_INPUT_PORT]->SetPortDefinition(&sPortDef);
    if(ret != OMX_ErrorNone) {
        LOG_ERROR("Set port definition for port[%d] failed.\n", AUDIO_FILTER_INPUT_PORT);
        return ret;
    }

    sPortDef.nPortIndex = AUDIO_FILTER_OUTPUT_PORT;
    sPortDef.eDir = OMX_DirOutput;
    sPortDef.eDomain = OMX_PortDomainAudio;
    sPortDef.format.audio.eEncoding = OMX_AUDIO_CodingPCM;
    sPortDef.bPopulated = OMX_FALSE;
    sPortDef.bEnabled = OMX_TRUE;
    sPortDef.nBufferCountMin = 1;
    sPortDef.nBufferCountActual = 3;
    sPortDef.nBufferSize = nPushModeInputLen*4;
    ret = ports[AUDIO_FILTER_OUTPUT_PORT]->SetPortDefinition(&sPortDef);
    if(ret != OMX_ErrorNone) {
	LOG_ERROR("Set port definition for port[%d] failed.\n", 0);
	return ret;
    }

	OMX_INIT_STRUCT(&PcmMode, OMX_AUDIO_PARAM_PCMMODETYPE);

	PcmMode.nPortIndex = AUDIO_FILTER_OUTPUT_PORT;
	PcmMode.nChannels = 2;
	PcmMode.nSamplingRate = 44100;
	PcmMode.nBitPerSample = 16;
	PcmMode.bInterleaved = OMX_TRUE;
	PcmMode.eNumData = OMX_NumericalDataSigned;
	PcmMode.ePCMMode = OMX_AUDIO_PCMModeLinear;
	PcmMode.eEndian = OMX_EndianLittle;
	PcmMode.eChannelMapping[0] = OMX_AUDIO_ChannelNone;

	return ret;
}

OMX_ERRORTYPE LibavAudioDec::DeInitComponent()
{
    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavAudioDec::GetCodecContext()
{
    switch ((OMX_U32)CodingType) {
        case OMX_AUDIO_CodingG711:
            codecContext->channels = PcmModeIn.nChannels;
            codecContext->sample_rate = PcmModeIn.nSamplingRate;
            codecContext->bits_per_coded_sample = PcmModeIn.nBitPerSample;
            break;

        case OMX_AUDIO_CodingADPCM:
            codecContext->channels = AdpcmMode.nChannels;
            codecContext->sample_rate = AdpcmMode.nSampleRate;
            codecContext->bits_per_coded_sample = AdpcmMode.nBitPerSample;
            codecContext->block_align = AdpcmMode.nBlockAlign;
            LOG_DEBUG("\t channels: %d \t sample_rate: %d \t bits_per_sample: %d \t block_align: %d \tcodecID: %x\n", \
            AdpcmMode.nChannels, AdpcmMode.nSampleRate, AdpcmMode.nBitPerSample, AdpcmMode.nBlockAlign, codecID);
            break;

        default:
            break;
    }
	return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavAudioDec::AudioFilterInstanceInit()
{
    avcodec_register_all();

    if (OMX_ErrorNone != GetCodecID())
        return OMX_ErrorUndefined;
	if (codecID == AV_CODEC_ID_NONE)
		return OMX_ErrorUndefined;
	
    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavAudioDec::AudioFilterCodecInit()
{
    AVCodec* codec;
	codec =  avcodec_find_decoder(codecID);
    if(!codec){
        LOG_ERROR("find decoder fail, codecID %x" , codecID);
        return OMX_ErrorUndefined;
    }

    codecContext = avcodec_alloc_context3(codec);
    if(!codecContext){
        LOG_ERROR("alloc context fail");
        return OMX_ErrorUndefined;
    }

    GetCodecContext();

    if(avcodec_open2(codecContext, codec, NULL) < 0){
        LOG_ERROR("codec open fail");
        return OMX_ErrorUndefined;
    }

    frame = av_frame_alloc();
    if(frame == NULL){
        return OMX_ErrorInsufficientResources;
    }
    return OMX_ErrorNone;  
}

OMX_ERRORTYPE LibavAudioDec::AudioFilterInstanceDeInit()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if(codecContext) {
        avcodec_close(codecContext);
        av_free(codecContext);
        codecContext = NULL;
    }
	if(frame) {
        av_frame_free(&frame);
        frame = NULL;
    }
    return ret;
}

OMX_ERRORTYPE LibavAudioDec::AudioFilterGetParameter(
        OMX_INDEXTYPE nParamIndex,
        OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    switch (nParamIndex) {
        case OMX_IndexParamStandardComponentRole:
            fsl_osal_strcpy((OMX_STRING)((OMX_PARAM_COMPONENTROLETYPE*) \
                        pComponentParameterStructure)->cRole,(OMX_STRING)cRole);
            break;
        case OMX_IndexParamAudioAdpcm:
        {
	        OMX_AUDIO_PARAM_ADPCMMODETYPE *pAdpcmMode;
            pAdpcmMode = (OMX_AUDIO_PARAM_ADPCMMODETYPE*)pComponentParameterStructure;
            OMX_CHECK_STRUCT(pAdpcmMode, OMX_AUDIO_PARAM_ADPCMMODETYPE, ret);

            if(ret != OMX_ErrorNone)
                break;

            fsl_osal_memcpy(pAdpcmMode, &AdpcmMode, sizeof(OMX_AUDIO_PARAM_ADPCMMODETYPE));
            CodingType = OMX_AUDIO_CodingADPCM;
            fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_ADPCMDEC);
        }
            break;
        default:
            ret = OMX_ErrorUnsupportedIndex;
            break;
    }

    return ret;
}

OMX_ERRORTYPE  LibavAudioDec::SetRoleFormat(OMX_STRING role)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if(fsl_osal_strcmp(role, "audio_decoder.adpcm") == 0) {
        CodingType = OMX_AUDIO_CodingADPCM;
        fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_ADPCMDEC);
    }
	else if(fsl_osal_strcmp(role, "audio_decoder.g711") == 0) {
        CodingType = OMX_AUDIO_CodingG711;
        fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_G711DEC);
    }
	else {
        CodingType = OMX_AUDIO_CodingUnused;
        codecID = AV_CODEC_ID_NONE;
        LOG_ERROR("%s: failure: unknow role: %s \r\n",__FUNCTION__,role);
        return OMX_ErrorUndefined;
    }

    ret = CheckPortFmt();
	if(ret != OMX_ErrorNone)
		return ret;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavAudioDec::AudioFilterSetParameter(
        OMX_INDEXTYPE nParamIndex,
        OMX_PTR pComponentParameterStructure)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    switch((int)nParamIndex){
        case OMX_IndexParamStandardComponentRole:
        {
            fsl_osal_strcpy( (fsl_osal_char *)cRole,(fsl_osal_char *) \
            ((OMX_PARAM_COMPONENTROLETYPE*)pComponentParameterStructure)->cRole);
            if (OMX_ErrorNone != SetRoleFormat((OMX_STRING)cRole)) {
                LOG_DEBUG("SetRoleFormat return error.\n");
                return OMX_ErrorBadParameter;
            }
        }
            break;

        case OMX_IndexParamAudioAdpcm:
        {
	        OMX_AUDIO_PARAM_ADPCMMODETYPE *pAdpcmMode;
            pAdpcmMode = (OMX_AUDIO_PARAM_ADPCMMODETYPE*)pComponentParameterStructure;
            OMX_CHECK_STRUCT(pAdpcmMode, OMX_AUDIO_PARAM_ADPCMMODETYPE, ret);

            if(ret != OMX_ErrorNone)
                break;

	        fsl_osal_memcpy(&AdpcmMode, pAdpcmMode, sizeof(OMX_AUDIO_PARAM_ADPCMMODETYPE));

            if (PcmMode.nChannels != AdpcmMode.nChannels || \
		            PcmMode.nSamplingRate != AdpcmMode.nSampleRate || \
		            PcmMode.nBitPerSample != AdpcmMode.nBitPerSample) {
                PcmMode.nChannels = AdpcmMode.nChannels;
		PcmMode.nSamplingRate = AdpcmMode.nSampleRate;
                switch (AdpcmMode.CodecID) {
                    case ADPCM_IMA_WAV:
                        codecID = AV_CODEC_ID_ADPCM_IMA_WAV;
                        break;
                    case ADPCM_MS:
                        codecID = AV_CODEC_ID_ADPCM_MS;
                        break;
                    default:
                        codecID = AV_CODEC_ID_NONE;
                        break;
                }
            }

            nPushModeInputLen = AdpcmMode.nBlockAlign;
            CodingType = OMX_AUDIO_CodingADPCM;
            fsl_osal_strcpy((fsl_osal_char*)name, LIBAV_COMP_NAME_ADPCMDEC);
        }
            break;
        default:
            ret = OMX_ErrorUnsupportedIndex;
            break;
    }

    return ret;
}

OMX_ERRORTYPE LibavAudioDec::AudioFilterSetParameterPCM()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

	if (PcmMode.nChannels != PcmModeIn.nChannels \
			|| PcmMode.nSamplingRate != PcmModeIn.nSamplingRate \
			|| PcmMode.nBitPerSample!= PcmModeIn.nBitPerSample)
	{
	    PcmModeIn.nChannels = PcmMode.nChannels;
	    PcmModeIn.nSamplingRate = PcmMode.nSamplingRate;
	    PcmModeIn.nBitPerSample = PcmMode.nBitPerSample;
	    PcmModeIn.ePCMMode = PcmMode.ePCMMode;

	    PcmMode.nPortIndex = AUDIO_FILTER_OUTPUT_PORT;
	    PcmMode.nBitPerSample = 16;
	    PcmMode.bInterleaved = OMX_TRUE;
	    PcmMode.eNumData = OMX_NumericalDataSigned;
	    PcmMode.ePCMMode = OMX_AUDIO_PCMModeLinear;
	    PcmMode.eEndian = OMX_EndianLittle;
	    PcmMode.eChannelMapping[0] = OMX_AUDIO_ChannelNone;
	}
    return ret;
}

OMX_ERRORTYPE LibavAudioDec::AVConvertSample(OMX_U32 len)
{
    if ( pOutBufferHdr->pBuffer == NULL )
		return OMX_ErrorUndefined;

	OMX_U32 ch = codecContext->channels;
	if ( ch > 1 && ( codecContext->sample_fmt == AV_SAMPLE_FMT_S16P || \
	                 codecContext->sample_fmt == AV_SAMPLE_FMT_S32P || \
                         codecContext->sample_fmt == AV_SAMPLE_FMT_U8P || \
		         codecContext->sample_fmt == AV_SAMPLE_FMT_FLTP || \
		         codecContext->sample_fmt == AV_SAMPLE_FMT_DBLP )) {

	    for (OMX_U32 i = 0; i < len/ch - 2; i += 2) {
	        for ( OMX_U32 j = 0; j < ch; j++) {
                    pOutBufferHdr->pBuffer[ch*(i+j)] = frame->data[j][i];
                    pOutBufferHdr->pBuffer[ch*(i+j)+1] = frame->data[j][i+1];
	        }
	    } 
	}
	else
	    memcpy(pOutBufferHdr->pBuffer, frame->data[0], len);

	return OMX_ErrorNone;

}

AUDIO_FILTERRETURNTYPE LibavAudioDec::AudioFilterFrame()
{
    AUDIO_FILTERRETURNTYPE ret = AUDIO_FILTER_SUCCESS;
    OMX_U8 *pBuffer = NULL;
    OMX_U32 nActuralLen = 0;
    OMX_U32 nConsumeLen = 0;
    OMX_U32 data_size = 0;
    OMX_U32 nchannel = 0;
    int nOutSize = 0;
    int len = 0;
    AVPacket pkt;

    AudioRingBuffer.BufferGet(&pBuffer, nPushModeInputLen, &nActuralLen);
    if (!nActuralLen){
	LOG_ERROR("AudioRingBuffer.BufferGet failed.\n");
	pOutBufferHdr->nOffset = 0;
	pOutBufferHdr->nFilledLen = 0;
	return AUDIO_FILTER_EOS;
    }
	
    av_init_packet(&pkt);
    pkt.data = (uint8_t *)pBuffer;
    pkt.size = nActuralLen;

    len = avcodec_decode_audio4(codecContext, frame, &nOutSize, &pkt);
	data_size = av_samples_get_buffer_size(NULL, codecContext->channels,
                                                 frame->nb_samples,
                                                 codecContext->sample_fmt, 1);
    if(len < 0){
        LOG_ERROR("libav decode fail %d\n", len);	
        return AUDIO_FILTER_FATAL_ERROR;
    }else if(len == 0){
        LOG_ERROR("libav decode need more %d\n", len);
        return AUDIO_FILTER_NEEDMORE;
    }
	
    nConsumeLen = len;	
    if(nConsumeLen != nActuralLen)
        LOG_WARNING("libav only support au alignment input.");

	LOG_DEBUG("\t data_size: %d \t nConsumeLen: %d\n", data_size, nConsumeLen);
	LOG_DEBUG("ch: %d sampleRate: %d BitPerSample: %d\n", PcmMode.nChannels, \
		PcmMode.nSamplingRate, PcmMode.nBitPerSample);
	
    /* AV sample convert*/
    AVConvertSample(data_size);

    pOutBufferHdr->nFilledLen = data_size;
    AudioRingBuffer.BufferConsumered(nConsumeLen);
	
    TS_PerFrame = (OMX_S64)pOutBufferHdr->nFilledLen * OMX_TICKS_PER_SECOND * 8 \
		           / PcmMode.nChannels / PcmMode.nBitPerSample / PcmMode.nSamplingRate;
    TS_Manager.Consumered(nConsumeLen);
    pOutBufferHdr->nOffset = 0;
    TS_Manager.TS_SetIncrease(TS_PerFrame);

    return ret;
}

OMX_ERRORTYPE LibavAudioDec::AudioFilterReset()
{
    AudioFilterInstanceDeInit();
    AudioFilterCodecInit();
    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavAudioDec::CheckPortFmt()
{
    /*change input and outport CodeingType*/
    OMX_PARAM_PORTDEFINITIONTYPE sPortDef;

    OMX_INIT_STRUCT(&sPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    sPortDef.nPortIndex = AUDIO_FILTER_INPUT_PORT;
    ports[AUDIO_FILTER_INPUT_PORT]->GetPortDefinition(&sPortDef);
    sPortDef.format.audio.eEncoding = CodingType;
    ports[AUDIO_FILTER_INPUT_PORT]->SetPortDefinition(&sPortDef);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE LibavAudioDec::AudioFilterCheckCodecConfig()
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    return ret;
}

/**< C style functions to expose entry point for the shared library */
extern "C" {
    OMX_ERRORTYPE LibavAudioDecInit(OMX_IN OMX_HANDLETYPE pHandle)
    {
        OMX_ERRORTYPE ret = OMX_ErrorNone;
        LibavAudioDec *obj = NULL;
        ComponentBase *base = NULL;

        obj = FSL_NEW(LibavAudioDec, ());
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
