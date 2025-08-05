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

#ifndef _AML_PARSER_MANAGER_H_
#define _AML_PARSER_MANAGER_H_

#include <cutils/list.h>
#include <hardware/audio.h>
#include <system/audio.h>
#include "aml_ringbuffer.h"
#include "aml_malloc_debug.h"
#include "aml_dump_debug.h"
#include "aml_parser_common.h"

//current not use callback, between parser and stream.
//#define USE_CALLBACK_FOR_PARSER_TO_STREAM

#define AML_PARSER_CACHE_MEMORY_NUM      4

typedef enum aml_parser_type {
    AML_PARSER_INVALID = -1,
    AML_PARSER_HWSYNC = 0,
    AML_PARSER_IEC,
    AML_PARSER_AC3,
    AML_PARSER_AC4,
    AML_PARSER_MAT,
    AML_PARSER_TRUEHD,
    AML_PARSER_DTS,
    AML_PARSER_DTSHD,
    AML_PARSER_HEAAC,
    AML_PARSER_MPEGH,

    AML_PARSER_MAX,
} aml_parser_type_t;

typedef enum aml_audio_buffer_state {
    AML_AUDIO_BUFFER_INVALID = -1,
    AML_AUDIO_BUFFER_IS_EMPTY,
    AML_AUDIO_BUFFER_VALID,

    AML_AUDIO_BUFFER_MAX,
} aml_audio_buffer_state_t;


struct buffer_infos {
    struct listnode lNode;
    const void *pBuffer;//consistent with "aml_audio_buffer_t"
} buffer_infos_t;

typedef struct parser_info {
    void *pHandle;
    void *pFunc;
    void *AudioBuffer;//aml_audio_buffer_t for output data parsed
    int  type;
} parser_info_t;

typedef struct parser_memory_item {
    void *pBuffer;
    bool inUsed;
    size_t bufferSize;
} parser_memory_item_t;


typedef struct aml_parser {
    parser_info_t parserInfos[AML_PARSER_MAX];
    parser_config_t parserConfig;
    void *pWriteCallback;
    data_format_t parsedFormat;
    uint64_t outApts;
    struct listnode bufListHead;
    pthread_mutex_t memoryLock;
    parser_memory_item_t memoryPool[AML_PARSER_CACHE_MEMORY_NUM];
} aml_parser_t;


int aml_parser_process(aml_parser_t *pAmlParser, const void *aBuffer, void *pCallback);
int aml_parser_get_format(aml_parser_t *pAmlParser);
int aml_parser_reset(aml_parser_t *pAmlParser);
int aml_parser_flush(aml_parser_t *pAmlParser);
int aml_parser_get_buffer(aml_parser_t *pAmlParser, aml_audio_buffer_t **outBuffer, void **dataBuf);

int aml_parser_deinit(aml_parser_t *pAmlParser);
int aml_parser_init(aml_parser_t **ppAmlParser, parser_config_t *pConfig);



#endif //_AML_PARSER_MANAGER_H_
