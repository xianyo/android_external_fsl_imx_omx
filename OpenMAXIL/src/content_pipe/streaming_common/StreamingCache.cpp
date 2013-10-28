/**
 *  Copyright (c) 2012-2013, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "StreamingCache.h"
#include "Mem.h"
#include "Log.h"

#if 0
#undef LOG_DEBUG
#define LOG_DEBUG printf
#endif

static void *DoReadStream(void *arg) 
{
    StreamingCache *h = (StreamingCache *) arg;
    LOG_DEBUG("StreamingCache:DoReadStream\n");
    while(OMX_ErrorNone == h->GetStreamData());
    return NULL;
}

OMX_BOOL GetStopReadingCallback(OMX_PTR handle)
{
    StreamingCache * pCache = (StreamingCache *)handle;
    return (pCache->bStopThread == OMX_TRUE) || (pCache->bReset == OMX_TRUE) ? OMX_TRUE : OMX_FALSE;
}

StreamingCache::StreamingCache()
{
    mBlockSize = 2*1024;
    mBlockCnt = 0;
    mCacheBuffer = NULL;
    mCacheNodeRoot = NULL;
    mContentLength = mOffsetStart = mOffsetEnd = 0;
    mStartNode = mEndNode = 0;
    SemReadStream = NULL;
    pThread = NULL;
    bStopThread = OMX_FALSE;
    bStreamEOS = OMX_FALSE;
    bReset = OMX_FALSE;
    mUsedBlockCnt = 0;
    pProtocol = NULL;
    mutex = NULL;
}

OMX_ERRORTYPE StreamingCache::Init(StreamingProtocol * protocol, OMX_U32 nCacheSize)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_S32 i;
    CacheNode *pNode = NULL;

    LOG_DEBUG("StreamingCache::Create Block size %d, block num %d\n", mBlockSize, mBlockCnt);

    if(protocol == NULL)
        return OMX_ErrorUndefined;

    if(nCacheSize < mBlockSize)
        return OMX_ErrorBadParameter;

    pProtocol = protocol;
    pProtocol->SetCallback(GetStopReadingCallback, (OMX_PTR)this);

    ret = pProtocol->Open();
    if (ret != OMX_ErrorNone) {
        LOG_ERROR("Connect failed.\n");
        goto err;
    }

    mContentLength = pProtocol->GetContentLength();
    LOG_DEBUG("Content Length: %lld\n", mContentLength);

    mBlockCnt = nCacheSize/mBlockSize;
    mCacheBuffer = FSL_MALLOC(mBlockSize * mBlockCnt);
    if(mCacheBuffer == NULL) {
        LOG_ERROR("Failed to allocate memory for StreamingCache, size %d\n", mBlockSize * mBlockCnt);
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    mCacheNodeRoot = (CacheNode*) FSL_MALLOC(mBlockCnt * sizeof(CacheNode));
    if(mCacheNodeRoot == NULL) {
        LOG_ERROR("Failed to allocate memory for StreamingCache node.\n");
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    pNode = mCacheNodeRoot;
    for(i=0; i<(OMX_S32)mBlockCnt; i++) {
        pNode->nOffset = 0;
        pNode->nLength = 0;
        pNode->pData = (OMX_PTR)((OMX_U32)mCacheBuffer + i * mBlockSize);
        pNode->nHeaderLen = 0;
        pNode ++;
    }

    if(E_FSL_OSAL_SUCCESS != fsl_osal_sem_init(&SemReadStream, 0, mBlockCnt)) {
        LOG_ERROR("Failed create Semphore for StreamingCache.\n");
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    if(E_FSL_OSAL_SUCCESS != fsl_osal_mutex_init(&mutex, fsl_osal_mutex_default)) {
        LOG_ERROR("Failed create mutex for StreamingCache.\n");
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    if(E_FSL_OSAL_SUCCESS != fsl_osal_thread_create(&pThread, NULL, DoReadStream, this)) {
        LOG_ERROR("Failed create Thread for StreamingCache.\n");
        ret = OMX_ErrorInsufficientResources;
        goto err;
    }

    return OMX_ErrorNone;

err:
    DeInit();
    return ret;
}

OMX_ERRORTYPE StreamingCache::DeInit()
{
    LOG_DEBUG("StreamingCache::%s\n", __FUNCTION__);

    if(pThread != NULL) {
        bStopThread = OMX_TRUE;
        fsl_osal_sem_post(SemReadStream);
        fsl_osal_thread_destroy(pThread);
        pThread = NULL;
    }

    if(SemReadStream != NULL) {
        fsl_osal_sem_destroy(SemReadStream);
        SemReadStream = NULL;
    }

    if(mutex != NULL) {
        fsl_osal_mutex_destroy(mutex);
        mutex = NULL;
    }

    pProtocol->Close();

    if(mCacheNodeRoot != NULL) {
        FSL_FREE(mCacheNodeRoot);
    }

    if(mCacheBuffer != NULL) {
        FSL_FREE(mCacheBuffer);
    }

    return OMX_ErrorNone;
}

OMX_U64 StreamingCache::GetContentLength()
{
    LOG_DEBUG("%s\n", __FUNCTION__);
    return mContentLength;
}

OMX_S32 StreamingCache::ReadAt(
        OMX_U64 nOffset, 
        OMX_U32 nSize, 
        OMX_PTR pBuffer)
{
    LOG_DEBUG("Read data to %p size %d offset %lld\n", pBuffer, nSize, nOffset);

    OMX_U64 mLength = (bStreamEOS == OMX_TRUE) ? mOffsetEnd : mContentLength;
    if(mLength > 0) {
        OMX_S64 nLeft = mLength - nOffset;
        if(nLeft <= 0){
            printf("!! nLeft <= 0");
            return 0;
    	}

        if(nSize > nLeft)
            nSize = nLeft;
    }

    if(OMX_TRUE != AvailableAt(nOffset, nSize)) {
        printf("!! Not enough data at [%lld:%d]\n", nOffset, nSize);
        return 0;
    }

    CacheNode *pNode = mCacheNodeRoot + mStartNode;
    //LOG_DEBUG("Cache node [%d], [%lld:%d]\n", mStartNode, pNode->nOffset, pNode->nLength);
    while(nOffset > pNode->nOffset + pNode->nLength) {
        mStartNode ++;
        pNode ++;
        if(mStartNode >= mBlockCnt) {
            mStartNode = 0;
            pNode = mCacheNodeRoot;
        }
        fsl_osal_mutex_lock(mutex);
        mUsedBlockCnt--;
        fsl_osal_mutex_unlock(mutex);
        fsl_osal_sem_post(SemReadStream);
        //LOG_DEBUG("Cache node [%d], [%lld:%d]\n", mStartNode, pNode->nOffset, pNode->nLength);
    }

    //LOG_DEBUG("Read from cache node %d, offset=%lld, length=%d\n", mStartNode, pNode->nOffset, pNode->nLength);

    if((nOffset + nSize) <= (pNode->nOffset + pNode->nLength)) {
        OMX_PTR src = (OMX_PTR)((OMX_U32)pNode->pData + pNode->nHeaderLen + (nOffset - pNode->nOffset));
        fsl_osal_memcpy(pBuffer, src, nSize);
        //printf("1:Copy from cache node %d, nOffset=%lld, nSize=%d\n", mStartNode, nOffset, nSize);
    }
    else {
        OMX_PTR src = (OMX_PTR)((OMX_U32)pNode->pData + pNode->nHeaderLen + (nOffset - pNode->nOffset));
        OMX_U32 left = pNode->nLength - (nOffset - pNode->nOffset);
        fsl_osal_memcpy(pBuffer, src, left);
        //printf("2:Copy from cache node %d, offset=%lld, length=%d\n", mStartNode, nOffset, nSize);
        //printf("2:Copy from cache node %d, offset=%lld, length=%d\n", mStartNode, nOffset, left);

        OMX_U64 offset = nOffset + left;
        OMX_U32 size = nSize - left;
        OMX_PTR dest = (OMX_PTR)((OMX_U32)pBuffer + left);
        do {
            mStartNode ++;
            pNode ++;
            if(mStartNode >= mBlockCnt) {
                mStartNode = 0;
                pNode = mCacheNodeRoot;
            }
            fsl_osal_mutex_lock(mutex);
            mUsedBlockCnt--;
            fsl_osal_mutex_unlock(mutex);
            fsl_osal_sem_post(SemReadStream);
            //LOG_DEBUG("Cache node [%d], [%lld:%d]\n", mStartNode, pNode->nOffset, pNode->nLength);

            OMX_U32 copy = size >= pNode->nLength ? pNode->nLength : size;
            fsl_osal_memcpy(dest, (OMX_U8*)pNode->pData + pNode->nHeaderLen, copy);
            //printf("2:Copy from cache node %d, offset=%lld, length=%d\n", mStartNode, nOffset, copy);
            offset += copy;
            size -= copy;
            dest = (OMX_PTR)((OMX_U32)dest + copy);
        } while(size > 0);
    }

    LOG_DEBUG("Read data ok from cache at [%lld:%d] %x\n", nOffset, nSize, *(unsigned char *)pBuffer);

    mOffsetStart = nOffset + nSize;

    return nSize;
}

OMX_BOOL StreamingCache::AvailableAt(
        OMX_U64 nOffset, 
        OMX_U32 nSize)
{
    if(bStreamEOS == OMX_TRUE)
        return OMX_TRUE;

    LOG_DEBUG("StreamingCache::AvailableAt nOffset %lld, nSize %d\n", nOffset, nSize);

    OMX_U32 nTotal = mBlockCnt * mBlockSize;
    if(nSize > nTotal) {
        LOG_DEBUG("CheckAvailableBytes %d larger than cache total size %d, ajust to %d\n", nSize, nTotal, nTotal);
        nSize = nTotal;
    }

    if(mContentLength > 0) {
        OMX_U64 nLeft = mContentLength - nOffset;
        if(nSize > nLeft) {
            LOG_DEBUG("CheckAvailableBytes %d larger than left %d, adjust to %d\n", nSize, nLeft, nLeft);
            nSize = nLeft;
        }
    }

    CacheNode *pNode = mCacheNodeRoot + mStartNode;
    //LOG_DEBUG("CheckAvailableBytes at [%lld:%d], current: [%lld:%d]\n",
    //        nOffset, nSize, pNode->nOffset, mOffsetEnd - pNode->nOffset);

    if(nOffset < pNode->nOffset || nOffset > mOffsetEnd) {
        LOG_DEBUG("Reset Cache to %lld:%d, current Node %d [%lld:%lld]\n", nOffset, nSize, mStartNode, pNode->nOffset, mOffsetEnd);
        ResetCache(nOffset);
        return OMX_FALSE;
    }

    //LOG_DEBUG("nOffset %lld, nSize %d, mOffsetEnd %lld\n", nOffset, nSize, mOffsetEnd);

    if(nOffset + nSize > mOffsetEnd) {
        while(nOffset > pNode->nOffset + pNode->nLength) {
            mStartNode ++;
            pNode ++;
            if(mStartNode >= mBlockCnt) {
                mStartNode = 0;
                pNode = mCacheNodeRoot;
            }
            fsl_osal_mutex_lock(mutex);
            mUsedBlockCnt--;
            fsl_osal_mutex_unlock(mutex);
            fsl_osal_sem_post(SemReadStream);
        }
        return OMX_FALSE;
    }

    return OMX_TRUE;
}

OMX_S32 StreamingCache::AvailableBytes(OMX_U64 nOffset)
{
    //printf("AvailableBytes() nOffset %lld, mOffsetEnd %lld\n", nOffset, mOffsetEnd);
    if(mOffsetEnd <= nOffset)
        return 0;

    return mOffsetEnd - nOffset;
}

OMX_ERRORTYPE StreamingCache::GetStreamData()
{
    CacheNode *pNode = NULL;

    //static int count = 0;
    //count++;
    //if(((count %50) == 0) || (mUsedBlockCnt < 30) || ((mBlockCnt - mUsedBlockCnt) < 30))
    //if((mUsedBlockCnt < 30) || ((mBlockCnt - mUsedBlockCnt) < 30))
    //        printf("%s, bReset %d, mBlockCnt %d, Used %d\n", __FUNCTION__, bReset, mBlockCnt, mUsedBlockCnt);

    fsl_osal_sem_wait(SemReadStream);
    
    if(bStopThread == OMX_TRUE)
        return OMX_ErrorNoMore;

    //OMX_U32 nFreeCache = mBlockSize * mBlockCnt - (mOffsetEnd - mOffsetStart);
    OMX_U32 nFreeCache = mBlockSize * (mBlockCnt - mUsedBlockCnt);
    if(nFreeCache < mBlockSize) {
        LOG_DEBUG("No free cache to read, nFreeCache: %d, mBlockSize: %d.\n", nFreeCache, mBlockSize);
        fsl_osal_sleep(100000);
        return OMX_ErrorNone;
    }

    if(mContentLength > 0 && mOffsetEnd >= mContentLength) {
        fsl_osal_sleep(1000000);
        return OMX_ErrorNone;
    }

    if(bReset == OMX_TRUE) {
        bReset = OMX_FALSE;
        pProtocol->Seek(mOffsetStart, SEEK_SET);
    }

    pNode = mCacheNodeRoot + mEndNode;
    pNode->nOffset = mOffsetEnd;
    pNode->nLength = 0;
    pNode->nHeaderLen = 0;

    LOG_DEBUG("+++++++Want Fill cache node [%d] at offset %d\n", mEndNode, mOffsetEnd);

    OMX_U8 *pBuffer = (OMX_U8*)pNode->pData;
    OMX_S32 nWant = mBlockSize;
    if((mContentLength > 0) && (mOffsetEnd + nWant > mContentLength))
        nWant = mContentLength - mOffsetEnd;

    OMX_U32 headerLen = 0, nFilled = 0;
    OMX_ERRORTYPE err = pProtocol->Read((unsigned char*)pBuffer, nWant, &bStreamEOS, mOffsetEnd, &headerLen, &nFilled);
    
    LOG_DEBUG("url_read: nWant %d, eos %d, headerLen %d, nFilled %d\n", nWant, bStreamEOS, headerLen, nFilled);
#if 0
    if(nFilled>0){
        static int iiCnt=0;
        static FILE *pfTest = NULL;
        iiCnt ++;
        if (iiCnt==1) {
            pfTest = fopen("/data/autotest/dumpdata_udp_loop.ts", "wb");
            if(pfTest == NULL)
                printf("Unable to open dump file! \n");
        }
        if(iiCnt > 0 && pfTest != NULL) {        
                //printf("dump data %d\n", n);
                fwrite(pBuffer + headerLen, sizeof(char), nFilled - headerLen, pfTest);
                fflush(pfTest);
                ////fclose(pfTest);
                //pfTest = NULL;
         }
    }
#endif

    if(bStopThread == OMX_TRUE)
        return OMX_ErrorUndefined;

    if(bReset == OMX_TRUE)
        return OMX_ErrorNone;

    if(err != OMX_ErrorNone) {
        printf("read source failed, ret = %x\n", err);
        if(bStreamEOS == OMX_TRUE) {
            return OMX_ErrorUndefined;
        }
    }

    if(nFilled > 0) {
        pNode->nLength = nFilled - headerLen;
        pNode->nHeaderLen = headerLen;
        mOffsetEnd += nFilled - headerLen;
        //LOG_DEBUG("%s fill cache node [%d], [%lld:%d]\n", __FUNCTION__, mEndNode, pNode->nOffset, pNode->nLength);

        mEndNode ++;
        if(mEndNode >= mBlockCnt)
            mEndNode = 0;

        fsl_osal_mutex_lock(mutex);
        mUsedBlockCnt++;
        fsl_osal_mutex_unlock(mutex);
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE StreamingCache::ResetCache(OMX_U64 nOffset)
{
    printf("ResetCache!!!\n");

    while(E_FSL_OSAL_SUCCESS == fsl_osal_sem_trywait(SemReadStream));

    OMX_S32 i;
    for(i=0; i<(OMX_S32)mBlockCnt; i++) {
        mCacheNodeRoot[i].nOffset = 0;
        mCacheNodeRoot[i].nLength = 0;
        mCacheNodeRoot[i].nHeaderLen = 0;
    }

    mOffsetStart = mOffsetEnd = nOffset;
    mCacheNodeRoot[0].nOffset = nOffset;
    mStartNode = mEndNode = 0;
    mUsedBlockCnt = 0;
    bReset = OMX_TRUE;

    for(i=0; i<(OMX_S32)mBlockCnt; i++)
        fsl_osal_sem_post(SemReadStream);


    return OMX_ErrorNone;
}

