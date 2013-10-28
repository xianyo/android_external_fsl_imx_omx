/**
 *  Copyright (c) 2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "HttpsProtocol.h"
#include "libavformat/http.h"
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "fsl_osal.h"
#include "Mem.h"
#include "Log.h"

extern URLProtocol ff_http_protocol;
extern URLProtocol ff_mmsh_protocol;

HttpsProtocol::HttpsProtocol(OMX_STRING url) : StreamingProtocol(url)
{
    if(szURL == NULL){
        LOG_ERROR("[HttpsProtocol] url is NULL\n");
        return;
    }
    if(fsl_osal_strncmp(szURL, "http://", 7) == 0)
        pURLProtocol = &ff_http_protocol;
    else if(fsl_osal_strncmp(szURL, "mms://", 6) == 0)
        pURLProtocol = &ff_mmsh_protocol;
    else
        LOG_ERROR("[HttpsProtocol] incorrect url prefix %s", __FUNCTION__, szURL);
}

OMX_ERRORTYPE HttpsProtocol::Open	()
{
    OMX_STRING URL, mURL, mHeader;
    URL = mURL = mHeader = NULL;

    if(pURLProtocol == NULL){
        LOG_ERROR("URL Protocol not assigned.\n");
        return OMX_ErrorUndefined;
    }

    av_register_all();
    //avio_set_interrupt_cb(abort_cb);

    OMX_S32 pos = UrlHeaderPos(szURL);
    if(pos > 0) {
        OMX_S32 url_len = fsl_osal_strlen(szURL) + 1;
        URL = (OMX_STRING)FSL_MALLOC(url_len);
        if(URL == NULL)
            return OMX_ErrorInsufficientResources;
        fsl_osal_strcpy(URL, szURL);
        URL[pos - 1] = '\0';
        mURL = URL;
        mHeader = URL + pos;
    }
    else
        mURL = szURL;

    LOG_DEBUG("mURL: %s, mHeader: %s\n", mURL, mHeader);

    uc = (URLContext*)FSL_MALLOC(sizeof(URLContext) + fsl_osal_strlen(mURL) + 1);
    if(uc == NULL) {
        FSL_FREE(URL);
        LOG_ERROR("Allocate for URLContext failed.\n");
        return OMX_ErrorInsufficientResources;
    }

    uc->filename = (char *) &uc[1];
    fsl_osal_strcpy(uc->filename, mURL);
    uc->flags = AVIO_RDONLY;
    uc->is_streamed = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */
    uc->priv_data = NULL;
    uc->prot = pURLProtocol;
        
    LOG_DEBUG("priv_data_size %d\n", uc->prot->priv_data_size);
    if (uc->prot->priv_data_size) {
    	printf("allocate priv_data\n");
        uc->priv_data = FSL_MALLOC(uc->prot->priv_data_size);
        if(uc->priv_data == NULL) {
            FSL_FREE(uc);
            FSL_FREE(URL);
            return OMX_ErrorInsufficientResources;
        }
        fsl_osal_memset(uc->priv_data, 0, uc->prot->priv_data_size);
        if (uc->prot->priv_data_class) {
            *(const AVClass**)uc->priv_data = uc->prot->priv_data_class;
            av_opt_set_defaults(uc->priv_data);
        }
    }

    if(mHeader)
        ff_http_set_headers(uc, mHeader);

    FSL_FREE(URL);

    LOG_DEBUG("Connecting %s\n", uc->filename);

    int err = pURLProtocol->url_open(uc, uc->filename, uc->flags);
    if (err) {
        LOG_ERROR("Connect %s failed.\n", uc->filename);
        FSL_FREE(uc->priv_data);
        FSL_FREE(uc);
        return OMX_ErrorUndefined;
    }

    uc->is_connected = 1;
    LOG_DEBUG("Connect to %s success.\n", uc->filename);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE HttpsProtocol::Read(unsigned char* pBuffer, OMX_U32 nWant, OMX_BOOL * bStreamEOS, OMX_U64 offsetEnd, OMX_U32 *headerLen, OMX_U32 *nFilled)
{
    LOG_DEBUG("[HttpsProtocol] Read() pBuffer %p, nWant %d, offsetEnd %lld\n", pBuffer, nWant, offsetEnd);

    int read = 0;
    while(nWant > 0){
        if(GetStopReading(callbackArg) == OMX_TRUE){
            printf("[HttpsProtocol] Read() exit from callback\n");
            return OMX_ErrorNone;
        }

        read = pURLProtocol->url_read(uc, pBuffer, nWant);
        if(read <= 0){
            printf("[HttpsProtocol] Read() url_read fail %d\n", read);

            if(GetHandleError() == OMX_TRUE){
                *bStreamEOS = OMX_TRUE;
                printf("[HttpsProtocol] Read() exit from GetHandleError\n");
                return OMX_ErrorUndefined;
            }
            
            fsl_osal_sleep(1000000);

            while (pURLProtocol->url_seek(uc, offsetEnd, SEEK_SET) < 0) {
                printf("[HttpsProtocol] Read() seek to %lld fail\n", offsetEnd);

                fsl_osal_sleep(1000000);
                if(GetStopReading(callbackArg) == OMX_TRUE){
                    printf("[HttpsProtocol] Read() stop seeking from callback\n");
                    return OMX_ErrorNone;
                }
            }
        }
        else if((OMX_U32)read <= nWant){
            pBuffer += read;
            nWant -= read;
            offsetEnd += read;
            *nFilled += read;
        }
    }
    
    *headerLen = 0;
    return OMX_ErrorNone;
}

OMX_ERRORTYPE HttpsProtocol::Seek(OMX_U64 targetOffset, int whence)
{
    LOG_DEBUG("[HttpsProtocol] Seek() to %lld , whence %d\n", targetOffset, whence);

    OMX_S64 location = pURLProtocol->url_seek(uc, targetOffset, whence);
    if(location < 0) {
        LOG_ERROR("[HttpsProtocol] seek() to %lld error, ret: %lld\n", targetOffset, location);
        return OMX_ErrorUndefined;
    }
    else if(location < (OMX_S64)targetOffset) {
        printf("[HttpsProtocol] seek() not correct, request: %lld, return: %lld\n", targetOffset, location);
        OMX_U8 buffer[512];
        OMX_S32 diff = targetOffset - location;
        while(diff > 0) {
            if(GetStopReading(callbackArg) == OMX_TRUE){
                printf("[HttpsProtocol] Seek() exit for callback\n");
                return OMX_ErrorNone;
            }
            OMX_S32 read = pURLProtocol->url_read(uc, (unsigned char*)buffer, diff > 512 ? 512 : diff);
            if(read > 0)
                diff -= read;
        }
    }
    return OMX_ErrorNone;
}

OMX_BOOL HttpsProtocol::GetHandleError()
{
    if(GetContentLength() == 0 && (fsl_osal_strncmp(szURL, "http://localhost", 16) == 0))
        return OMX_TRUE;
    else
        return OMX_FALSE;
}

OMX_S32 HttpsProtocol::UrlHeaderPos(OMX_STRING url) 
{
    OMX_S32 len = fsl_osal_strlen(url) + 1;
    OMX_S32 i;
    OMX_BOOL bHasHeader = OMX_FALSE;
    for(i=0; i<len; i++) {
        if(url[i] == '\n') {
            bHasHeader = OMX_TRUE;
            break;
        }
    }

    if(bHasHeader == OMX_TRUE)
        return i+1;
    else
        return 0;
}

