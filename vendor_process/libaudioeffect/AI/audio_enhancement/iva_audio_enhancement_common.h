/*
* Copyright 2023 Amlogic Inc. All rights reserved.
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

#ifndef _IVA_AUDIO_ENHANCEMENT_COMMON_H_
#define _IVA_AUDIO_ENHANCEMENT_COMMON_H_

/**
 * @brief audio enhancement handle class
 */
typedef struct Audio_EnhancementImplement_t *Audio_EnhancementImplement_handle_t;

/**
 * @brief audio enhancement context struct
 */
typedef struct iva_audio_enhancement_strut
{
    /**
    * @brief audio enhancement context
    */
    Audio_EnhancementImplement_handle_t ctx_audio_enhancement;
} iva_audio_enhancement_strut_t;

/**
 * @brief  audio enhancement parameter struct
 */
typedef struct
{
     /**
      * @brief
      * fs:input audio sample rate,only support 16000Hz,44100Hz,48000Hz,96000Hz.
      */
    int fs;
    int bits_per_sample;
    int chunk_size;
    char model_path[256];
} aai_iva_audio_enhancement_param_t;

/**
 * 2. audio enhancement input struct
 */
typedef struct
{
    /**
    * @brief
    * samples:input PCM samples buffer
    *
    */
    short *samples;
    int   *int_samples;
    int   enable;
} aai_iva_audio_enhancement_input_t;

/**
 * @brief audio enhancement output struct
 */
typedef struct
{
    /**
    * @brief // output PCM samples buffer
    */
    short *samples;
    int *int_samples;
} aai_iva_audio_enhancement_output_t;

#endif /* _IVA_AUDIO_ENHANCEMENT_COMMON_H_ */
