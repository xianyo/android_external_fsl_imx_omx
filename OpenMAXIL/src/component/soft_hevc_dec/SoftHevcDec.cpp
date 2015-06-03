/**
 *  Copyright (c) 2014, Freescale Semiconductor Inc.,
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of Freescale Semiconductor Inc.,
 *  and contain its proprietary and confidential information.
 *
 */

#include "SoftHevcDec.h"


//#undef LOG_LOG
//#define LOG_LOG printf
//#undef LOG_DEBUG
//#define LOG_DEBUG printf


#define ALIGN_STRIDE(x)  (((x)+31)&(~31))
#define ALIGN_CHROMA(x) (x)

#define DROP_B_THRESHOLD    30000

#define COMPONENT_NAME "OMX.Freescale.std.video_decoder.soft_hevc.sw-based"
#define ivd_aligned_malloc(alignment, size) memalign(alignment, size)
#define ivd_aligned_free(buf) free(buf)

/** Function and structure definitions to keep code similar for each codec */
#define ivdec_api_function              ihevcd_cxa_api_function
#define ivdext_init_ip_t                ihevcd_cxa_init_ip_t
#define ivdext_init_op_t                ihevcd_cxa_init_op_t
#define ivdext_fill_mem_rec_ip_t        ihevcd_cxa_fill_mem_rec_ip_t
#define ivdext_fill_mem_rec_op_t        ihevcd_cxa_fill_mem_rec_op_t
#define ivdext_ctl_set_num_cores_ip_t   ihevcd_cxa_ctl_set_num_cores_ip_t
#define ivdext_ctl_set_num_cores_op_t   ihevcd_cxa_ctl_set_num_cores_op_t

#define IVDEXT_CMD_CTL_SET_NUM_CORES    \
        (IVD_CONTROL_API_COMMAND_TYPE_T)IHEVCD_CXA_CMD_CTL_SET_NUM_CORES

SoftHevcDec::SoftHevcDec()
{
    int n;

    fsl_osal_strcpy((fsl_osal_char*)name, COMPONENT_NAME);
    ComponentVersion.s.nVersionMajor = 0x0;
    ComponentVersion.s.nVersionMinor = 0x1;
    ComponentVersion.s.nRevision = 0x0;
    ComponentVersion.s.nStep = 0x0;
    role_cnt = 1;
    role[0] = (OMX_STRING)"video_decoder.hevc";

    fsl_osal_memset(&sInFmt, 0, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
    sInFmt.nFrameWidth = 320;
    sInFmt.nFrameHeight = 240;
    sInFmt.xFramerate = 30 * Q16_SHIFT;
    sInFmt.eColorFormat = OMX_COLOR_FormatUnused;
    sInFmt.eCompressionFormat = (OMX_VIDEO_CODINGTYPE)OMX_VIDEO_CodingHEVC;

    nInPortFormatCnt = 0;
    nOutPortFormatCnt = 1;
    eOutPortPormat[0] = OMX_COLOR_FormatYUV420Planar;

    sOutFmt.nFrameWidth = 320;
    sOutFmt.nFrameHeight = 240;
    sOutFmt.eColorFormat = OMX_COLOR_FormatYUV420Planar;
    sOutFmt.eCompressionFormat = OMX_VIDEO_CodingUnused;

    bFilterSupportPartilInput = OMX_FALSE;
    nInBufferCnt = 8;
    nInBufferSize = 1024*1024;
    nOutBufferCnt = 3;
    nOutBufferSize = sOutFmt.nFrameWidth * sOutFmt.nFrameHeight \
                     * pxlfmt2bpp(sOutFmt.eColorFormat) / 8;

    pInBuffer = pOutBuffer = NULL;
    nInputSize = nInputOffset = 0;
    bInEos = OMX_FALSE;
    bOutEos = OMX_FALSE;

    nWidth = 320;
    nHeight = 240;

#if defined(_SC_NPROCESSORS_ONLN)
    n  = sysconf(_SC_NPROCESSORS_ONLN);
#else
    // _SC_NPROC_ONLN must be defined...
    n  = sysconf(_SC_NPROC_ONLN);
#endif

    if (n < 1)
        n = 1;

    nNumCores = n;

    OMX_INIT_STRUCT(&sOutCrop, OMX_CONFIG_RECTTYPE);
    sOutCrop.nPortIndex = OUT_PORT;
    sOutCrop.nLeft = sOutCrop.nTop = 0;
    sOutCrop.nWidth = sInFmt.nFrameWidth;
    sOutCrop.nHeight = sInFmt.nFrameHeight;

    CodingType = (OMX_VIDEO_CODINGTYPE)OMX_VIDEO_CodingUnused;
    pClock = NULL;

    eDecState = HEVC_DEC_INIT;
    LOG_LOG("SoftHevcDec::SoftHevcDec");

}

OMX_ERRORTYPE SoftHevcDec::GetConfig(OMX_INDEXTYPE nParamIndex, OMX_PTR pComponentParameterStructure)
{
    if(nParamIndex == OMX_IndexConfigCommonOutputCrop) {
        OMX_CONFIG_RECTTYPE *pRecConf = (OMX_CONFIG_RECTTYPE*)pComponentParameterStructure;
        if(pRecConf->nPortIndex == OUT_PORT) {
            pRecConf->nTop = sOutCrop.nTop;
            pRecConf->nLeft = sOutCrop.nLeft;
            pRecConf->nWidth = sOutCrop.nWidth;
            pRecConf->nHeight = sOutCrop.nHeight;
        }
        return OMX_ErrorNone;
    }
    else
        return OMX_ErrorUnsupportedIndex;
}

OMX_ERRORTYPE SoftHevcDec::SetConfig(OMX_INDEXTYPE nIndex, OMX_PTR pComponentConfigStructure)
{
    OMX_CONFIG_CLOCK *pC;

    switch((int)nIndex)
    {
        case OMX_IndexConfigClock:
            pC = (OMX_CONFIG_CLOCK*) pComponentConfigStructure;
            pClock = pC->hClock;
            break;
        default:
            break;
    }
    return OMX_ErrorNone;
}

void SoftHevcDec::logVersion() {
    ivd_ctl_getversioninfo_ip_t s_ctl_ip;
    ivd_ctl_getversioninfo_op_t s_ctl_op;
    UWORD8 au1_buf[512];
    IV_API_CALL_STATUS_T status;

    s_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_ctl_ip.e_sub_cmd = IVD_CMD_CTL_GETVERSION;
    s_ctl_ip.u4_size = sizeof(ivd_ctl_getversioninfo_ip_t);
    s_ctl_op.u4_size = sizeof(ivd_ctl_getversioninfo_op_t);
    s_ctl_ip.pv_version_buffer = au1_buf;
    s_ctl_ip.u4_version_buffer_size = sizeof(au1_buf);

    status = ivdec_api_function(mCodecCtx, (void *)&s_ctl_ip,
            (void *)&s_ctl_op);

    if (status != IV_SUCCESS) {
        LOG_ERROR("Error in getting version number: 0x%x",
                s_ctl_op.u4_error_code);
    } else {
        LOG_DEBUG("Ittiam decoder version number: %s",
                (char *)s_ctl_ip.pv_version_buffer);
    }

    return;
}

OMX_ERRORTYPE SoftHevcDec::setNumCores() {
    ivdext_ctl_set_num_cores_ip_t s_set_cores_ip;
    ivdext_ctl_set_num_cores_op_t s_set_cores_op;
    IV_API_CALL_STATUS_T status;
    s_set_cores_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_set_cores_ip.e_sub_cmd = IVDEXT_CMD_CTL_SET_NUM_CORES;
    s_set_cores_ip.u4_num_cores = MIN(nNumCores, 4);
    s_set_cores_ip.u4_size = sizeof(ivdext_ctl_set_num_cores_ip_t);
    s_set_cores_op.u4_size = sizeof(ivdext_ctl_set_num_cores_op_t);
    LOG_DEBUG("Set number of cores to %u", s_set_cores_ip.u4_num_cores);
    status = ivdec_api_function(mCodecCtx, (void *)&s_set_cores_ip,
            (void *)&s_set_cores_op);
    if (IV_SUCCESS != status) {
        LOG_ERROR("Error in setting number of cores: 0x%x",
                s_set_cores_op.u4_error_code);
        return OMX_ErrorUndefined;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::setDisplayStride(OMX_U32 stride) {

    ivd_ctl_set_config_ip_t s_ctl_ip;
    ivd_ctl_set_config_op_t s_ctl_op;
    IV_API_CALL_STATUS_T status;

    s_ctl_ip.u4_disp_wd = (UWORD32)stride;
    s_ctl_ip.e_frm_skip_mode = IVD_SKIP_NONE;

    s_ctl_ip.e_frm_out_mode = IVD_DISPLAY_FRAME_OUT;
    s_ctl_ip.e_vid_dec_mode = IVD_DECODE_FRAME;
    s_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_ctl_ip.e_sub_cmd = IVD_CMD_CTL_SETPARAMS;
    s_ctl_ip.u4_size = sizeof(ivd_ctl_set_config_ip_t);
    s_ctl_op.u4_size = sizeof(ivd_ctl_set_config_op_t);

    LOG_DEBUG("Set the run-time (dynamic) parameters stride = %u", stride);
    status = ivdec_api_function(mCodecCtx, (void *)&s_ctl_ip,
            (void *)&s_ctl_op);

    if (status != IV_SUCCESS) {
        LOG_ERROR("Error in setting the run-time parameters: 0x%x",
                s_ctl_op.u4_error_code);

        return OMX_ErrorUndefined;
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::resetDecoder() {
    ivd_ctl_reset_ip_t s_ctl_ip;
    ivd_ctl_reset_op_t s_ctl_op;
    IV_API_CALL_STATUS_T status;

    s_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_ctl_ip.e_sub_cmd = IVD_CMD_CTL_RESET;
    s_ctl_ip.u4_size = sizeof(ivd_ctl_reset_ip_t);
    s_ctl_op.u4_size = sizeof(ivd_ctl_reset_op_t);

    status = ivdec_api_function(mCodecCtx, (void *)&s_ctl_ip,
            (void *)&s_ctl_op);
    if (IV_SUCCESS != status) {
        LOG_ERROR("Error in reset: 0x%x", s_ctl_op.u4_error_code);
        return OMX_ErrorUndefined;
    }

    /* Set the run-time (dynamic) parameters */
    setDisplayStride(nWidth);

    /* Set number of cores/threads to be used by the codec */
    setNumCores();

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::initDecoder()
{
    IV_API_CALL_STATUS_T status;

    UWORD32 u4_num_reorder_frames;
    UWORD32 u4_num_ref_frames;
    UWORD32 u4_share_disp_buf;
    WORD32 i4_level;

    /* Initialize number of ref and reorder modes (for HEVC) */
    u4_num_reorder_frames = 16;
    u4_num_ref_frames = 16;
    u4_share_disp_buf = 0;

    OMX_U32 displayStride = nWidth;;
    OMX_U32 displayHeight = nHeight;
    OMX_U32 displaySizeY = displayStride * displayHeight;

    if (displaySizeY > (1920 * 1088)) {
        i4_level = 50;
    } else if (displaySizeY > (1280 * 720)) {
        i4_level = 40;
    } else if (displaySizeY > (960 * 540)) {
        i4_level = 31;
    } else if (displaySizeY > (640 * 360)) {
        i4_level = 30;
    } else if (displaySizeY > (352 * 288)) {
        i4_level = 21;
    } else {
        i4_level = 20;
    }

    {
        iv_num_mem_rec_ip_t s_num_mem_rec_ip;
        iv_num_mem_rec_op_t s_num_mem_rec_op;

        s_num_mem_rec_ip.u4_size = sizeof(s_num_mem_rec_ip);
        s_num_mem_rec_op.u4_size = sizeof(s_num_mem_rec_op);
        s_num_mem_rec_ip.e_cmd = IV_CMD_GET_NUM_MEM_REC;

        LOG_DEBUG("initDecoder Get number of mem records");
        status = ivdec_api_function(mCodecCtx, (void*)&s_num_mem_rec_ip,
                (void*)&s_num_mem_rec_op);
        if (IV_SUCCESS != status) {
            LOG_ERROR("Error in getting mem records: 0x%x",
                    s_num_mem_rec_op.u4_error_code);
            return OMX_ErrorUndefined;
        }

        mNumMemRecords = s_num_mem_rec_op.u4_num_mem_rec;
    }

    mMemRecords = (iv_mem_rec_t*)ivd_aligned_malloc(
            128, mNumMemRecords * sizeof(iv_mem_rec_t));
    if (mMemRecords == NULL) {
        LOG_ERROR("Allocation failure");
        return OMX_ErrorUndefined;
    }

    memset(mMemRecords, 0, mNumMemRecords * sizeof(iv_mem_rec_t));

    {
        OMX_U32 i;
        ivdext_fill_mem_rec_ip_t s_fill_mem_ip;
        ivdext_fill_mem_rec_op_t s_fill_mem_op;
        iv_mem_rec_t *ps_mem_rec;

        s_fill_mem_ip.s_ivd_fill_mem_rec_ip_t.u4_size =
            sizeof(ivdext_fill_mem_rec_ip_t);
        s_fill_mem_ip.i4_level = i4_level;
        s_fill_mem_ip.u4_num_reorder_frames = u4_num_reorder_frames;
        s_fill_mem_ip.u4_num_ref_frames = u4_num_ref_frames;
        s_fill_mem_ip.u4_share_disp_buf = u4_share_disp_buf;
        s_fill_mem_ip.u4_num_extra_disp_buf = 0;
        s_fill_mem_ip.e_output_format = IV_YUV_420P;

        s_fill_mem_ip.s_ivd_fill_mem_rec_ip_t.e_cmd = IV_CMD_FILL_NUM_MEM_REC;
        s_fill_mem_ip.s_ivd_fill_mem_rec_ip_t.pv_mem_rec_location = mMemRecords;
        s_fill_mem_ip.s_ivd_fill_mem_rec_ip_t.u4_max_frm_wd = displayStride;
        s_fill_mem_ip.s_ivd_fill_mem_rec_ip_t.u4_max_frm_ht = displayHeight;
        s_fill_mem_op.s_ivd_fill_mem_rec_op_t.u4_size =
            sizeof(ivdext_fill_mem_rec_op_t);

        ps_mem_rec = mMemRecords;
        for (i = 0; i < mNumMemRecords; i++)
            ps_mem_rec[i].u4_size = sizeof(iv_mem_rec_t);

        status = ivdec_api_function(mCodecCtx, (void *)&s_fill_mem_ip,
                (void *)&s_fill_mem_op);

        if (IV_SUCCESS != status) {
            LOG_ERROR("Error in filling mem records: 0x%x",
                    s_fill_mem_op.s_ivd_fill_mem_rec_op_t.u4_error_code);
            return OMX_ErrorUndefined;
        }
        mNumMemRecords =
            s_fill_mem_op.s_ivd_fill_mem_rec_op_t.u4_num_mem_rec_filled;

        ps_mem_rec = mMemRecords;

        for (i = 0; i < mNumMemRecords; i++) {
            ps_mem_rec->pv_base = ivd_aligned_malloc(
                    ps_mem_rec->u4_mem_alignment, ps_mem_rec->u4_mem_size);
            if (ps_mem_rec->pv_base == NULL) {
                LOG_ERROR("Allocation failure for memory record #%zu of size %u",
                        i, ps_mem_rec->u4_mem_size);
                status = IV_FAIL;
                return OMX_ErrorUndefined;
            }

            ps_mem_rec++;
        }
    }

    /* Initialize the decoder */
    {
        ivdext_init_ip_t s_init_ip;
        ivdext_init_op_t s_init_op;

        void *dec_fxns = (void *)ivdec_api_function;

        s_init_ip.s_ivd_init_ip_t.u4_size = sizeof(ivdext_init_ip_t);
        s_init_ip.s_ivd_init_ip_t.e_cmd = (IVD_API_COMMAND_TYPE_T)IV_CMD_INIT;
        s_init_ip.s_ivd_init_ip_t.pv_mem_rec_location = mMemRecords;
        s_init_ip.s_ivd_init_ip_t.u4_frm_max_wd = displayStride;
        s_init_ip.s_ivd_init_ip_t.u4_frm_max_ht = displayHeight;

        s_init_ip.i4_level = i4_level;
        s_init_ip.u4_num_reorder_frames = u4_num_reorder_frames;
        s_init_ip.u4_num_ref_frames = u4_num_ref_frames;
        s_init_ip.u4_share_disp_buf = u4_share_disp_buf;
        s_init_ip.u4_num_extra_disp_buf = 0;

        s_init_op.s_ivd_init_op_t.u4_size = sizeof(s_init_op);

        s_init_ip.s_ivd_init_ip_t.u4_num_mem_rec = mNumMemRecords;
        s_init_ip.s_ivd_init_ip_t.e_output_format = IV_YUV_420P;

        mCodecCtx = (iv_obj_t*)mMemRecords[0].pv_base;
        mCodecCtx->pv_fxns = dec_fxns;
        mCodecCtx->u4_size = sizeof(iv_obj_t);

        LOG_DEBUG("Initializing decoder");
        status = ivdec_api_function(mCodecCtx, (void *)&s_init_ip,
                (void *)&s_init_op);
        if (status != IV_SUCCESS) {
            LOG_ERROR("Error in init: 0x%x",
                    s_init_op.s_ivd_init_op_t.u4_error_code);
            return OMX_ErrorUndefined;
        }
    }

    /* Reset the plugin state */
    bInEos = OMX_FALSE;
    bOutEos = OMX_FALSE;
    bInFlush = OMX_FALSE;

    bChangingResolution = OMX_FALSE;

    /* Set the run time (dynamic) parameters */
    setDisplayStride(displayStride);

    /* Set number of cores/threads to be used by the codec */
    setNumCores();

    /* Get codec version */
    logVersion();

    /* Allocate internal picture buffer */
    OMX_U32 bufferSize = displaySizeY * 3 / 2;
    pInternalBuffer = ivd_aligned_malloc(128, bufferSize);
    if (NULL == pInternalBuffer) {
        LOG_ERROR("Could not allocate flushOutputBuffer of size %zu", bufferSize);
        return OMX_ErrorUndefined;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::deInitDecoder() {
    OMX_U32 i;

    if (mMemRecords) {
        iv_mem_rec_t *ps_mem_rec;

        ps_mem_rec = mMemRecords;
        LOG_DEBUG("deInitDecoder Freeing codec memory");
        for (i = 0; i < mNumMemRecords; i++) {
            if(ps_mem_rec->pv_base) {
                ivd_aligned_free(ps_mem_rec->pv_base);
            }
            ps_mem_rec++;
        }
        ivd_aligned_free(mMemRecords);
        mMemRecords = NULL;
    }

    if(pInternalBuffer) {
        //FSL_FREE(pInternalBuffer);
        ivd_aligned_free(pInternalBuffer);
        pInternalBuffer = NULL;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::reInitDecoder() {
    OMX_ERRORTYPE ret;
    LOG_DEBUG("reInitDecoder");

    deInitDecoder();

    ret = initDecoder();
    if (OMX_ErrorNone != ret) {
        LOG_ERROR("Create failure");
        deInitDecoder();
        return OMX_ErrorUndefined;
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::setFlushMode() {
    IV_API_CALL_STATUS_T status;
    ivd_ctl_flush_ip_t s_video_flush_ip;
    ivd_ctl_flush_op_t s_video_flush_op;

    s_video_flush_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_video_flush_ip.e_sub_cmd = IVD_CMD_CTL_FLUSH;
    s_video_flush_ip.u4_size = sizeof(ivd_ctl_flush_ip_t);
    s_video_flush_op.u4_size = sizeof(ivd_ctl_flush_op_t);
    LOG_DEBUG("Set the decoder in flush mode ");

    /* Set the decoder in Flush mode, subsequent decode() calls will flush */
    status = ivdec_api_function(mCodecCtx, (void *)&s_video_flush_ip,
            (void *)&s_video_flush_op);

    if (status != IV_SUCCESS) {
        LOG_ERROR("Error in setting the decoder in flush mode: (%d) 0x%x", status,
                s_video_flush_op.u4_error_code);
        return OMX_ErrorUndefined;
    }

    bInFlush = OMX_TRUE;
    return OMX_ErrorNone;
}
void SoftHevcDec::setDecodeArgs(ivd_video_decode_ip_t *ps_dec_ip,
        ivd_video_decode_op_t *ps_dec_op,OMX_PTR inBuffer,OMX_U32 inSize,OMX_PTR outBuffer) {
    OMX_U32 sizeY = nWidth * nHeight;
    OMX_U32 sizeUV;
    OMX_U8 *pBuf;

    ps_dec_ip->u4_size = sizeof(ivd_video_decode_ip_t);
    ps_dec_op->u4_size = sizeof(ivd_video_decode_op_t);

    ps_dec_ip->e_cmd = IVD_CMD_VIDEO_DECODE;

    /* When in flush and after EOS with zero byte input,
     * inHeader is set to zero. Hence check for non-null */
    if (pInBuffer) {
        //ps_dec_ip->u4_ts = timeStampIx;
        ps_dec_ip->pv_stream_buffer = (OMX_U8*)inBuffer;
        ps_dec_ip->u4_num_Bytes = inSize;
    } else {
        ps_dec_ip->u4_ts = 0;
        ps_dec_ip->pv_stream_buffer = NULL;
        ps_dec_ip->u4_num_Bytes = 0;
    }


    if (outBuffer) {
        pBuf = (OMX_U8*)outBuffer;
    } else {
        pBuf = (OMX_U8*)pInternalBuffer;
    }

    sizeUV = sizeY / 4;
    ps_dec_ip->s_out_buffer.u4_min_out_buf_size[0] = sizeY;
    ps_dec_ip->s_out_buffer.u4_min_out_buf_size[1] = sizeUV;
    ps_dec_ip->s_out_buffer.u4_min_out_buf_size[2] = sizeUV;

    ps_dec_ip->s_out_buffer.pu1_bufs[0] = pBuf;
    ps_dec_ip->s_out_buffer.pu1_bufs[1] = pBuf + sizeY;
    ps_dec_ip->s_out_buffer.pu1_bufs[2] = pBuf + sizeY + sizeUV;
    ps_dec_ip->s_out_buffer.u4_num_bufs = 3;
    return;
}

OMX_ERRORTYPE SoftHevcDec::InitFilterComponent()
{
    nNalLen = 0;
    bCodecDataParsed = OMX_FALSE;
    bNeedCopyCodecData = OMX_TRUE;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::DeInitFilterComponent()
{
    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::InitFilter()
{

    nWidth = 320;
    nHeight = 240;

    LOG_DEBUG("SoftHevcDec::InitFilter");

    initDecoder();

    bInEos = OMX_FALSE;
    bOutEos = OMX_FALSE;
    bInFlush = OMX_FALSE;

    bNeedCopyCodecData = OMX_TRUE;
    skipMode = IVD_SKIP_NONE;

    bChangingResolution = OMX_FALSE;

    eDecState = HEVC_DEC_PREPARE_FOR_FRAME;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::DeInitFilter()
{
    deInitDecoder();

    return OMX_ErrorNone;
}
OMX_ERRORTYPE SoftHevcDec::ParseCodecInfo()
{
    OMX_U32 i, j, k;
    OMX_U32 info_size=0;
    OMX_U8 *data;
    OMX_U8 * ptr;
    OMX_U32 size;
    OMX_U8 lengthSizeMinusOne;
    OMX_U32 originLen = 0;
    OMX_U32 newLen = 0;
    OMX_PTR newCodecData = NULL;

    OMX_U32 blockNum;

    OMX_U32 nalNum;
    OMX_U16 NALLength;

    if(pCodecData == NULL || nCodecDataLen == 0)
        return OMX_ErrorBadParameter;

    originLen = nCodecDataLen;

    if(originLen < 23)
        return OMX_ErrorBadParameter;

    ptr = (OMX_U8 *)pCodecData;

    lengthSizeMinusOne= ptr[21];
    lengthSizeMinusOne &= 0x03;
    nNalLen = lengthSizeMinusOne+1; /* lengthSizeMinusOne = 0x11, 0b1111 1111 */
    LOG_DEBUG("ParseCodecInfo nal len=%d",nNalLen);

    blockNum = ptr[22];

    data = ptr+23;
    size = originLen - 23;

    //get target codec data len
    for (i = 0; i < blockNum; i++) {
        data += 1;
        size -= 1;

        // Num of nals
        nalNum = (data[0] << 8) + data[1];
        data += 2;
        size -= 2;

        for (j = 0;j < nalNum;j++) {
            if (size < 2) {
                break;
            }

            NALLength = (data[0] << 8) + data[1];
            LOG_DEBUG("ParseCodecInfo NALLength=%d",NALLength);
            data += 2;
            size -= 2;

            if (size < NALLength) {
                break;
            }

            //start code needs 4 bytes
            newLen += 4+NALLength;

            data += NALLength;
            size -= NALLength;

        }
    }

    newCodecData = FSL_MALLOC(newLen);
    if(newCodecData ==NULL)
        return OMX_ErrorUndefined;

    data=(OMX_U8 *)newCodecData;

    ptr = (OMX_U8 *)pCodecData+23;
    size = originLen - 23;
    for (i = 0; i < blockNum; i++) {
        ptr += 1;
        size -= 1;

        // Num of nals
        nalNum = (ptr[0] << 8) + ptr[1];
        ptr += 2;
        size -= 2;

        for (j = 0;j < nalNum;j++) {
            if (size < 2) {
                break;
            }

            NALLength = (ptr[0] << 8) + ptr[1];
            ptr += 2;
            size -= 2;

            if (size < NALLength) {
                break;
            }

            //add start code
            *data=0x0;
            *(data+1)=0x0;
            *(data+2)=0x0;
            *(data+3)=0x01;

            //as start code needs 4 bytes, move 2 bytes for start code, the other 2 bytes uses the nal length
            fsl_osal_memcpy(data+4, ptr, NALLength);

            data += 4+ NALLength;

            ptr += NALLength;
            size -= NALLength;

        }
    }

    FSL_FREE(pCodecData);
    pCodecData = newCodecData;
    nCodecDataLen = newLen;

    LOG_DEBUG("ParseCodecInfo new len=%d",newLen);

    return OMX_ErrorNone;

}

OMX_U32 SoftHevcDec::parseNALSize(OMX_U8 *data) {
    switch (nNalLen) {
        case 1:
            return *data;
        case 2:
            return ((data[0] << 8) | data[1]);
        case 3:
            return ((data[0] << 16) | (data[1] << 8) | data[2]);
        case 4:
            return ((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]);
    }

    return 0;
}

OMX_ERRORTYPE SoftHevcDec::SetInputBuffer(
        OMX_PTR pBuffer,
        OMX_S32 nSize,
        OMX_BOOL bLast)
{
    if(pBuffer == NULL)
        return OMX_ErrorBadParameter;

    pInBuffer = pBuffer;
    nInputSize = nSize;
    nInputOffset = 0;
    LOG_LOG("SoftHevcDec input buffer BEGIN: %p:%d:%d\n", pBuffer, nSize, bInEos);

    if( nCodecDataLen > 0){

        if(bCodecDataParsed != OMX_TRUE){
            LOG_DEBUG("SoftHevcDec BEGIN bCodecDataParsed=%d",bCodecDataParsed);
            ParseCodecInfo();
            bCodecDataParsed = OMX_TRUE;
            LOG_DEBUG("SoftHevcDec END bCodecDataParsed=%d",bCodecDataParsed);
        }

        OMX_U32 srcOffset = 0;
        OMX_U32 newSize = 0;
        OMX_BOOL bMalFormed = OMX_FALSE;
        OMX_U8 * ptr = (OMX_U8 *)pBuffer;

        //do not modify the buffer we have changed before
        if(ptr[0] == 0x0 && ptr[1] == 0x0 && ptr[2] == 0x0 && ptr[3] == 0x1){
            LOG_DEBUG("SoftHevcDec SetInputBuffer skip same buffer");
            goto bail;
        }


        while (srcOffset < (OMX_U32)nSize) {

            if(srcOffset + nNalLen > (OMX_U32)nSize)
                bMalFormed = OMX_TRUE;

            OMX_U32 nalLength = 0;

            if (!bMalFormed) {
                nalLength = parseNALSize(ptr+srcOffset);
                LOG_LOG("SoftHevcDec input buffer nalLength=%d",nalLength);
            }

            if(srcOffset + nNalLen > (OMX_U32)nSize)
                bMalFormed = OMX_TRUE;

            if (bMalFormed) {
                return OMX_ErrorUndefined;
            }

            if (nalLength == 0) {
                continue;
            }

            fsl_osal_memmove( ptr+srcOffset + 4 - nNalLen,ptr+srcOffset, nSize - srcOffset );

            * (ptr + srcOffset)= 0;
            * (ptr + srcOffset+1)= 0;
            * (ptr + srcOffset+2)= 0;
            * (ptr + srcOffset+3)= 1;

            srcOffset += nalLength + nNalLen + 4 - nNalLen;
            newSize += nalLength + 4;
        }

        nInputSize = newSize;

    }

bail:
    bInEos = bLast;
    if(nSize == 0 && !bLast){
        nInputSize = 0;
        pInBuffer = NULL;
    }

    if(eDecState == HEVC_DEC_IDLE &&  nSize > 0)
        eDecState = HEVC_DEC_PREPARE_FOR_FRAME;

    LOG_LOG("SoftHevcDec input buffer END: %p:%d:%d\n", pBuffer, nSize, bInEos);


    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::SetOutputBuffer(OMX_PTR pBuffer)
{
    if(pBuffer == NULL)
        return OMX_ErrorBadParameter;

    pOutBuffer = pBuffer;

    LOG_LOG("SoftHevcDec SetOutputBuffer : %p\n", pBuffer);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::ProcessQOS()
{
    OMX_TIME_CONFIG_TIMESTAMPTYPE sCur;
    OMX_TIME_CONFIG_SCALETYPE sScale;
    OMX_TICKS nTimeStamp;
    OMX_S32 param;

    nTimeStamp=QueryStreamTs();

    if(nTimeStamp < 0 || pClock == NULL)
        return OMX_ErrorNone;

    OMX_INIT_STRUCT(&sScale, OMX_TIME_CONFIG_SCALETYPE);
    OMX_GetConfig(pClock, OMX_IndexConfigTimeScale, &sScale);
    if(!IS_NORMAL_PLAY(sScale.xScale)){
        return OMX_ErrorNone;
    }
    OMX_INIT_STRUCT(&sCur, OMX_TIME_CONFIG_TIMESTAMPTYPE);
    OMX_GetConfig(pClock, OMX_IndexConfigTimeCurrentMediaTime, &sCur);

    ivd_ctl_set_config_ip_t s_ctl_ip;
    ivd_ctl_set_config_op_t s_ctl_op;
    IV_API_CALL_STATUS_T status;

    if(sCur.nTimestamp > (nTimeStamp - DROP_B_THRESHOLD))
    {
        s_ctl_ip.e_frm_skip_mode = IVD_SKIP_B;
    }
    else
    {
        s_ctl_ip.e_frm_skip_mode = IVD_SKIP_NONE;
    }

    if(skipMode != s_ctl_ip.e_frm_skip_mode){

        LOG_DEBUG("change skip mode from 0x%x to 0x%x.",skipMode,s_ctl_ip.e_frm_skip_mode);
        skipMode = s_ctl_ip.e_frm_skip_mode;

        s_ctl_ip.u4_disp_wd = (UWORD32)nWidth;
        s_ctl_ip.e_frm_skip_mode = IVD_SKIP_NONE;

        s_ctl_ip.e_frm_out_mode = IVD_DISPLAY_FRAME_OUT;
        s_ctl_ip.e_vid_dec_mode = IVD_DECODE_FRAME;
        s_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
        s_ctl_ip.e_sub_cmd = IVD_CMD_CTL_SETPARAMS;
        s_ctl_ip.u4_size = sizeof(ivd_ctl_set_config_ip_t);
        s_ctl_op.u4_size = sizeof(ivd_ctl_set_config_op_t);

        LOG_DEBUG("Set the run-time (dynamic) parameters stride = %d", nWidth);
        status = ivdec_api_function(mCodecCtx, (void *)&s_ctl_ip,
                (void *)&s_ctl_op);

        if (status != IV_SUCCESS) {
            LOG_ERROR("Error in Error in Enable SkipB frames 0x%x",
                    s_ctl_op.u4_error_code);

            return OMX_ErrorUndefined;
        }

    }

    return OMX_ErrorNone;
}

FilterBufRetCode SoftHevcDec::FilterOneBuffer()
{
    FilterBufRetCode ret = FILTER_OK;

    switch(eDecState) {
        case HEVC_DEC_IDLE:
            break;
        case HEVC_DEC_INIT:
            ret = FILTER_DO_INIT;
            break;
        case HEVC_DEC_PREPARE_FOR_FRAME:
            ret = PrepareForFrame();
            break;
        case HEVC_DEC_RUN:
            ret = DecodeOneFrame();
            break;
        default:
            break;
    }

    //LOG_DEBUG("SoftHevcDec FilterOneBuffer ret: %d\n", ret);

    return ret;
}

FilterBufRetCode SoftHevcDec::PrepareForFrame()
{
    OMX_U32 status;
    ivd_ctl_set_config_ip_t s_ctl_ip;
    ivd_ctl_set_config_op_t s_ctl_op;

    s_ctl_ip.u4_disp_wd = nWidth;
    s_ctl_ip.e_frm_skip_mode = IVD_SKIP_NONE;

    s_ctl_ip.e_frm_out_mode = IVD_DISPLAY_FRAME_OUT;
    s_ctl_ip.e_vid_dec_mode = IVD_DECODE_FRAME;
    s_ctl_ip.e_cmd = IVD_CMD_VIDEO_CTL;
    s_ctl_ip.e_sub_cmd = IVD_CMD_CTL_SETPARAMS;
    s_ctl_ip.u4_size = sizeof(ivd_ctl_set_config_ip_t);

    s_ctl_op.u4_size = sizeof(ivd_ctl_set_config_op_t);

    status = ivdec_api_function(mCodecCtx, (void *)&s_ctl_ip, (void *)&s_ctl_op);

    if(IV_SUCCESS != status)
    {
        LOG_ERROR("Error in Set Parameters for frame");
    }

    eDecState = HEVC_DEC_RUN;
    LOG_DEBUG("SoftHevcDec PrepareForFrame SUCCESS\n");

    return FILTER_OK;
}

FilterBufRetCode SoftHevcDec::DecodeOneFrame()
{
    FilterBufRetCode ret = FILTER_OK;

    LOG_LOG("SoftHevcDec::DecodeOneFrame BEGIN bNeedCopyCodecData=%d",bNeedCopyCodecData);

    if(!bInit)
        return FILTER_DO_INIT;

    if(bOutEos == OMX_TRUE && pOutBuffer != NULL){
        eDecState = HEVC_DEC_IDLE;
        return FILTER_LAST_OUTPUT;
    }

    if(pOutBuffer == NULL){
        return FILTER_NO_OUTPUT_BUFFER;
    }

    if(pInBuffer == NULL && bInEos != OMX_TRUE){
        return FILTER_NO_INPUT_BUFFER;
    }

    if(bInEos && !bInFlush){
        setFlushMode();
    }

    {
        ivd_video_decode_ip_t s_dec_ip;
        ivd_video_decode_op_t s_dec_op;
        OMX_U32 status;
        struct timeval tv, tv1;
        gettimeofday (&tv, NULL);

        if(bNeedCopyCodecData && nCodecDataLen > 0){
            setDecodeArgs(&s_dec_ip, &s_dec_op,(OMX_PTR)((OMX_U8*)pCodecData),nCodecDataLen,NULL);
        }else{
            setDecodeArgs(&s_dec_ip, &s_dec_op,(OMX_PTR)((OMX_U8*)pInBuffer+nInputOffset),nInputSize,NULL);
        }

        ProcessQOS();

        status = ivdec_api_function(mCodecCtx, (void *)&s_dec_ip, (void *)&s_dec_op);
        gettimeofday (&tv1, NULL);
        LOG_DEBUG("*** Decode Time: %d\n", (tv1.tv_sec-tv.tv_sec)*1000+(tv1.tv_usec-tv.tv_usec)/1000);

        //end the decoding process when in flush mode
        if(status != IV_SUCCESS && bInFlush){
            if(pOutBuffer == NULL) {
                ret = (FilterBufRetCode) (ret | FILTER_NO_OUTPUT_BUFFER);
                bOutEos = OMX_TRUE;
                return ret;
            } else {
                ret = (FilterBufRetCode) (ret | FILTER_LAST_OUTPUT);
                eDecState = HEVC_DEC_IDLE;
                return ret;
            }
        }

        //decoder will report IHEVCD_UNSUPPORTED_DIMENSIONS for output changed event
        if(status != IV_SUCCESS && bNeedCopyCodecData &&
            (IHEVCD_UNSUPPORTED_DIMENSIONS != s_dec_op.u4_error_code)){
            LOG_ERROR("SoftHevcDec decode status=%d err =0x%x",status,s_dec_op.u4_error_code);
            return FILTER_ERROR;
        }

        if(status != IV_SUCCESS && (IHEVCD_UNSUPPORTED_DIMENSIONS != s_dec_op.u4_error_code)){
            LOG_ERROR("SoftHevcDec decode err =0x%x",s_dec_op.u4_error_code);
            pInBuffer = NULL;
            nInputSize = 0;
            ret = FILTER_INPUT_CONSUMED;
            return ret;
        }

        if ((0 < s_dec_op.u4_pic_wd) && (0 < s_dec_op.u4_pic_ht)) {
            DetectOutputFmt(s_dec_op.u4_pic_wd, s_dec_op.u4_pic_ht);
        }

        pInBuffer = NULL;
        nInputSize = 0;
        ret = FILTER_INPUT_CONSUMED;

        if (s_dec_op.u4_output_present) {
            ret = (FilterBufRetCode) (ret | FILTER_HAS_OUTPUT);
        } else {
            bInFlush = OMX_FALSE;
            ret = (FilterBufRetCode) (ret | FILTER_SKIP_OUTPUT);
        }

        //as changg resolution will reinit the decoder, so just return OK.
        if(bChangingResolution){
            bChangingResolution = OMX_FALSE;
            return FILTER_OK;
        }

        if(bNeedCopyCodecData ){
            bNeedCopyCodecData = OMX_FALSE;
            return FILTER_OK;
        }

    }
    //LOG_LOG("SoftHevcDec::DecodeOneFrame END ret=%x",ret);

    return ret;

}


OMX_ERRORTYPE SoftHevcDec::DetectOutputFmt(OMX_U32 width, OMX_U32 height)
{

    if(width != sOutCrop.nWidth || height != sOutCrop.nHeight){
        LOG_DEBUG("DetectOutputFmt %d,%d -> %d,%d",sOutCrop.nWidth,sOutCrop.nHeight,width,height);

        sOutCrop.nLeft = 0;
        sOutCrop.nTop = 0;
        sOutCrop.nWidth = width;// & (~7);
        sOutCrop.nHeight = height;

        nOutBufferCnt = 8;
        sOutFmt.nFrameWidth = ALIGN_STRIDE(width);
        sOutFmt.nFrameHeight = height;

        nOutBufferSize = sOutFmt.nFrameWidth * sOutFmt.nFrameHeight * pxlfmt2bpp(sOutFmt.eColorFormat) / 8;

        nWidth = width;
        nHeight = height;
        LOG_DEBUG("DetectOutputFmt w,h=%d,%d,sOutFmt=%d,%d",
            sOutCrop.nWidth,sOutCrop.nHeight,sOutFmt.nFrameWidth,sOutFmt.nFrameHeight);

        VideoFilter::OutputFmtChanged();
        reInitDecoder();
        bChangingResolution =OMX_TRUE;
    }

    return OMX_ErrorNone;
}


OMX_ERRORTYPE SoftHevcDec::GetOutputBuffer(OMX_PTR *ppBuffer,OMX_S32* pOutSize)
{
    OMX_S32 Ysize = sOutFmt.nFrameWidth * sOutFmt.nFrameHeight;
    OMX_S32 Usize = Ysize/4;
    OMX_U32 i;

    OMX_U8* y = (OMX_U8*)pOutBuffer;
    OMX_U8* u = (OMX_U8*)ALIGN_STRIDE((OMX_U32)pOutBuffer+Ysize);
    OMX_U8* v = (OMX_U8*)ALIGN_STRIDE((OMX_U32)u+Usize);
    OMX_U8* ysrc = (OMX_U8*)pInternalBuffer;
    OMX_U8* usrc = (OMX_U8*)pInternalBuffer + nWidth*nHeight;
    OMX_U8* vsrc = usrc+ nWidth*nHeight/4;


    fsl_osal_memset(pOutBuffer,0,nOutBufferSize);

    for(i=0;i<nHeight;i++)
    {
        fsl_osal_memcpy((OMX_PTR)y, (OMX_PTR)ysrc, nWidth);
        y+=sOutFmt.nFrameWidth;
        ysrc+=nWidth;
    }
    for(i=0;i<nHeight/2;i++)
    {
        fsl_osal_memcpy((OMX_PTR)u, (OMX_PTR)usrc, nWidth/2);
        fsl_osal_memcpy((OMX_PTR)v, (OMX_PTR)vsrc, nWidth/2);
        u+=sOutFmt.nFrameWidth/2;
        v+=sOutFmt.nFrameWidth/2;
        usrc+=nWidth/2;
        vsrc+=nWidth/2;
    }


    *ppBuffer = pOutBuffer;
    *pOutSize = nOutBufferSize;
    LOG_LOG("SoftHevcDec GetOutputBuffer : %p,size=%d \n", *ppBuffer,nOutBufferSize);
#if 0
        FILE * pfile;
        pfile = fopen("/data/data/1.raw","ab");
        if(pfile){
            fwrite(pOutBuffer,1,nOutBufferSize,pfile);
            fclose(pfile);
        }
#endif

    pOutBuffer = NULL;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::FlushInputBuffer()
{
    pInBuffer = NULL;
    nInputSize = 0;
    nInputOffset = 0;
    bInEos = OMX_FALSE;
    bOutEos = OMX_FALSE;
    LOG_DEBUG("SoftHevcDec FlushInputBuffer \n");

    setFlushMode();

    while (1) {
        ivd_video_decode_ip_t s_dec_ip;
        ivd_video_decode_op_t s_dec_op;
        IV_API_CALL_STATUS_T status;

        setDecodeArgs(&s_dec_ip, &s_dec_op,NULL,0,NULL);

        status = ivdec_api_function(mCodecCtx, (void *)&s_dec_ip,
                (void *)&s_dec_op);
        if (0 == s_dec_op.u4_output_present) {
            bInEos = OMX_FALSE;
            break;
        }
    }

    bInFlush = OMX_FALSE;

    return OMX_ErrorNone;
}

OMX_ERRORTYPE SoftHevcDec::FlushOutputBuffer()
{
    pOutBuffer = NULL;
    return OMX_ErrorNone;
}

/**< C style functions to expose entry point for the shared library */
extern "C" {
    OMX_ERRORTYPE SoftHevcDecoderInit(OMX_IN OMX_HANDLETYPE pHandle)
    {
        OMX_ERRORTYPE ret = OMX_ErrorNone;
        SoftHevcDec *obj = NULL;
        ComponentBase *base = NULL;


        obj = FSL_NEW(SoftHevcDec, ());
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
