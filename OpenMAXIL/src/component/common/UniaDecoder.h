/**
 *  Copyright (c) 2009-2013, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

/**
 *  @file UniaDecoder.h
 *  @brief Class definition of UniaDecoder Component
 *  @ingroup UniaDecoder
 */
#ifndef UniaDecoder_h
#define UniaDecoder_h

#include "AudioFilter.h"
#include "ShareLibarayMgr.h"
#include "fsl_unia.h"

typedef struct
{
    UniACodecVersionInfo                GetVersionInfo;
    UniACodecCreate                     CreateDecoder;
    UniACodecDelete                     DeleteDecoder;
    UniACodecReset                      ResetDecoder;
    UniACodecSetParameter               SetParameter;
    UniACodecGetParameter               GetParameter;
    UniACodec_decode_frame              DecodeFrame;
    UniACodec_get_last_error            GetLastError;

}UniaDecInterface;

class UniaDecoder : public AudioFilter {
    public:
        UniaDecoder();
    protected:
        OMX_S32 codingType;
        OMX_U32 outputPortBufferSize;
        const char * decoderLibName;
        const char * optionaDecoderlLibName;
        OMX_BOOL frameInput;
    private:
        OMX_ERRORTYPE InitComponent();
        OMX_ERRORTYPE DeInitComponent();
        OMX_ERRORTYPE AudioFilterInstanceInit();
        OMX_ERRORTYPE AudioFilterCodecInit();
        OMX_ERRORTYPE AudioFilterInstanceDeInit();
        AUDIO_FILTERRETURNTYPE AudioFilterFrame();
        OMX_ERRORTYPE AudioFilterReset();
        OMX_ERRORTYPE AudioFilterCheckCodecConfig();
        OMX_ERRORTYPE CreateDecoderInterface();
        OMX_ERRORTYPE InitPort();
        OMX_ERRORTYPE SetParameterToDecoder();
        OMX_ERRORTYPE ResetParameterWhenOutputChange();
        OMX_ERRORTYPE MapOutputLayoutChannel(UniAcodecOutputPCMFormat * outputValue);
        virtual OMX_ERRORTYPE UniaDecoderSetParameter(UA_ParaType index,OMX_S32 value) = 0;
        virtual OMX_ERRORTYPE UniaDecoderGetParameter(UA_ParaType index,OMX_S32 * value) = 0;
        ShareLibarayMgr *libMgr;
        OMX_PTR hLib;
        UniaDecInterface * IDecoder;

        UniACodecMemoryOps memOps;
        UniACodec_Handle uniaHandle;

        OMX_S32 errorCount;//debug
        OMX_S32 profileErrorCount;

        UniACodecParameterBuffer codecConfig;
        CHAN_TABLE channelTable;
        OMX_U32 inputFrameCount;
        OMX_U32 consumeFrameCount;
};
#endif
