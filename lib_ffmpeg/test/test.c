/*
 *  Copyright (c) 2011-2013, Freescale Semiconductor Inc.,
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libavutil/opt.h"
#include "avformat.h"
#include "avcodec.h"


extern AVInputFormat ff_rtsp_demuxer;
extern AVInputFormat ff_applehttp_demuxer;
extern URLProtocol ff_http_protocol;
extern URLProtocol ff_mmsh_protocol;
extern URLProtocol ff_udp_protocol;
extern URLProtocol ff_rtp_protocol;


int test_http(char *url)
{
    int err = 0;
    URLContext *uc = NULL;

    uc = (URLContext*)malloc(sizeof(URLContext) + strlen(url) + 1);
    if(uc == NULL) {
        printf("Allocate for http context failed.\n");
        return 0;
    }

    uc->filename = (char *) &uc[1];
    strcpy(uc->filename, url);
    uc->prot = &ff_http_protocol;
    uc->flags = AVIO_RDONLY;
    uc->is_streamed = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */
    if (uc->prot->priv_data_size) {
        uc->priv_data = malloc(uc->prot->priv_data_size);
        if (uc->prot->priv_data_class) {
            *(const AVClass**)uc->priv_data = uc->prot->priv_data_class;
            av_opt_set_defaults(uc->priv_data);
        }
    }

    err = uc->prot->url_open(uc, uc->filename, uc->flags);
    if (err)
        return err;
    uc->is_connected = 1;

    FILE *fp = fopen("httpstream.dat", "wb");
    if(fp == NULL)
        return 0;

    uint8_t buffer[2048];
    int len = 0;
    while(1) {
        len = uc->prot->url_read(uc, buffer, 2048);
        if (len == 0)
            break;
        fwrite(buffer, 1, len, fp);
        printf("recev %d byte from httpserver.\n", len);
    }

    fclose(fp);
    uc->prot->url_close(uc);
    free(uc);

    return 1;
}

int test_udp(char *url)
{
    int err = 0;
    URLContext *uc = NULL;

    printf("test_udp url %s\n", url);

    uc = (URLContext*)malloc(sizeof(URLContext) + strlen(url) + 1);
    if(uc == NULL) {
        printf("Allocate for udp context failed.\n");
        return 0;
    }

    uc->filename = (char *) &uc[1];
    strcpy(uc->filename, url);
    uc->prot = &ff_udp_protocol;
    uc->flags = AVIO_RDONLY;
    uc->is_streamed = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */

    printf("url open\n");
    err = uc->prot->url_open(uc, uc->filename, uc->flags);
    if (err){
        printf("url open fail\n");
        return err;
    }
    uc->is_connected = 1;

    printf("open dump file\n");
    FILE *fp = fopen("/data/udpstream.dat", "wb");
    if(fp == NULL){
        printf("open dump file fail\n");
        return 0;
    }

    uint8_t buffer[2048];
    int len = 0;
    printf("start reading loop\n");
    while(1) {
        len = uc->prot->url_read(uc, buffer, 2048);
        printf("recev %d byte from udpserver.\n", len);
        if (len == 0)
            break;
        if(len > 0)
            fwrite(buffer, 1, len, fp);
    }

    fclose(fp);
    uc->prot->url_close(uc);
    free(uc);

    return 1;
}


int test_rtp(char *url)
{
    int err = 0;
    URLContext *uc = NULL;

    printf("test_rtp url %s\n", url);

    uc = (URLContext*)malloc(sizeof(URLContext) + strlen(url) + 1);
    if(uc == NULL) {
        printf("Allocate for udp context failed.\n");
        return 0;
    }

    uc->filename = (char *) &uc[1];
    strcpy(uc->filename, url);
    uc->prot = &ff_rtp_protocol;
    uc->flags = AVIO_RDONLY;
    uc->is_streamed = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */

    printf("url open\n");
    err = uc->prot->url_open(uc, uc->filename, uc->flags);
    if (err){
        printf("url open fail\n");
        return err;
    }
    uc->is_connected = 1;

    printf("open dump file\n");
    FILE *fp = fopen("/data/rtpstream.dat", "wb");
    if(fp == NULL){
        printf("open dump file fail\n");
        return 0;
    }

    uint8_t buffer[2048];
    int len = 0;
    printf("start reading loop\n");
    while(1) {
        len = uc->prot->url_read(uc, buffer, 2048);
        printf("recev %d byte from udpserver.\n", len);
        if (len == 0)
            break;

        char payloadType = buffer[1] & 0x7f;
        if(payloadType != 33){
            printf("payloadType mismatch: %x, len %d\n", payloadType, len);
            continue;
        }
        
        if(len > 12)
            fwrite(buffer+12, 1, len-12, fp);
    }

    fclose(fp);
    uc->prot->url_close(uc);
    free(uc);

    return 1;
}

int dump_packet(AVPacket *pkt)
{
    if(pkt->stream_index == 0) {
        FILE *fp = fopen("stream0.dat", "ab");
        if(fp) {
            fwrite(pkt->data, 1, pkt->size, fp);
            fclose(fp);
        }
    }

    if(pkt->stream_index == 1) {
        FILE *fp = fopen("stream1.dat", "ab");
        if(fp) {
            fwrite(pkt->data, 1, pkt->size, fp);
            fclose(fp);
        }
    }

    return 1;
}

int test_httplive(char *url)
{
    int err;
    AVFormatContext *ic = NULL;
    AVInputFormat *fmt = &ff_applehttp_demuxer;
    AVFormatParameters params, *ap = &params;
    AVPacket cur_pkt, *pkt = &cur_pkt;

    memset(ap, 0, sizeof(*ap));

    ic = avformat_alloc_context();
    if(ic == NULL)
        return 0;

    ic->iformat = fmt;
    strcpy(ic->filename, url);

    if (fmt->priv_data_size > 0) {
        ic->priv_data = av_mallocz(fmt->priv_data_size);
        if (!ic->priv_data) {
            return 0;
        }
    } else {
        ic->priv_data = NULL;
    }

    err = ic->iformat->read_header(ic, ap);
    if (err < 0)
        return 0;

    while(1) {
        av_init_packet(pkt);
        //err = ic->iformat->read_packet(ic, pkt);
        err = av_read_packet(ic, pkt);
        if (err < 0)
            return 0;
        printf("stream: %d, len: %d, pts: %lld, flags: %x\n", pkt->stream_index, pkt->size, pkt->pts, pkt->flags);
        dump_packet(pkt);
        if (pkt->destruct) pkt->destruct(pkt);
        pkt->data = NULL; pkt->size = 0;
    }

    return 1;
}

int test_mms(char *url)
{
    int err = 0;
    URLContext *uc = NULL;

    uc = (URLContext*)malloc(sizeof(URLContext) + strlen(url) + 1);
    if(uc == NULL) {
        printf("Allocate for http context failed.\n");
        return 0;
    }

    uc->filename = (char *) &uc[1];
    strcpy(uc->filename, url);
    uc->prot = &ff_mmsh_protocol;
    uc->flags = AVIO_RDONLY;
    uc->is_streamed = 0; /* default = not streamed */
    uc->max_packet_size = 0; /* default: stream file */
    if (uc->prot->priv_data_size) {
        uc->priv_data = malloc(uc->prot->priv_data_size);
        if (uc->prot->priv_data_class) {
            *(const AVClass**)uc->priv_data = uc->prot->priv_data_class;
            av_opt_set_defaults(uc->priv_data);
        }
    }

    err = uc->prot->url_open(uc, uc->filename, uc->flags);
    if (err)
        return err;
    uc->is_connected = 1;

    FILE *fp = fopen("mmsh.dat", "wb");
    if(fp == NULL)
        return 0;

    uint8_t buffer[2048];
    int len = 0;
    while(1) {
        len = uc->prot->url_read(uc, buffer, 2048);
        if (len == 0)
            break;
        fwrite(buffer, 1, len, fp);
        printf("recev %d byte from mmsh server.\n", len);
    }

    fclose(fp);
    uc->prot->url_close(uc);
    free(uc);

    return 1;
}

int test_rtsp(char *url)
{
    int err;
    AVFormatContext *ic = NULL;
    AVInputFormat *fmt = &ff_rtsp_demuxer;
    AVFormatParameters params, *ap = &params;
    AVPacket cur_pkt, *pkt = &cur_pkt;

    memset(ap, 0, sizeof(*ap));

    ic = avformat_alloc_context();
    if(ic == NULL)
        return 0;

    ic->iformat = fmt;
    strcpy(ic->filename, url);

    if (fmt->priv_data_size > 0) {
        ic->priv_data = av_mallocz(fmt->priv_data_size);
        if (!ic->priv_data) {
            return 0;
        }
    } else {
        ic->priv_data = NULL;
    }

    err = ic->iformat->read_header(ic, ap);
    if (err < 0)
        return 0;

    while(1) {
        av_init_packet(pkt);
        //err = ic->iformat->read_packet(ic, pkt);
        err = av_read_packet(ic, pkt);
        if (err < 0)
            return 0;
        printf("stream: %d, len: %d, pts: %lld\n", pkt->stream_index, pkt->size, pkt->pts);
        dump_packet(pkt);
        if (pkt->destruct) pkt->destruct(pkt);
        pkt->data = NULL; pkt->size = 0;
    }

    return 1;
}


int main(int argc, char *argv[])
{
    char *url = (char*)argv[1];

    if(url == NULL)
        return 0;

    av_register_all();

    printf("Loading %s\n", url);
    if(!strncmp(url, "http://", 7))
        test_http(url);

    if(!strncmp(url, "rtsp://", 7))
        test_rtsp(url);

    if(!strncmp(url, "mms://", 6))
        test_mms(url);

    if(!strncmp(url, "udp://", 6))
        test_udp(url);

    if(!strncmp(url, "rtp://", 6))
        test_rtp(url);

    return 1;
}
