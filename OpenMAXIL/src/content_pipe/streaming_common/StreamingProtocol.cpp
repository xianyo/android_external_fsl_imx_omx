/**
 *  Copyright (c) 2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "StreamingProtocol.h"
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "fsl_osal.h"
#include "Mem.h"
#include "Log.h"

#if 0
#undef LOG_DEBUG
#define LOG_DEBUG printf
#endif

static OMX_BOOL bAbort = OMX_FALSE;
static int abort_cb(void)
{
    if(bAbort == OMX_TRUE) {
        bAbort = OMX_FALSE;
        return 1;
    }
    return 0;
}

StreamingProtocol::StreamingProtocol(OMX_STRING url)
{
    pURLProtocol = NULL;
    szURL = url;
    uc = NULL;
}

OMX_ERRORTYPE StreamingProtocol::Open	()
{
    OMX_STRING mURL, mHeader;
    mURL = mHeader = NULL;

    if(pURLProtocol == NULL){
        LOG_ERROR("URL Protocol not assigned.\n");
        return OMX_ErrorUndefined;
    }

    av_register_all();
    //avio_set_interrupt_cb(abort_cb);

    mURL = szURL;

    LOG_DEBUG("mURL: %s, mHeader: %s\n", mURL, mHeader);

    uc = (URLContext*)FSL_MALLOC(sizeof(URLContext) + fsl_osal_strlen(mURL) + 1);
    if(uc == NULL) {
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
            return OMX_ErrorInsufficientResources;
        }
        fsl_osal_memset(uc->priv_data, 0, uc->prot->priv_data_size);
        if (uc->prot->priv_data_class) {
            *(const AVClass**)uc->priv_data = uc->prot->priv_data_class;
            av_opt_set_defaults(uc->priv_data);
        }
    }

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

OMX_ERRORTYPE StreamingProtocol::Close()
{
    bAbort = OMX_TRUE;
    if(uc != NULL) {
        pURLProtocol->url_close(uc);
        if (uc->prot->priv_data_size && uc->priv_data) 
            FSL_FREE(uc->priv_data);
        FSL_FREE(uc);
    }
    return OMX_ErrorNone;
}

OMX_U64 StreamingProtocol::GetContentLength()
{
    OMX_U64 len = 0;
    if(pURLProtocol->url_seek){
        len = pURLProtocol->url_seek(uc, 0, AVSEEK_SIZE);
        if(len == (OMX_U64)-1)
            len = 0;
    }
    return len;
}

void StreamingProtocol::SetCallback(GetStopReadingFunction f, OMX_PTR handle)
{
    GetStopReading = f;
    callbackArg = handle;
}

OMX_ERRORTYPE StreamingProtocol::Seek(OMX_U64 offset, int whence)
{
    return OMX_ErrorNone;
}


