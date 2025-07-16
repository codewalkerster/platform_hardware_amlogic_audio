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

#ifndef _AML_STREAM_MANAGER_H_
#define _AML_STREAM_MANAGER_H_

#include <hardware/audio.h>
#include <cutils/list.h>

#include "audio_hw.h"


struct stream_infos {
    struct listnode lNode;
    const void *pStream;//consistent with "struct aml_stream_out *"
};

typedef enum aml_write_policy_type {
    AML_WRITE_POLICY_INVALID = -1,
    AML_WRITE_POLICY_APPROVAL,
    AML_WRITE_POLICY_REJECTION,

    AML_WRITE_POLICY_MAX,
} aml_write_policy_type_t;

typedef enum aml_release_policy_type {
    AML_RELEASE_POLICY_INVALID = -1,
    AML_RELEASE_POLICY_APPROVAL,
    AML_RELEASE_POLICY_REJECTION,

    AML_RELEASE_POLICY_MAX,
} aml_release_policy_type_t;


int aml_stream_check_hwsync_release_policy(struct aml_stream_out *amlStream);
int aml_check_spdif_write_policy(struct aml_stream_out *amlStream);
int aml_stream_check_dts_write_policy(struct aml_stream_out *amlStream);
bool aml_get_is_need_hw_mix(struct aml_stream_out *amlStream);
bool aml_get_is_exist_active_stream(void);
bool aml_is_preempt_deep_buffer_stream(struct aml_stream_out *amlStream);
void aml_check_close_ms12_output_main_stream(struct aml_stream_out *amlStream);

int aml_stream_register(struct aml_stream_out *amlStream);
void aml_stream_unregister(struct aml_stream_out *amlStream);
struct aml_stream_out * aml_get_main_active_stream(audio_format_t audio_format);

void aml_stream_check_preempt(struct aml_stream_out *amlStream);

void aml_init_stream_manager(struct aml_audio_device *adev);
bool aml_get_is_exist_active_mpegh_stream(void);
void aml_destroy_stream_manager(struct aml_audio_device *adev);

#endif //end of _AML_STREAM_MANAGER_H_