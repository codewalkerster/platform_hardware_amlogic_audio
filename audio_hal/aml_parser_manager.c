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

#define LOG_TAG "audio_hw_parser_manager"
//#define LOG_NDEBUG 0

#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <cutils/log.h>
#include <inttypes.h>

#include "aml_parser_manager.h"
#include "audio_hwsync.h"
#include "aml_audio_ac3parser.h"
#include "aml_audio_spdifdec.h"
#include "aml_audio_ac4parser.h"
#include "aml_audio_dtsparser.h"
#include "audio_hw_utils.h"

const char* parserType2Str(aml_parser_type_t type)
{
    ENUM_TYPE_TO_STR_START("AML_PARSER_");
    ENUM_TYPE_TO_STR(AML_PARSER_HWSYNC)
    ENUM_TYPE_TO_STR(AML_PARSER_IEC)
    ENUM_TYPE_TO_STR(AML_PARSER_AC3)
    ENUM_TYPE_TO_STR(AML_PARSER_AC4)
    ENUM_TYPE_TO_STR(AML_PARSER_MAT)
    ENUM_TYPE_TO_STR(AML_PARSER_TRUEHD)
    ENUM_TYPE_TO_STR(AML_PARSER_DTS)
    ENUM_TYPE_TO_STR(AML_PARSER_DTSHD)
    ENUM_TYPE_TO_STR(AML_PARSER_HEAAC)
    ENUM_TYPE_TO_STR(AML_PARSER_MAX)
    ENUM_TYPE_TO_STR_END
}

static bool _is_original_raw_format(audio_format_t inFormat)
{
    bool retValue = false;
    switch (inFormat) {
        case AUDIO_FORMAT_AC3:
        case AUDIO_FORMAT_E_AC3:
        case AUDIO_FORMAT_E_AC3_JOC:
        case AUDIO_FORMAT_AC4:
        case AUDIO_FORMAT_MAT:
        case AUDIO_FORMAT_DOLBY_TRUEHD:
        case AUDIO_FORMAT_DTS:
        case AUDIO_FORMAT_DTS_HD:
            retValue = true;
            break;
        default:
            break;
    };

    return retValue;
}

static bool _is_hwsync_and_raw_format(aml_parser_t *pAmlParser)
{
    parser_config_t *pParserConfig = &(pAmlParser->parserConfig);
    audio_format_t inSubFormat = pParserConfig->dataFormat.subFormat;

    return (pAmlParser->parserConfig.isHwsyncFlag
            && _is_original_raw_format(inSubFormat));
}

static bool is_raw_parser_support(audio_format_t inFormat) {
    bool retValue = false;
    switch (inFormat) {
        case AUDIO_FORMAT_IEC61937:
        case AUDIO_FORMAT_AC3:
        case AUDIO_FORMAT_E_AC3:
        case AUDIO_FORMAT_E_AC3_JOC:
        case AUDIO_FORMAT_AC4:
        case AUDIO_FORMAT_DTS:
        case AUDIO_FORMAT_DTS_HD:
        case AUDIO_FORMAT_DTS_HD_MA:
        case AUDIO_FORMAT_DTS_UHD:
        case AUDIO_FORMAT_DTS_UHD_P2:
            retValue = true;
            break;
        default:
            break;
    };

    return retValue;
}

static int _convert_format_to_parser_type(audio_format_t inFormat, bool isHwsyncParser)
{
    int parserType = AML_PARSER_INVALID;
    if (isHwsyncParser) {
        parserType = AML_PARSER_HWSYNC;
    } else {
        switch (inFormat) {
            case AUDIO_FORMAT_IEC61937:
                parserType = AML_PARSER_IEC;
                break;
            case AUDIO_FORMAT_PCM_16_BIT:
            case AUDIO_FORMAT_PCM_32_BIT:
                parserType = AML_PARSER_HWSYNC;
                break;
            case AUDIO_FORMAT_AC3:
            case AUDIO_FORMAT_E_AC3:
            case AUDIO_FORMAT_E_AC3_JOC:
                parserType = AML_PARSER_AC3;
                break ;
            case AUDIO_FORMAT_AC4:
                parserType = AML_PARSER_AC4;
                break;
            case AUDIO_FORMAT_MAT:
                parserType = AML_PARSER_MAT;
                break;
            case AUDIO_FORMAT_DTS:
            case AUDIO_FORMAT_DTS_HD:
            case AUDIO_FORMAT_DTS_HD_MA:
            case AUDIO_FORMAT_DTS_UHD:
            case AUDIO_FORMAT_DTS_UHD_P2:
                parserType = AML_PARSER_DTS;
                break;
            default:
                break;
        };
    }
    return parserType;
}


static aml_parser_func_t *_get_dynamic_parser_function(aml_parser_t *pAmlParser, audio_format_t inFormat, bool isHwsyncParser)
{
    aml_parser_func_t *pParserFunc = NULL;
    int type = _convert_format_to_parser_type(inFormat, isHwsyncParser);

    if (pAmlParser && pAmlParser->parserInfos[type].pFunc) {
        return pParserFunc = (aml_parser_func_t *)pAmlParser->parserInfos[type].pFunc;
    }

    if (isHwsyncParser) {
        pParserFunc = get_hwsync_parser_func_handle();
    } else {
        switch (inFormat) {
            case AUDIO_FORMAT_IEC61937:
                pParserFunc = get_iec_parser_func_handle();
                break;
            case AUDIO_FORMAT_PCM_16_BIT:
            case AUDIO_FORMAT_PCM_32_BIT:
                pParserFunc = get_hwsync_parser_func_handle();
                break;
            case AUDIO_FORMAT_AC3:
            case AUDIO_FORMAT_E_AC3:
            case AUDIO_FORMAT_E_AC3_JOC:
                pParserFunc = get_ac3_parser_func_handle();
                break ;
            case AUDIO_FORMAT_AC4:
                pParserFunc = get_ac4_parser_func_handle();
                break ;
            case AUDIO_FORMAT_MAT:
                break;
            case AUDIO_FORMAT_DTS:
            case AUDIO_FORMAT_DTS_HD:
            case AUDIO_FORMAT_DTS_HD_MA:
            case AUDIO_FORMAT_DTS_UHD:
            case AUDIO_FORMAT_DTS_UHD_P2:
                pParserFunc = get_dts_parser_func_handle();
                break;
            default:
                break;
        };
    }
    return pParserFunc;
}


static bool _is_need_sub_parser(aml_parser_t *pAmlParser, void *pParserHandle)
{
    int i = 0;
    bool retValue = false;
    parser_info_t *pParserInfos = pAmlParser->parserInfos;

    for (i = 0; i < AML_PARSER_MAX; ++i) {
        /*there are two scenes that need sub parser.
        **one case is IEC parser.
        **the other case is hwsync parser of raw data.
        */
        if (pParserHandle == pParserInfos[i].pHandle
            && ((pParserInfos[i].type == AML_PARSER_HWSYNC && is_raw_parser_support(pAmlParser->parserConfig.dataFormat.format)))) {
            retValue = true ;
            break;
        }
    }
    return retValue;
}

static void _get_pParserFunc_and_Handle_from_pAmlParser(aml_parser_t *pAmlParser, void **ppParserFunc, void **ppParserHandle)
{
    if (pAmlParser != NULL) {
        parser_config_t *pParserConfig = &(pAmlParser->parserConfig);
        audio_format_t inFormat = pParserConfig->dataFormat.format;
        //process should be called by stream,so this hwsync would match with it.
        bool isHwsyncParser = pAmlParser->parserConfig.isHwsyncFlag;
        int type = _convert_format_to_parser_type(inFormat, isHwsyncParser);
        aml_parser_func_t *pParserFunc = pAmlParser->parserInfos[type].pFunc;
        void *pParserHandle = pAmlParser->parserInfos[type].pHandle;

        *ppParserFunc = pParserFunc;
        *ppParserHandle = pParserHandle;
    } else {
        AM_LOGW(" pAmlParser is %p", pAmlParser);
    }
    return;
}

static void* aml_parser_alloc_memory(aml_parser_t *pAmlParser, size_t require_size)
{
    int i = 0;
    void *pBuffer = NULL;
    parser_memory_item_t *pMemoryItem = NULL;

    pthread_mutex_lock(&pAmlParser->memoryLock);
    for (i = 0; i < AML_PARSER_CACHE_MEMORY_NUM; i++) {
        pMemoryItem = &pAmlParser->memoryPool[i];
        if (pMemoryItem->inUsed) {
            continue;
        } else {
            if (aml_audio_check_and_realloc(&pMemoryItem->pBuffer, &pMemoryItem->bufferSize, require_size) == 0) {
                pMemoryItem->inUsed = true;
                pBuffer = pMemoryItem->pBuffer;
            } else {
                ALOGE("%s realloc size %zu fail !", __func__, require_size);
            }
            break;
        }
    }
    pthread_mutex_unlock(&pAmlParser->memoryLock);

    if (pBuffer == NULL) {
        pBuffer = aml_audio_malloc(require_size);
    }
    return pBuffer;
}

static void aml_parser_free_memory(aml_parser_t *pAmlParser, void *buffer)
{
    int i = 0;
    bool found = false;
    parser_memory_item_t *pMemoryItem = NULL;

    pthread_mutex_lock(&pAmlParser->memoryLock);
    for (i = 0; i < AML_PARSER_CACHE_MEMORY_NUM; i++) {
        pMemoryItem = &pAmlParser->memoryPool[i];
        if (pMemoryItem->pBuffer == buffer) {
            pMemoryItem->inUsed = false;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&pAmlParser->memoryLock);

    // this memory is not cached, just free directly
    if (!found) {
        aml_audio_free(buffer);
    }
}

static void aml_parser_destroy_cache_memory(aml_parser_t *pAmlParser)
{
    int i = 0;
    parser_memory_item_t *pMemoryItem = NULL;

    pthread_mutex_lock(&pAmlParser->memoryLock);
    for (i = 0; i < AML_PARSER_CACHE_MEMORY_NUM; i++) {
        parser_memory_item_t *pMemoryItem = &pAmlParser->memoryPool[i];
        if (pMemoryItem->pBuffer) {
            ALOGD("%s i = %d, pBuffer = %p, bufferSize = %zu", __func__, i, pMemoryItem->pBuffer, pMemoryItem->bufferSize);
            aml_audio_free(pMemoryItem->pBuffer);
            pMemoryItem->pBuffer = NULL;
            pMemoryItem->bufferSize = 0;
            pMemoryItem->inUsed = false;
        }
    }
    pthread_mutex_unlock(&pAmlParser->memoryLock);
}

//maybe there are multilevel parser invoked.
//so add this parser callbak that can handle it.
int parser_data_callback(void *priObject, void *aBuffer, void *pParserHandle)
{
    int retValue = 0;
    aml_parser_t *pAmlParser = (aml_parser_t *)priObject;
    aml_audio_buffer_t *inAudioBuffer = (aml_audio_buffer_t *)aBuffer;

    //IEC and Hwsync parser should entry this code.
    if (_is_need_sub_parser(pAmlParser, pParserHandle)) {
        bool isHwsyncParser = false;//sub parser can't be hwsync type.
        parser_config_t *pParserConfig = &(pAmlParser->parserConfig);
        audio_format_t inSubFormat = pParserConfig->dataFormat.subFormat;
        int type = _convert_format_to_parser_type(inSubFormat, isHwsyncParser);
        aml_parser_func_t *pSubParserFunc = NULL;
        void *pSubParserHandle = NULL;
        aml_audio_buffer_t *outAudioBuffer = NULL;

        //AM_LOGI(" pParserHandle:%p  buffer:%p bytes:%zu outApts:0x%" PRIx64 " (%" PRIu64 " ms) ", pParserHandle, buffer, bytes, outApts, outApts/90);
        pAmlParser->outApts = inAudioBuffer->apts;
        pSubParserFunc = pAmlParser->parserInfos[type].pFunc;
        pSubParserHandle = pAmlParser->parserInfos[type].pHandle;
        outAudioBuffer = pAmlParser->parserInfos[type].AudioBuffer;
        if (pSubParserFunc && pSubParserFunc) {
            aml_parser_data_callback_t parserCallback = {
                .common.pAmlParser = (void *)pAmlParser,
                .callback = parser_data_callback,
            };

            retValue = pSubParserFunc->f_process(pSubParserHandle, inAudioBuffer, outAudioBuffer, (void *)(&parserCallback));
        } else {
            AM_LOGW(" pParserFunc:%p  pParserHandle:%p  in_sub_format:0x%x", pSubParserFunc, pSubParserHandle, inSubFormat);
        }
        return retValue;
    }

    if (inAudioBuffer->apts == 0 && pAmlParser->outApts) {
        inAudioBuffer->apts = pAmlParser->outApts;
    }

    //AM_LOGI(" callback to stream  pParserHandle:%p  buffer:%p bytes:%zu outApts:0x%" PRIx64 " (%" PRIu64 " ms) ",
    //    pParserHandle, inAudioBuffer->pData, inAudioBuffer->size, inAudioBuffer->apts, inAudioBuffer->apts/90);
//current not define USE_CALLBACK_FOR_PARSER_TO_STREAM
#ifndef USE_CALLBACK_FOR_PARSER_TO_STREAM
    struct buffer_infos *pBuffInfos = (struct buffer_infos *)aml_audio_calloc(1, sizeof(struct buffer_infos));
    struct aml_audio_buffer *tmpABuffer = (struct aml_audio_buffer *)aml_audio_calloc(1, sizeof(aml_audio_buffer_t));
    void *tmpbuf = aml_parser_alloc_memory(pAmlParser, inAudioBuffer->size*2);
    if (pBuffInfos && tmpABuffer && tmpbuf) {
        memcpy(tmpABuffer, inAudioBuffer, sizeof(aml_audio_buffer_t));
        tmpABuffer->pData = tmpbuf;
        memcpy(tmpABuffer->pData, inAudioBuffer->pData, inAudioBuffer->size);//copy parsed audio data.
        pBuffInfos->pBuffer = tmpABuffer;
        list_add_tail(&pAmlParser->bufListHead, &pBuffInfos->lNode);
    } else {
        retValue = -1;
        AM_LOGE(" pBuffInfos:%p  alloc failed, then retValue:%d", pBuffInfos, retValue);
        if (pBuffInfos) {
            aml_audio_free(pBuffInfos);
        }
        if (tmpABuffer) {
            aml_audio_free(tmpABuffer);
        }
        if (tmpbuf) {
            aml_parser_free_memory(pAmlParser, tmpbuf);
        }
    }
#else
    if (pAmlParser && pAmlParser->pWriteCallback) {
        aml_parser_data_callback_t *pCallback = (aml_parser_data_callback_t *)pAmlParser->pWriteCallback;
        Func_Write_CallBack __callback = pCallback->callback;
        retValue = (*__callback)(pCallback->common.pAmlStream, aBuffer, NULL/*pParserHandle*/);
    }
#endif
    return retValue;
}

int aml_parser_process(aml_parser_t *pAmlParser, const void *aBuffer, void *pCallback)
{
    aml_audio_buffer_t *audioBuffer = (aml_audio_buffer_t *)aBuffer;
    const void *inBuffer = audioBuffer->pData;
    size_t inBytes = audioBuffer->size;
    aml_audio_buffer_t *outAudioBuffer = NULL;

    aml_parser_func_t *pParserFunc = NULL;
    int ret = inBytes;
    parser_config_t *pParserConfig = &(pAmlParser->parserConfig);
    audio_format_t inFormat = pParserConfig->dataFormat.format;
    //process should be called by stream,so this hwsync would match with it.
    bool isHwsyncParser = pAmlParser->parserConfig.isHwsyncFlag;
    int type = _convert_format_to_parser_type(inFormat, isHwsyncParser);
    void *pParserHandle = NULL;

    pAmlParser->pWriteCallback = pCallback;
    pParserFunc = pAmlParser->parserInfos[type].pFunc;
    pParserHandle = pAmlParser->parserInfos[type].pHandle;
    outAudioBuffer = pAmlParser->parserInfos[type].AudioBuffer;

    if (pParserFunc && pParserHandle) {
        aml_parser_data_callback_t parserCallback = {
            .common.pAmlParser = (void *)pAmlParser,
            .callback = parser_data_callback,
        };

        pParserFunc->f_process(pParserHandle, aBuffer, outAudioBuffer, (void *)(&parserCallback));
    }

    return ret;
}

int aml_parser_get_format(aml_parser_t *pAmlParser)
{
    pAmlParser;
    //need to implement later.
    //pAmlParser->parsedFormat
    return 0;
}

int aml_parser_reset(aml_parser_t *pAmlParser)
{
    pAmlParser;
    return 0;
}

int aml_parser_flush(aml_parser_t *pAmlParser)
{
    if (pAmlParser != NULL && !list_empty(&pAmlParser->bufListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct buffer_infos *ptmp = NULL;

        //drop audio buffer in list.
        list_for_each_safe(item, temp, &pAmlParser->bufListHead) {
            ptmp = (struct buffer_infos *)item;
            struct aml_audio_buffer *audioBuffer = (struct aml_audio_buffer *)ptmp->pBuffer;

            //free audioBuffer mallocked in parser_data_callback.
            aml_parser_free_memory(pAmlParser, audioBuffer->pData);
            aml_audio_free(audioBuffer);

            //free buffer_infos
            list_remove(&ptmp->lNode);
            ptmp->lNode.prev = NULL;
            ptmp->lNode.next = NULL;
            aml_audio_free(ptmp);
        }
    } else {
        AM_LOGI(" pAmlParser:%p buffer list is_empty:%d", pAmlParser, pAmlParser?list_empty(&pAmlParser->bufListHead):0);
    }

    if (pAmlParser != NULL) {
        int i = 0;
        aml_parser_func_t *pParserFunc = NULL;
        for (i = 0; i < AML_PARSER_MAX; ++i) {
            pParserFunc = pAmlParser->parserInfos[i].pFunc;
            if (pAmlParser->parserInfos[i].pHandle && pParserFunc && pParserFunc->f_flush) {
                pParserFunc->f_flush(pAmlParser->parserInfos[i].pHandle);
            }
        }
        aml_parser_destroy_cache_memory(pAmlParser);
    }

    return 0;
}

int aml_parser_get_buffer(aml_parser_t *pAmlParser, aml_audio_buffer_t **outBuffer, void **dataBuf)
{
    int retValue = AML_AUDIO_BUFFER_INVALID;
    if (pAmlParser != NULL && !list_empty(&pAmlParser->bufListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct buffer_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &pAmlParser->bufListHead) {
            ptmp = (struct buffer_infos *)item;
            //AM_LOGI(" item:%p ptmp:%p  lNode:%p pBuffer:%p", item, ptmp, &ptmp->lNode, ptmp->pBuffer);
            struct aml_audio_buffer *audioBuffer = (struct aml_audio_buffer *)ptmp->pBuffer;
            memcpy(*outBuffer, audioBuffer, sizeof(aml_audio_buffer_t));
            memcpy(*dataBuf, audioBuffer->pData, audioBuffer->size);
            retValue = AML_AUDIO_BUFFER_VALID;

            //free audioBuffer mallocked in parser_data_callback.
            aml_parser_free_memory(pAmlParser, audioBuffer->pData);
            aml_audio_free(audioBuffer);

            //free buffer_infos
            list_remove(&ptmp->lNode);
            ptmp->lNode.prev = NULL;
            ptmp->lNode.next = NULL;
            aml_audio_free(ptmp);
            break;
        }
    } else {
        //AM_LOGI(" buffer list is_empty:%d", list_empty(&pAmlParser->bufListHead));
        retValue = AML_AUDIO_BUFFER_IS_EMPTY;
    }

    return retValue;
}

int aml_parser_deinit(aml_parser_t *pAmlParser)
{
    if (pAmlParser != NULL) {
        int i = 0;
        aml_parser_func_t *pParserFunc = NULL;
        for (i = 0; i < AML_PARSER_MAX; ++i) {
            if (pAmlParser->parserInfos[i].pHandle) {
                pParserFunc = pAmlParser->parserInfos[i].pFunc;
                pParserFunc->f_deinit(pAmlParser->parserInfos[i].pHandle);

                //parser handle would be free in parser implementation code.
                //aml_audio_free(pAmlParser->parserInfos[i].pHandle);
                pAmlParser->parserInfos[i].pHandle = NULL;
            }
            if (pAmlParser->parserInfos[i].pFunc) {
                aml_audio_free(pAmlParser->parserInfos[i].pFunc);
                pAmlParser->parserInfos[i].pFunc = NULL;
            }
            pAmlParser->parserInfos[i].type = AML_PARSER_INVALID;

            if (pAmlParser->parserInfos[i].AudioBuffer) {
                aml_audio_free(pAmlParser->parserInfos[i].AudioBuffer);
                pAmlParser->parserInfos[i].AudioBuffer = NULL;
            }
        }

        //do flush, drop these buffer.
        aml_parser_flush(pAmlParser);
        aml_audio_free(pAmlParser);
        pAmlParser = NULL;
    }

    AM_LOGI(" done");
    return 0;
}

static void _init_parser_handles(aml_parser_t *pAmlParser)
{
    int i = 0;
    for (i = 0; i < AML_PARSER_MAX; ++i) {
        pAmlParser->parserInfos[i].pHandle = NULL;
        pAmlParser->parserInfos[i].pFunc = NULL;
        pAmlParser->parserInfos[i].AudioBuffer = NULL;
        pAmlParser->parserInfos[i].type = AML_PARSER_INVALID;
    }
    return;
}

static int _create_parser_and_config_parserinfo(aml_parser_t *pAmlParser, audio_format_t inFormat, bool isHwsyncParser)
{
    int retValue = -1;
    void *pParserHandle = NULL;
    int type = AML_PARSER_INVALID, parserType = AML_PARSER_INVALID;
    aml_parser_func_t *pParserFunc = NULL;
    parser_config_t *pParserConfig = &(pAmlParser->parserConfig);
    aml_audio_buffer_t *aBuffer = NULL;

    aBuffer = (struct aml_audio_buffer *)aml_audio_calloc(1, sizeof(aml_audio_buffer_t));
    if (aBuffer == NULL) {
        AM_LOGE("init parser failed, errno:%d %s\n", errno, strerror(errno));
        retValue = -1;
        goto err_alloc_aBuffer;
    }

    pParserFunc = _get_dynamic_parser_function(pAmlParser, inFormat, isHwsyncParser);
    if (pParserFunc) {
        //It is for unify f_init interface.
        if (pParserConfig->isHwsyncFlag) {
            pParserHandle = (void *)pParserConfig->pAmlStream;
        }
        retValue = pParserFunc->f_init(&pParserHandle);
    } else {
        AM_LOGE("get pParserFunc failed, errno:%d %s\n", errno, strerror(errno));
        retValue = -1;
        goto err_calloc_pFunc;

    }
    if (retValue < 0) {
        AM_LOGE("init parser failed, errno:%d %s\n", errno, strerror(errno));
        retValue = -1;
        goto err_init_parser;
    }
    type = _convert_format_to_parser_type(inFormat, isHwsyncParser);
    parserType = type;
    pAmlParser->parserInfos[type].type= parserType;
    pAmlParser->parserInfos[type].pFunc = pParserFunc;
    pAmlParser->parserInfos[type].pHandle = pParserHandle;
    pAmlParser->parserInfos[type].AudioBuffer = (void *)aBuffer;//aml_audio_buffer_t
    AM_LOGI(" parser pFunc:%p, type:%d %s, phandle:%p",
            pParserFunc, parserType, parserType2Str(parserType), pParserHandle);

    return retValue;

err_init_parser:
    if (pParserFunc != NULL) {
        aml_audio_free(pParserFunc);
        pParserFunc = NULL;
    }
err_calloc_pFunc:
    if (aBuffer != NULL) {
        aml_audio_free(aBuffer);
        aBuffer = NULL;
    }
err_alloc_aBuffer:
    return retValue;
}

int aml_parser_init(aml_parser_t **ppAmlParser, parser_config_t *pConfig)
{
    int retValue = -1;
    aml_parser_t *pAmlParser = NULL;

    pAmlParser = aml_audio_calloc(1, sizeof(aml_parser_t));
    if (pAmlParser == NULL) {
        AM_LOGE("malloc aml_parser failed, errno:%d %s\n", errno, strerror(errno));
        retValue = errno;
        goto err_calloc_parser;
    }

    _init_parser_handles(pAmlParser);

    AM_LOGI(" config isHwsyncFlag:%d pAmlStream:%p channel_count:%d,mask:0x%x, sampleRate:%d, format:0x%x sub_format:0x%x", pConfig->isHwsyncFlag, pConfig->pAmlStream,
        pConfig->dataFormat.channelCount,pConfig->dataFormat.channelMask, pConfig->dataFormat.sampleRate, pConfig->dataFormat.format, pConfig->dataFormat.subFormat);
    memcpy(&(pAmlParser->parserConfig), pConfig, sizeof(parser_config_t));

    parser_config_t *pParserConfig = &(pAmlParser->parserConfig);
    audio_format_t inFormat = pParserConfig->dataFormat.format;
    bool isHwsyncParser = pParserConfig->isHwsyncFlag;

    /*check whether hwsync need first*/
    if (isHwsyncParser) {
        retValue = _create_parser_and_config_parserinfo(pAmlParser, inFormat, isHwsyncParser);
        if (retValue < 0) {
            AM_LOGE("_create_parser_and_config_parserinfo failed, errno:%d %s\n", errno, strerror(errno));
            retValue = -1;
            goto err_create_parser;
        }
    }

    /*if it is we only need parser IEC format, not need to parser more*/
    if (is_raw_parser_support(pParserConfig->dataFormat.format)) {
        inFormat = pParserConfig->dataFormat.format;
        isHwsyncParser = false;//sub parser can't be hwsync type.
        retValue = _create_parser_and_config_parserinfo(pAmlParser, inFormat, isHwsyncParser);
        if (retValue < 0) {
            AM_LOGE("_create_parser_and_config_parserinfo failed, errno:%d %s\n", errno, strerror(errno));
            retValue = -1;
            goto err_create_parser;
        }
    }

    list_init(&pAmlParser->bufListHead);
    pthread_mutex_init(&pAmlParser->memoryLock, NULL);

    *ppAmlParser = pAmlParser;
    retValue = 0;
    AM_LOGI(" *ppAmlParser:%p done", *ppAmlParser);
    return retValue;

err_create_parser:
err_calloc_parser:
    aml_parser_deinit(pAmlParser);
    AM_LOGE(" come out error, abnormal exit");
    return retValue;
}


