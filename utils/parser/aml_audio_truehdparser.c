/*
 * Copyright (C) 2024 Amlogic Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "audio_truehd_parser"
//#define LOG_NDEBUG 0

#include <stdlib.h>
#include <string.h>
#include <cutils/log.h>
#include <system/audio.h>
#include "aml_audio_truehdparser.h"
#include "aml_malloc_debug.h"
#include "aml_dump_debug.h"

#define MLP_HEADER_SIZE   (32)  //4+28  check_nibble + access_unit_length + input_timing + major_sync_info ,and the SMPTE timestamp should have already been discarded in ffmpeg:mlp_parse.
#define MAJOR_SYNC_HEADER (8)   //check_nibble + access_unit_length + input_timing + format_sync.

/*  32-bit Format sync word in MLP/TrueHD major sync  */
#define FORMAT_SYNC_FBA  0xf8726fba
#define FORMAT_SYNC_FBB  0xf8726fbb

#define MAX_ACCESS_UNIT_LENGTH_BYTES        (8190) //0XFFF*2

#define GET_4BYTE_VALUE(ptr) ((uint32_t)((uint8_t*)(ptr))[3] | \
                             ((uint32_t)((uint8_t*)(ptr))[2] << 8) | \
                             ((uint32_t)((uint8_t*)(ptr))[1] << 16) | \
                             ((uint32_t)((uint8_t*)(ptr))[0] << 24))

enum PARSER_STATE {
    PARSER_SYNCING,
    PARSER_SYNCED,
};

struct aml_truehd_parser {
    void * buf;
    int32_t buf_size;
    int32_t buf_remain;
    uint32_t status;
};

int aml_truehd_parser_open(void **pparser_handle)
{
    struct aml_truehd_parser *aml_parser_handle = NULL;

    aml_parser_handle = (struct aml_truehd_parser *)aml_audio_calloc(1, sizeof(struct aml_truehd_parser));
    if (aml_parser_handle == NULL) {
        ALOGE("%s handle error", __func__);
        goto error;
    }

    aml_parser_handle->buf_size   = MAX_ACCESS_UNIT_LENGTH_BYTES;
    aml_parser_handle->buf        = aml_audio_calloc(1, MAX_ACCESS_UNIT_LENGTH_BYTES);
    if (aml_parser_handle->buf == NULL) {
        ALOGE("%s data buffer error", __func__);
        aml_audio_free(aml_parser_handle);
        aml_parser_handle = NULL;
        goto error;
    }
    aml_parser_handle->status     = PARSER_SYNCING;
    aml_parser_handle->buf_remain = 0;
    *pparser_handle = aml_parser_handle;
    ALOGI("%s exit =%p", __func__, aml_parser_handle);
    return 0;
error:
    *pparser_handle = NULL;
    ALOGE("%s error", __func__);
    return -1;
}
int aml_truehd_parser_close(void *parser_handle)
{
    struct aml_truehd_parser *aml_parser_handle = (struct aml_truehd_parser *)parser_handle;

    if (aml_parser_handle) {
        if (aml_parser_handle->buf) {
            aml_audio_free(aml_parser_handle->buf);
        }
        aml_audio_free(aml_parser_handle);
    }
    ALOGI("%s exit", __func__);
    return 0;
}

int aml_truehd_parser_reset(void *parser_handle)
{
    struct aml_truehd_parser *aml_parser_handle = (struct aml_truehd_parser *)parser_handle;

    if (aml_parser_handle) {
        aml_parser_handle->status = PARSER_SYNCING;
        aml_parser_handle->buf_remain = 0;
    }
    ALOGI("%s exit", __func__);
    return 0;
}

static int mlp_get_first_access_unit_offset(
    const unsigned char *frameBuf
    , int length)
{
    for (int i = 0; i < (length - 7); i++) {
        if (frameBuf[i + 4] == 0xF8 && frameBuf[i + 5] == 0x72 && frameBuf[i + 6] == 0x6F && (frameBuf[i + 7] == 0xBA || frameBuf[i + 7] == 0xBB)) {
            return i;
        }
    }
    return -1;

}

int aml_truehd_parser_process(
    void *parser_handle
    , const void *in_buffer
    , int32_t numBytes
    , int32_t *used_size
    , void **output_buf
    , int32_t *out_size)
{
    struct aml_truehd_parser *aml_parser_handle = (struct aml_truehd_parser *)parser_handle;
    uint8_t *buffer = (uint8_t *)in_buffer;
    uint8_t *parser_buf = NULL;
    int32_t sync_word_offset = -1;
    int32_t buf_left = 0;
    int32_t buf_offset = 0;
    int32_t need_size = 0;

    int32_t ret = 0;
    int32_t data_valid = 0;
    int32_t frame_size = 0;
    int32_t format_sync;

    if (aml_parser_handle == NULL) {
        ALOGE("error aml_parser_handle is NULL");
        goto error;
    }

    parser_buf = aml_parser_handle->buf;
    buf_left   = numBytes;

    ALOGV("%s input buf size=%d status=%d", __func__, numBytes, aml_parser_handle->status);

    /*1.if status == PARSER_SYNCING, start to find the first AU which carried the major sync.
        if find, set status == PARSER_SYNCED
      2.if status == PARSER_SYNCED, start to read the next AU, this au length will be directly
        treated as a single decoding unit.*/

    /*we need at least MLP_HEADER_SIZE bytes*/
    if (aml_parser_handle->buf_remain < MLP_HEADER_SIZE) {
        need_size = MLP_HEADER_SIZE - aml_parser_handle->buf_remain;
        /*input data is not enough, just copy to internal buf*/
        if (buf_left < need_size) {
            memcpy(parser_buf + aml_parser_handle->buf_remain, buffer + buf_offset, buf_left);
            aml_parser_handle->buf_remain += buf_left;
            goto exit;
        }
        /*make sure the remain buf has MLP_HEADER_SIZE bytes*/
        memcpy(parser_buf + aml_parser_handle->buf_remain, buffer + buf_offset, need_size);
        aml_parser_handle->buf_remain += need_size;
        buf_offset += need_size;
        buf_left   = numBytes - buf_offset;

    }

    if (aml_parser_handle->status == PARSER_SYNCING) {
        sync_word_offset = -1;

        while (sync_word_offset < 0) {
            sync_word_offset = mlp_get_first_access_unit_offset(   //find format sync 0xf8726fba/0xf6726fbb
                (const unsigned char *)parser_buf, aml_parser_handle->buf_remain);

            ALOGI("[%s:%d] sync_word_offset %d 0x%x 0x%x 0x%x 0x%x",__func__, __LINE__, sync_word_offset,\
                parser_buf[sync_word_offset+4], parser_buf[sync_word_offset+5], parser_buf[sync_word_offset+6], parser_buf[sync_word_offset+7]);

            /*if we don't find the header in period bytes, move the last 7 bytes to header*/
            if (sync_word_offset < 0) {
                memmove(parser_buf, parser_buf + aml_parser_handle->buf_remain - MAJOR_SYNC_HEADER + 1, MAJOR_SYNC_HEADER);
                aml_parser_handle->buf_remain = MAJOR_SYNC_HEADER -1;
                need_size = MLP_HEADER_SIZE - aml_parser_handle->buf_remain;
                /*input data is not enough, just copy to internal buf*/
                if (buf_left < need_size) {
                    memcpy(parser_buf + aml_parser_handle->buf_remain, buffer + buf_offset, buf_left);
                    aml_parser_handle->buf_remain += buf_left;
                    /*don't find the header, and there is no enough data*/
                    goto exit;
                }
                /*make the buf has MLP_HEADER_SIZE bytes*/
                memcpy(parser_buf + aml_parser_handle->buf_remain, buffer + buf_offset, need_size);
                aml_parser_handle->buf_remain += need_size;
                buf_offset += need_size;
                buf_left = numBytes - buf_offset;
            } else {
                aml_parser_handle->status = PARSER_SYNCED;
                break;
            }
        }

        data_valid = aml_parser_handle->buf_remain - sync_word_offset;
        if (data_valid <= 0) {
            ALOGE("[%s:%d] TrueHD data error!", __FUNCTION__, __LINE__);
            goto error;
        }

        /*move the header to the beginning of buf*/
        if (sync_word_offset != 0) {
            memmove(parser_buf, parser_buf + sync_word_offset, data_valid);
        }
        aml_parser_handle->buf_remain = data_valid;
    }

    //get assess unit length (unit: byte)

    frame_size = (((parser_buf[0] << 8) | parser_buf[1]) & 0xfff) * 2;
    if (frame_size > MAX_ACCESS_UNIT_LENGTH_BYTES) {
        ALOGE("[%s:%d] au length(%d) is invalid, and exceeds the maximum value(%d)\n", __func__, __LINE__, frame_size, MAX_ACCESS_UNIT_LENGTH_BYTES);
        goto error;
    }
    format_sync = GET_4BYTE_VALUE((parser_buf + 4));
    if (get_debug_value(AML_DEBUG_AUDIOHAL_MATENC)) {
        ALOGI("[%s:%d] access_unit_length = %d, format_sync = 0x%x\n", __func__, __LINE__, frame_size, format_sync);
    }

    /*we have a complete payload*/
    if ((aml_parser_handle->buf_remain + buf_left) >= frame_size) {
        need_size = frame_size - (aml_parser_handle->buf_remain);
        if (need_size >= 0) {
            memcpy(parser_buf + aml_parser_handle->buf_remain, buffer + buf_offset, need_size);
            buf_offset += need_size;
            buf_left = numBytes - buf_offset;

            *output_buf = (void*)(parser_buf);
            *out_size   = frame_size;
            *used_size  = buf_offset;
            aml_parser_handle->buf_remain = 0;
        } else {
            /*internal buf has more data than framesize, we only need part of it*/
            *output_buf = (void*)(parser_buf);
            *out_size   = frame_size;
            aml_parser_handle->buf_remain = -need_size;
            *used_size  = buf_offset + need_size;
            if (*used_size <= 0) {
                ALOGE("%s wrong used size =%d", __func__, *used_size);
                goto error;
            }
        }
        ALOGV("framesize = %d, used size= %d, buf_remain = %d", frame_size, buf_offset, aml_parser_handle->buf_remain);
    } else {
        memcpy(parser_buf + aml_parser_handle->buf_remain, buffer + buf_offset, buf_left);
        aml_parser_handle->buf_remain += buf_left;
        ALOGV("framesize =%d, buf_remain=%d, ", frame_size, aml_parser_handle->buf_remain);
        goto exit;
    }

    return 0;
error:
    *output_buf = NULL;
    *out_size   = 0;
    *used_size = numBytes;
    aml_parser_handle->buf_remain = 0;
    aml_parser_handle->status = PARSER_SYNCING;
    return AML_PARSE_RETURN_TYPE_FAIL;
exit:
    *output_buf = NULL;
    *out_size   = 0;
    *used_size = numBytes;
    return AML_PARSE_RETURN_TYPE_CACHE_DATA;

}
