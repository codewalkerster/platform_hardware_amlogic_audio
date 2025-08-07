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

#ifndef AAI_IVA_VOCAL_ISOLATE_COMMON_H
#define AAI_IVA_VOCAL_ISOLATE_COMMON_H

typedef struct iva_karaoke_struct_t *karaoke_context_handle_t;

typedef struct aai_iva_karaoke_param {
    char model_path[256];
    char model_type[256];
    int debug_en;
} aai_iva_karaoke_param_t;

typedef struct aai_iva_karaoke_input {
    float *samples;
    int num_samples;
    int sample_rate;
    float vocal_gain;
} aai_iva_karaoke_input_t;

typedef struct aai_iva_karaoke_output {
    float *samples;
    int num_samples;
} aai_iva_karaoke_output_t;

typedef struct aai_iva_karaoke_struct {
    karaoke_context_handle_t ctx_karaoke;
} aai_iva_karaoke_struct_t;

 #endif
