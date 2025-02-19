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


#ifndef _DTV_PATCH_DTVSYNC_H_
#define _DTV_PATCH_DTVSYNC_H_


#include <stdbool.h>

#include "audio_hw_ms12.h"
#include "audio_mediasync_wrap.h"
#include "MediaSyncInterface.h"

typedef enum {
    DTVSYNC_AUDIO_DROP = 0,
    DTVSYNC_AUDIO_OUTPUT,
} dtvsync_process_res;

typedef enum {
    DTVSYNC_AUDIO_UNKNOWN = 0,
    DTVSYNC_AUDIO_NORMAL_OUTPUT,
    DTVSYNC_AUDIO_DROP_PCM,
    DTVSYNC_AUDIO_INSERT,
    DTVSYNC_AUDIO_HOLD,
    DTVSYNC_AUDIO_MUTE,
    DTVSYNC_AUDIO_RESAMPLE,
    DTVSYNC_AUDIO_ADJUST_CLOCK,
} dtvsync_policy;

typedef enum {
    DTVSYNC_TSYNC = 0,
    DTVSYNC_MEDIASYNC,
} dtvsync_type_t;

struct dtvsync_audio_policy {
    dtvsync_policy audiopolicy;
    int32_t  param1;
    int32_t  param2;
};

typedef struct  aml_dtvsync {
    void* mediasync;
    int mediasync_id;
    int64_t cur_outapts;
    int64_t out_start_apts;
    int64_t out_end_apts;
    int64_t last_queue_apts;
    int cur_speed;
    struct dtvsync_audio_policy apolicy;
    int pcm_dropping;
    int duration;
    pthread_mutex_t ms_lock;
    uint64_t last_package_pts;
    uint64_t last_lookup_apts;
} aml_dtvsync_t;

void* aml_dtvsync_create(aml_dtvsync_t *p_dtvsync);

bool aml_dtvsync_allocInstance(aml_dtvsync_t *p_dtvsync, int32_t* id);

bool aml_dtvsync_bindInstance(aml_dtvsync_t *p_dtvsync, uint32_t id);

bool aml_dtvsync_setParameter(aml_dtvsync_t *p_dtvsync, mediasync_parameter type, void* arg);


bool aml_dtvsync_getParameter(aml_dtvsync_t *p_dtvsync, mediasync_parameter type, void* arg);

bool aml_dtvsync_queue_audio_frame(aml_dtvsync_t *p_dtvsync, struct mediasync_audio_queue_info* info);

bool aml_dtvsync_audioprocess(aml_dtvsync_t *p_dtvsync, int64_t apts, int64_t cur_apts,
                                mediasync_time_unit tunit,
                                struct mediasync_audio_policy* asyncPolicy);


bool aml_dtvsync_insertpcm(struct audio_stream_out *stream, audio_format_t format, int time_ms, bool is_ms12);

bool aml_dtvsync_spdif_insertraw(struct audio_stream_out *stream,  void **spdifout_handle, int time_ms, int is_packed);

bool aml_audio_spdif_insertpcm(struct audio_stream_out *stream,  void **spdifout_handle, int time_ms);

bool aml_dtvsync_ms12_insert_pcm(void *priv_data, int time_ms, int pcm_type);

bool aml_dtvsync_ms12_insertraw(void *priv_data, int time_ms, audio_format_t output_format);

//bool aml_dtvsync_adjustclock(struct audio_stream_out *stream, struct mediasync_audio_policy *p_policy);

dtvsync_process_res aml_dtvsync_nonms12_process(struct audio_stream_out *stream, int duration, bool *speed_enabled);

void aml_dtvsync_ms12_get_policy(struct audio_stream_out *stream);

dtvsync_process_res aml_dtvsync_ms12_process_policy(void *priv_data, void *ms12_info);

bool aml_dtvsync_setPause(aml_dtvsync_t *p_dtvsync, bool pause);

bool aml_dtvsync_reset(aml_dtvsync_t *p_dtvsync);

void aml_dtvsync_release(aml_dtvsync_t *p_dtvsync);

//bool aml_dtvsync_ms12_adjust_clock(struct audio_stream_out *stream, int direct);

int aml_dtvsync_ms12_process_resample(struct audio_stream_out *stream, struct dtvsync_audio_policy *p_policy);
int aml_dtvsync_setPlaybackRate(aml_dtvsync_t *p_dtvsync,  float rate);

#endif /* _DTV_PATCH_DTVSYNC_H_ */

