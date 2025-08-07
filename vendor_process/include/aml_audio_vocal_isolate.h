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

#ifndef _AML_AUDIO_VOCAL_ISOLATE_H_
#define _AML_AUDIO_VOCAL_ISOLATE_H_

#include "iva.h"
#include "iva_audio_vocal_isolate_common.h"
#include "iva_audio_vocal_isolate.h"

typedef struct audio_vocal_isolate_libraries_context_s {
    //dlopen libaaisdk.so
    void *dl_handle;

    aai_iva_karaoke_struct_t *iva_karaoke_handle;
    aai_iva_karaoke_input_t t_audio_karaoke_input;
    aai_iva_karaoke_output_t t_audio_karaoke_output;

    aai_iva_karaoke_struct_t *(*iva_init)(aai_iva_karaoke_param_t *pt_karaoke_param);

    IVA_STATUS_E (*iva_deinit)(aai_iva_karaoke_struct_t *iva_karaoke_handle);

    IVA_STATUS_E (*iva_reset)(aai_iva_karaoke_struct_t *iva_karaoke_handle);

    IVA_STATUS_E (*iva_setparam)(aai_iva_karaoke_struct_t *iva_karaoke_handle,
                                 aai_iva_karaoke_param_t *pt_karaoke_param);

    IVA_STATUS_E (*iva_getparam)(aai_iva_karaoke_struct_t *iva_karaoke_handle,
                                 aai_iva_karaoke_param_t *pt_karaoke_param);

    IVA_STATUS_E (*iva_process)(aai_iva_karaoke_struct_t *iva_karaoke_handle,
                                aai_iva_karaoke_input_t *pt_karaoke_input,
                                aai_iva_karaoke_output_t *pt_karaoke_output);

} audio_vocal_isolate_libraries_context_t;

typedef struct aml_audio_vocal_isolate_module_s {
    audio_vocal_isolate_libraries_context_t iva_vocal_isolate_handle;
    aai_iva_karaoke_param_t iva_karaoke_param;

    audio_config_base_t audio_config;
    audio_channel_mask_t channel_mask;
    int channel_width;

    bool audio_vocal_isolate_enable;
    float audio_vocal_gain;

    void *input_samples;
    void *output_samples;

    unsigned int total_input_sample;
    unsigned int total_output_sample;
    unsigned int total_sample;

    ring_buffer_t input_rbuffer;
    ring_buffer_t output_rbuffer;

    void *process_buffer;
    int process_buffer_size;

    //pthread APIs
    pthread_t iva_thread_id;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool req_thread_exit;

    //unify the volume gain
    int music_gain;
    float volume_gain;
    float compensation_gain;
} aml_audio_vocal_isolate_module_t;

int aml_open_audio_vocal_isolate_module(struct aml_native_postprocess *native_postprocess, audio_config_base_t *audio_config);
int aml_close_audio_vocal_isolate_module(struct aml_native_postprocess *native_postprocess);
int aml_set_audio_vocal_isolate_enable(struct aml_native_postprocess *native_postprocess, bool enable);
int aml_get_audio_vocal_isolate_enable(struct aml_native_postprocess *native_postprocess);
int aml_audio_vocal_isolate_module_process(void *handle, audio_buffer_t *inBuf, audio_buffer_t *outBuf);
int aml_set_audio_vocal_gain(struct aml_native_postprocess *native_postprocess, float gain);
float aml_get_audio_vocal_gain(struct aml_native_postprocess *native_postprocess);
int aml_update_audio_vocal_isolate_volume(struct aml_native_postprocess *native_postprocess);

#endif /* _AML_AUDIO_VOCAL_ISOLATE_H_S */
