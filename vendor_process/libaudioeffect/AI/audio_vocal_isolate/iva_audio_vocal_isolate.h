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

#ifndef AAI_IVA_VOCAL_ISOLATE_H
#define AAI_IVA_VOCAL_ISOLATE_H

#include "iva.h"
#include "iva_audio_vocal_isolate_common.h"

#ifdef __cplusplus
extern "C" {
#endif

//initialize, return handle
__attribute ((visibility("default"))) extern aai_iva_karaoke_struct_t *aai_iva_karaoke_init(aai_iva_karaoke_param_t *pt_karaoke_param);

//de-init handle
__attribute ((visibility("default"))) extern IVA_STATUS_E aai_iva_karaoke_deinit(aai_iva_karaoke_struct_t *iva_karaoke_handle);

//reset handle
__attribute ((visibility("default"))) extern IVA_STATUS_E aai_iva_karaoke_reset(aai_iva_karaoke_struct_t *iva_karaoke_handle);

//set parameter
__attribute ((visibility("default"))) extern IVA_STATUS_E aai_iva_karaoke_param_set(aai_iva_karaoke_struct_t *iva_karaoke_handle,
                                const aai_iva_karaoke_param_t *pt_karaoke_param);

//get parameter
__attribute ((visibility("default"))) extern IVA_STATUS_E aai_iva_karaoke_param_get(aai_iva_karaoke_struct_t *iva_karaoke_handle,
                                aai_iva_karaoke_param_t *pt_karaoke_param);

//run karaoke frame by frame
__attribute ((visibility("default"))) extern IVA_STATUS_E aai_iva_karaoke_process(aai_iva_karaoke_struct_t *iva_karaoke_handle,
                                aai_iva_karaoke_input_t   *pt_karaoke_input,
                                aai_iva_karaoke_output_t  *pt_karaoke_output);

#ifdef __cplusplus
}
#endif

#endif
