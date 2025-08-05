/*
 * Copyright (C) 2025 Amlogic Corporation.
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

#ifndef _AML_AUDIO_MPEGHPARSER_H_
#define _AML_AUDIO_MPEGHPARSER_H_

#include "aml_parser_common.h"

#define MIN_HEADER_SIZE                15
#define MHAS_SYNC_WORD_LENGTH          3
#define MAX_MPEGH_FRAME_LENGTH         (64*1024)

#define PACTYP_MPEGH3DACFG      1
#define PACTYP_MPEGH3DAFRAME    2
#define PACTYP_AUDIOTRUNCATION  17

typedef enum {
    STATE_FINDING_SYNC    = 0,     // Initial state: continuous syncword detection
    STATE_READING_HEADER  = 1,     // Reading packet header state
    STATE_READING_PAYLOAD = 2,     // Reading payload state
    STATE_END
} ParseState;

typedef struct {
    uint32_t frame_samples;
    uint32_t sample_rate;
    uint32_t nb_channels;
} mpegh_parser_info;

typedef struct {
    int32_t  frame_type;
    uint64_t label;
    uint64_t payload_length;
    int32_t  header_len;
    int32_t  sample_lens;
    int32_t  truncSamples;
    int32_t  sample_rate;
    int32_t  dur_samples;
} MhasHeader;

int aml_mpegh_parser_open(void **pparser_handle, void *pParserConfig);
int aml_mpegh_parser_close(void *parser_handle);
int aml_mpegh_parser_reset(void *parser_handle);
int aml_mpegh_parser_process(void *phandle, const void *inABuffer, void *outABuffer, void *parser_callback);
int aml_mpegh_parser_flush(void *parser_handle);
aml_parser_func_t *get_mpegh_parser_func_handle(void);

#endif /*_AML_AUDIO_MPEGHPARSER_H_*/

