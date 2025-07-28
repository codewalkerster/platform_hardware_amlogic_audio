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

/*
 * audio enhancement handle class
 */
typedef struct Audio_EnhancementImplement_t *Audio_EnhancementImplement_handle_t;

/*
 * audio enhancement context struct
 */
typedef struct iva_audio_enhancement_strut
{
    /*
    * @brief audio enhancement context
    */
    Audio_EnhancementImplement_handle_t ctx_audio_enhancement;
} iva_audio_enhancement_strut_t;

/*
 * audio enhancement parameter struct
 */
typedef struct
{
    /* @brief
     * fs: input audio sample rate,only support 48000Hz.
     * bits_per_sample: only support 16 or 32.
     * chunk_size: the sample numbers ,process evry time. only support 480 or 512
     * model_path: dir for model files
     * value: -15-15db for enhance.
     */
    int fs;
    int bits_per_sample;
    int chunk_size;
    char model_path[256];
    int value;
    /* background audio gain from 0 to 1.0, default is 1.0 */
    float bg_gain;
    /* model size (1/2/3), smaller model type, lower cpu cost */
    int model_type;
} aai_iva_audio_enhancement_param_t;

/*
 * audio enhancement input struct
 */
typedef struct
{
    /*
     * @brief
     * samples:input PCM samples buffer
     *
     */
    short *samples;
    int   *int_samples;
    int   enable;
} aai_iva_audio_enhancement_input_t;

/*
 * audio enhancement output struct
 */
typedef struct
{
    /*
     * @brief
     * samples:output PCM samples buffer
     */
    short *samples;
    int *int_samples;
} aai_iva_audio_enhancement_output_t;

#endif /* _IVA_AUDIO_ENHANCEMENT_COMMON_H_ */
