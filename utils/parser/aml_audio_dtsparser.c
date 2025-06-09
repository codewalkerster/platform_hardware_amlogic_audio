/*
 * Copyright (C) 2017 Amlogic Corporation.
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

#define LOG_TAG "audio_hw_utils_parserdts"
// #define LOG_NDEBUG 0


#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <cutils/log.h>
#include <aml_dump_debug.h>

#include "aml_audio_dtsparser.h"
#include "aml_malloc_debug.h"


#define DTS_MAXSIZE          (32768)
#define DTS_HEADER_SIZE       (12)

struct aml_dts_parser {
    void * buf;
    int32_t buf_size;
    int32_t buf_remain;
    uint32_t status;
    int32_t framesize;
    bool is_sub_parser;
};


int aml_dts_parser_open(void **pparser_handle, void *pParserConfig)
{
    struct aml_dts_parser *aml_parser_handle = NULL;
    parser_config_t *pConfig = (parser_config_t *)pParserConfig;

    aml_parser_handle = (struct aml_dts_parser *)aml_audio_calloc(1, sizeof(struct aml_dts_parser));
    if (aml_parser_handle == NULL) {
        AM_LOGE("%s handle error", __func__);
        goto error;
    }

    aml_parser_handle->buf_size  = DTS_MAXSIZE;
    aml_parser_handle->buf  = aml_audio_calloc(1, DTS_MAXSIZE);
    if (aml_parser_handle->buf == NULL) {
        AM_LOGE("%s data buffer error", __func__);
        aml_audio_free(aml_parser_handle);
        aml_parser_handle = NULL;
        goto error;
    }
    //aml_parser_handle->status = PARSER_SYNCING;
    aml_parser_handle->buf_remain = 0;
    if (pConfig) {
        aml_parser_handle->is_sub_parser = pConfig->isSubParser;
    }
    *pparser_handle = aml_parser_handle;
    AM_LOGI("%s exit =%p", __func__, aml_parser_handle);
    return 0;
error:
    *pparser_handle = NULL;
    AM_LOGE("%s error", __func__);
    return -1;
}
int aml_dts_parser_close(void *parser_handle)
{
    struct aml_dts_parser *aml_parser_handle = (struct aml_dts_parser *)parser_handle;

    if (aml_parser_handle) {
        if (aml_parser_handle->buf) {
            aml_audio_free(aml_parser_handle->buf);
            aml_parser_handle->buf = NULL;
        }
        aml_audio_free(aml_parser_handle);
        aml_parser_handle = NULL;
    }
    AM_LOGI("%s exit", __func__);
    return 0;
}

int aml_dts_parser_reset(void *parser_handle)
{
    struct aml_dts_parser *aml_parser_handle = (struct aml_dts_parser *)parser_handle;

    if (aml_parser_handle) {
        //aml_parser_handle->status = PARSER_SYNCING;
        aml_parser_handle->buf_remain = 0;
    }
    AM_LOGI("%s exit", __func__);
    return 0;
}

int aml_dts_parser_flush(void *parser_handle)
{
    struct aml_dts_parser *aml_parser_handle = (struct aml_dts_parser *)parser_handle;

    //use reset to complete flush action.
    aml_dts_parser_reset(parser_handle);
    ALOGI("%s exit", __func__);
    return 0;
}

int aml_dts_parser_process(void *parser_handle, const void *in_buffer, int32_t numBytes, int32_t *used_size, void **output_buf, int32_t *out_size, struct dts_parser_info *dts_info)
{
    struct aml_dts_parser *aml_parser_handle = (struct aml_dts_parser *)parser_handle;
    dts_info;

    if (numBytes > aml_parser_handle->buf_size) {
        aml_parser_handle->buf = aml_audio_realloc(aml_parser_handle->buf, numBytes);
        if (aml_parser_handle->buf == NULL) {
            AM_LOGE("%s realloc buf failed =%d", __func__, numBytes);
            return -1;
        }
    }
    *output_buf = aml_parser_handle->buf;
    memcpy(*output_buf, in_buffer, numBytes);
    *out_size = numBytes;
    *used_size = numBytes;

    return 0;
}

int dts_parsing_data_process(void *phandle, const void *inABuffer, void *outABuffer, void *parser_callback)
{
    aml_audio_buffer_t *audioBuffer = (aml_audio_buffer_t *)inABuffer;
    const void *inBuffer = audioBuffer->pData;
    size_t inBytes = audioBuffer->size;
    aml_audio_buffer_t *outAudioBuffer = (aml_audio_buffer_t *)outABuffer;
    int retValue = 0;
    struct dts_parser_info dts_info = { 0 };
    void *outBuffer = NULL;
    int32_t outBytes = 0;
    int32_t leftBytes = inBytes;
    int32_t usedBytes = 0, totalUsedBytes = 0;
    int32_t inSize = (int32_t)inBytes;
    char *inBuf = (char *)inBuffer;
    struct aml_dts_parser *pParserHanle = (struct aml_dts_parser *)phandle;
    bool is_sub_parser = pParserHanle->is_sub_parser;

    do {
        aml_dts_parser_process(phandle, inBuf, inSize, &usedBytes, &outBuffer, &outBytes, &dts_info);
        totalUsedBytes += usedBytes;
        if (leftBytes >= usedBytes) {
            leftBytes -= usedBytes;
            inSize = leftBytes;
        }
        inBuf = inBuf + usedBytes;

        if (parser_callback && outBuffer && outBytes > 0) {
            aml_parser_data_callback_t *pCallback = (aml_parser_data_callback_t *)parser_callback;
            Func_Write_CallBack __callback = pCallback->callback;
            outAudioBuffer->pData = outBuffer;
            outAudioBuffer->size = outBytes;
            outAudioBuffer->apts = audioBuffer->apts;
            memcpy(&outAudioBuffer->bufFormat, &audioBuffer->bufFormat, sizeof(buffer_data_format_t));

            retValue = (*__callback)(pCallback->common.pAmlParser, outAudioBuffer, phandle);
        }
        AM_LOGV(" is_sub_parser:%d, phandle:%p inBuf:%p inSize(leftBytes):%d %d,  used_bytes:%d totalUsedBytes:%d outBuffer:%p out_frame_size:%d  dts_info.frame_size:%d",
            is_sub_parser, phandle, inBuf, inSize, leftBytes, usedBytes, totalUsedBytes, outBuffer, outBytes, dts_info.frame_size);

        /*if it is sub parser, shouldn't break directly.
         *one hwsync packet maybe contains multi data frames,
         *so it should loop parse all frames.
         */
        if (!is_sub_parser) {
            retValue = usedBytes;
            break;
        }
    } while (inSize > 0);

    return retValue;
}

aml_parser_func_t *get_dts_parser_func_handle(void)
{
    aml_parser_func_t *amlParserFunc = NULL;

    amlParserFunc = (struct aml_parser_func *)aml_audio_calloc(1, sizeof(struct aml_parser_func));
    if (amlParserFunc) {
        amlParserFunc->f_init       = aml_dts_parser_open;
        amlParserFunc->f_deinit     = aml_dts_parser_close;
        amlParserFunc->f_process    = dts_parsing_data_process;
        amlParserFunc->f_reset      = aml_dts_parser_reset;
        amlParserFunc->f_flush      = aml_dts_parser_flush;
    } else {
        AM_LOGE(" calloc amlParserFunc:%p failed", amlParserFunc);
        amlParserFunc = NULL;
    }

    return amlParserFunc;
}

