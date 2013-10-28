/**
 *  Copyright (c) 2012-2013, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "StreamingPipe.h"
#include "StreamingCache.h"
#include "Log.h"
#include "Mem.h"

#if 0
#undef LOG_DEBUG
#define LOG_DEBUG printf
#endif

StreamingPipe::StreamingPipe(StreamingProtocol * protocol)
{
    nOffset = 0;
    pCache = NULL;
    nLen = 0;
    ClientCallback = NULL;
    pProtocol = protocol;
}

StreamingPipe::~StreamingPipe()
{
}

OMX_ERRORTYPE StreamingPipe::Init(CPuint nCacheSize)
{
    pCache = FSL_NEW(StreamingCache, ());
    if(pCache == NULL) {
        LOG_ERROR("Failed to New StreamingCache .\n");
        return OMX_ErrorInsufficientResources;
    }

    OMX_ERRORTYPE err = pCache->Init(pProtocol, nCacheSize);
    if(OMX_ErrorNone != err) {
        LOG_ERROR(" Failed to initialize cache\n");
        FSL_DELETE(pCache);
        return err;
    }

    nLen = pCache->GetContentLength();

    return OMX_ErrorNone;
}

OMX_ERRORTYPE StreamingPipe::DeInit()
{
    pCache->DeInit();

    if(pCache != NULL) {
        FSL_DELETE(pCache);
    }

    if(pProtocol != NULL) {
        FSL_DELETE(pProtocol);
    }

    return OMX_ErrorNone;
}

CPresult StreamingPipe::CheckAvailableBytes(
        CPuint nBytesRequested, CP_CHECKBYTESRESULTTYPE *eResult)
{
    if(nBytesRequested == (CPuint)-1) {
        OMX_U32 nAvailable = pCache->AvailableBytes(nOffset);
        *eResult = (CP_CHECKBYTESRESULTTYPE) nAvailable;
        return STREAMING_PIPE_SUCESS;
    }

    if(nLen > 0 && nOffset >= nLen) {
        *eResult = CP_CheckBytesAtEndOfStream;
        LOG_ERROR("Data is EOS at [%lld:%d]\n", nOffset, nBytesRequested);
        return STREAMING_PIPE_SUCESS;
    }

    LOG_DEBUG("StreamingPipe::CheckAvailableBytes nOffset %lld, request %d\n", nOffset, nBytesRequested);
    
    if(OMX_TRUE != pCache->AvailableAt(nOffset, nBytesRequested)) {
        *eResult = CP_CheckBytesInsufficientBytes;
        LOG_DEBUG("Data is not enough at [%lld:%d]\n", nOffset, nBytesRequested);
    }
    else {
        *eResult = CP_CheckBytesOk;
        LOG_DEBUG("Data is enough at [%lld:%d]\n", nOffset, nBytesRequested);
    }

    return STREAMING_PIPE_SUCESS;
}

CPresult StreamingPipe::SetPosition(CPint64 offset, CP_ORIGINTYPE eOrigin)
{

    LOG_DEBUG("StreamingPipe::%s to: %lld, type: %d\n", __FUNCTION__, offset, eOrigin);

    switch(eOrigin)
    {
        case CP_OriginBegin:
            nOffset = offset;
            break;
        case CP_OriginCur:
            nOffset += offset;
            break;
        case CP_OriginEnd:
            nOffset = nLen + offset;
            break;
        default:
            return KD_EINVAL;
    }

    return STREAMING_PIPE_SUCESS;
}

CPint64 StreamingPipe::GetPosition()
{
    return nOffset;
}

CPresult StreamingPipe::Read(CPbyte *pData, CPuint nSize)
{
    CPint nActualRead = 0;

    if(pData == NULL || nSize == 0)
        return -KD_EINVAL;

    //printf("StreamingPipe::Read want %d at offset %d\n", nSize, nOffset);

    nActualRead = pCache->ReadAt(nOffset, nSize, pData);

    //LOG_DEBUG("StreamingPipe::Read actual read %d\n", nActualRead);
    LOG_DEBUG("StreamingPipe::Read want %d, actual read %d [%x]\n",nSize, nActualRead, *pData);

    nOffset += nActualRead;

    return nActualRead;
}

CPresult StreamingPipe::ReadBuffer(CPbyte **ppBuffer, CPuint *nSize, CPbool bForbidCopy)
{
    return KD_EINVAL;
}

CPresult StreamingPipe::ReleaseReadBuffer(CPbyte *pBuffer)
{
    return KD_EINVAL;
}

CPresult StreamingPipe::Write(CPbyte *data, CPuint nSize)
{
    return KD_EINVAL;
}

CPresult StreamingPipe::GetWriteBuffer(CPbyte **ppBuffer, CPuint nSize)
{
    return KD_EINVAL;
}

CPresult StreamingPipe::WriteBuffer(CPbyte *pBuffer, CPuint nFilledSize)
{
    return KD_EINVAL;
}

CPresult StreamingPipe::RegisterCallback(
        CPresult (*_ClientCallback)(CP_EVENTTYPE eEvent, CPuint iParam))
{

    if(ClientCallback == NULL)
        return KD_EINVAL;

    ClientCallback = _ClientCallback;

    return STREAMING_PIPE_SUCESS;
}


