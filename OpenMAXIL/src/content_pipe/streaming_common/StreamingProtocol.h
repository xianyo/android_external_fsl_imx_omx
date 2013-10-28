/**
 *  Copyright (c) 2012, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#ifndef STREAMING_RPOTOCOL_H
#define STREAMING_RPOTOCOL_H


#include "OMX_Types.h"
#include "OMX_Core.h"
#include "avio.h"

typedef OMX_BOOL (*GetStopReadingFunction)(OMX_PTR handle);   

class StreamingProtocol
{
protected:
    URLContext *uc;
    URLProtocol * pURLProtocol;
    GetStopReadingFunction GetStopReading;
    OMX_PTR callbackArg;
    OMX_STRING szURL;

public:
    StreamingProtocol(OMX_STRING url);
    virtual ~StreamingProtocol(){};
    virtual OMX_ERRORTYPE Open();
	virtual OMX_ERRORTYPE Close() ;
    virtual OMX_U64 GetContentLength() ;
    virtual OMX_ERRORTYPE Seek(OMX_U64 offset, int whence);
    virtual OMX_ERRORTYPE Read(unsigned char* pBuffer, OMX_U32 nWant, OMX_BOOL * streamEOS, OMX_U64 offsetEnd, OMX_U32 *headerLen, OMX_U32 *nFilled) = 0;
    void SetCallback(GetStopReadingFunction f, OMX_PTR handle);

};

#endif

