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

#ifndef _AML_AUDIO_ENHANCEMENT_H_
#define _AML_AUDIO_ENHANCEMENT_H_

#include "iva.h"
#include "iva_audio_enhancement.h"
#include "iva_audio_enhancement_common.h"

// max the processed channels
#define MAX_AUDIO_ENHANCEMENT_INSTANCE              (2)
#define MAX_AUDIO_ENHANCEMENT_GAIN                  (3)
#define MIN_AUDIO_ENHANCEMENT_GAIN                  (1)

typedef struct audio_enhancement_libraries_context_s {
    //dlopen libaaisdk.so
    void *dl_handle;

    iva_audio_enhancement_strut_t *nnans_imple[MAX_AUDIO_ENHANCEMENT_INSTANCE];
    aai_iva_audio_enhancement_input_t t_audio_enhancement_input;
    aai_iva_audio_enhancement_output_t t_audio_enhancement_output;

    iva_audio_enhancement_strut_t *(*iva_init)(aai_iva_audio_enhancement_param_t *pt_enhancement_param);
    void (*iva_deinit)(iva_audio_enhancement_strut_t *enhancement_imple);
    void (*iva_process)(iva_audio_enhancement_strut_t *enhancement_imple,
        aai_iva_audio_enhancement_input_t *pt_enhancement_input,
        aai_iva_audio_enhancement_output_t *pt_enhancement_output);
    void (*iva_process_int32)(iva_audio_enhancement_strut_t *enhancement_imple,
        aai_iva_audio_enhancement_input_t *pt_enhancement_input,
        aai_iva_audio_enhancement_output_t *pt_enhancement_output);
    void (*iva_enable)(iva_audio_enhancement_strut_t *enhancement_imple, bool enable);
    void (*iva_clear)(iva_audio_enhancement_strut_t *enhancement_imple);
    void (*iva_setparam)(iva_audio_enhancement_strut_t *enhancement_imple,
        aai_iva_audio_enhancement_param_t *pt_enhancement_param);
    void (*iva_getparam)(iva_audio_enhancement_strut_t *enhancement_imple,
        aai_iva_audio_enhancement_param_t *pt_enhancement_param);
} audio_enhancement_libraries_context_t;

typedef struct aml_audio_enhancement_module_s {
    audio_enhancement_libraries_context_t iva_enhancement_handle;
    aai_iva_audio_enhancement_param_t stEnhancementParam;

    audio_config_base_t audio_config;
    /* channel width 2ch for non-ms12 and 8ch for ms12 */
    int channel_width;
    /* stereo stream processes L/R; multich stream processes center channel */
    audio_channel_mask_t channel_mask;
    int processed_channel_layout;

    bool audio_enhancement_enable;
    int audio_enhancement_gain;
    void *input_samples;
    void *output_samples;

    ring_buffer_t input_rbuffer;
    ring_buffer_t output_rbuffer;

    /*process one block 512 frames, framessize (sizeof(int32_t) * ch )*/
    void *process_buffer;
    int process_buffer_size;

} aml_audio_enhancement_module_t;

int aml_open_audio_enhancement_module(struct aml_native_postprocess *native_postprocess, audio_config_base_t *audio_config, int channel_width);
int aml_audio_enhancement_module_process(void *handle, audio_buffer_t *inBuf, audio_buffer_t *outBuf);
int aml_close_audio_enhancement_module(struct aml_native_postprocess *native_postprocess);
int aml_set_audio_enhancement_enable(struct aml_native_postprocess *native_postprocess, bool enable);
int aml_set_audio_enhancement_gain(struct aml_native_postprocess *native_postprocess, int gain_db);
int aml_get_audio_enhancement_gain(struct aml_native_postprocess *native_postprocess);
int aml_get_audio_enhancement_enable(struct aml_native_postprocess *native_postprocess);
int aml_update_audio_channel_mask(struct aml_native_postprocess *native_postprocess, audio_channel_mask_t channel_mask);

#endif /* _AML_AUDIO_ENHANCEMENT_H_ */