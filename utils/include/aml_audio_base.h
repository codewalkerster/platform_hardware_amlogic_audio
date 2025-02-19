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

#ifndef __AML_AUDIO_BASE_H__
#define __AML_AUDIO_BASE_H__

#include <system/audio.h>


//16 times of pcm Buffer size.
#define AUDIO_BUFFER_DATA_SIZE (8192*16)

typedef enum aml_audio_buffer_type {
    AUDIO_BUFFER_INVALID = -1,
    AUDIO_BUFFER_OUT_PCM,
    AUDIO_BUFFER_OUT_CONVERT_RAW,
    AUDIO_BUFFER_OUT_RAW,

    AUDIO_BUFFER_MAX
} aml_audio_buffer_type_t;

typedef struct buffer_data_format {
    uint32_t sampleRate;
    audio_format_t format;
    uint32_t channelCount;
    audio_channel_mask_t channelMask;
} buffer_data_format_t;

/*this audio buffer is more important for hwsync.
**it includes buffer and corresponding apts.
*/
typedef struct aml_audio_buffer {
    void *pData;
    uint32_t size;//unit is byte
    buffer_data_format_t bufFormat;
    uint64_t apts;
    bool isAptsValid;

    //it can be used by dtv/hdmi
    bool isPassthroughMode;
    bool isDtv; //dtv es_data_block
    void *privObject;//private object, it is a data block that can point to dtv/hdmi data.
} aml_audio_buffer_t;

typedef struct aml_audio_buffer_info {
    aml_audio_buffer_t *inBuffer;
    aml_audio_buffer_t *parsedBuffer;
    aml_audio_buffer_t *outBuffer[AUDIO_BUFFER_MAX];
} aml_audio_buffer_info_t;


#endif //__AML_AUDIO_BASE_H__

