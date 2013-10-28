/**
 *  Copyright (c) 2012-2013, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#ifndef STREAMING_CACHE_H
#define STREAMING_CACHE_H

#include "fsl_osal.h"
#include "OMX_Core.h"
#include "avformat.h"
#include "StreamingProtocol.h"

class StreamingCache {
    public:
        StreamingCache();
        OMX_ERRORTYPE Init(StreamingProtocol * protocol, OMX_U32 nCacheSize);
        OMX_ERRORTYPE DeInit();
        OMX_U64 GetContentLength();
        OMX_S32 ReadAt(OMX_U64 nOffset, OMX_U32 nSize, OMX_PTR pBuffer);
        OMX_BOOL AvailableAt(OMX_U64 nOffset, OMX_U32 nSize);
        OMX_S32 AvailableBytes(OMX_U64 nOffset);
        OMX_ERRORTYPE GetStreamData();

    friend OMX_BOOL GetStopReadingCallback(OMX_PTR pHandle);

    private:
        typedef struct{
            OMX_U64 nOffset;
            OMX_U32 nLength;
            OMX_PTR pData;
            OMX_U32 nHeaderLen;
        } CacheNode;

        OMX_U32 mBlockSize;
        OMX_U32 mBlockCnt, mUsedBlockCnt;
        OMX_PTR mCacheBuffer;
        CacheNode *mCacheNodeRoot;
        OMX_U64 mContentLength;
        OMX_U64 mOffsetEnd;
        OMX_U64 mOffsetStart;
        OMX_U32 mStartNode;
        OMX_U32 mEndNode;
        fsl_osal_sem SemReadStream;
        OMX_PTR pThread;
        OMX_BOOL bStopThread;
        OMX_BOOL bStreamEOS;
        OMX_BOOL bReset;
        fsl_osal_mutex mutex;
        StreamingProtocol * pProtocol;

        OMX_ERRORTYPE ResetCache(OMX_U64 nOffset);
};

#endif
/* File EOF */

