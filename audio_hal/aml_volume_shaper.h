/*
 * Copyright (C) 2021 Amlogic Corporation.
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
#ifndef _AML_VOLUME_SHAPER_H_
#define _AML_VOLUME_SHAPER_H_


#include <cutils/list.h>
#include <audio_utils/minifloat.h>

typedef struct aml_volume_shaper {
    struct listnode   list_head;
    struct listnode  *last_used_node;
    pthread_mutex_t   lock;
    bool              bDebug;
    bool              bInitialized;
    bool              bUseStartFrames;

    float             fLastUsedVolume;
    float             fFinalVolume;
    int               s32DelayFrames;
    int               s32SampleRate;
    uint64_t          u64LastEaseFrames;
    uint64_t          u64CurrentEaseFrames;
    uint64_t          u64StartFrames;
    uint64_t          u64StartTimeUs;

    int               s32WriteTimeMs;
} aml_volume_shaper_t;


/* Pick up -1.0f(magic number, outside of 0.0f~1.0f) as a invalid value. */
#define AML_AUDIO_GAIN_FLOAT_INVALID      (-GAIN_FLOAT_UNITY)

#define AML_VOLUME_DEBUG_BYPASS_MASK       0xF0000


int aml_volume_shaper_check_equal(float a, float b);
int aml_volume_shaper_check_sanity(float volume);

int aml_volume_shaper_empty(aml_volume_shaper_t *p_vol_shaper);
int aml_volume_shaper_list_each(aml_volume_shaper_t *p_vol_shaper);

int aml_volume_shaper_init(aml_volume_shaper_t *p_vol_shaper, int delay_samples);
int aml_volume_shaper_release(aml_volume_shaper_t *p_vol_shaper);
void aml_volume_shaper_set_delay_frames(aml_volume_shaper_t *p_vol_shaper, int delay_frames);
void aml_volume_shaper_update_start_frames(aml_volume_shaper_t *p_vol_shaper, uint64_t frames);
uint64_t aml_volume_shaper_get_current_frame(aml_volume_shaper_t *p_vol_shaper);
void aml_volume_shaper_update_write_time(aml_volume_shaper_t *p_vol_shaper, int time_ms);
bool aml_volume_shaper_update_moving_frame(aml_volume_shaper_t *p_vol_shaper, int frames);


int aml_volume_shaper_add(aml_volume_shaper_t *p_vol_shaper, float vol, uint64_t time_us);
int aml_volume_shaper_get(aml_volume_shaper_t *p_vol_shaper, int move_frames, float *p_volume, int *p_write_ms);

void aml_volume_shaper_enable_debug(aml_volume_shaper_t *p_vol_shaper, bool debug_enable) ;


#endif
