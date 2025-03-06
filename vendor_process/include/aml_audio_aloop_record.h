/*
* Copyright 2025 Amlogic Inc. All rights reserved.
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

#ifndef _AML_AUDIO_ALOOP_RECORD_H_
#define _AML_AUDIO_ALOOP_RECORD_H_

#include <hardware/audio.h>
#include <tinyalsa/asoundlib.h>
#include "aml_ringbuffer.h"

#define MAX_DELAY_TIME 5000

/* This feature is designed for real time subtitle, first write pcm data to aloop device */
/* and then store these audio data in ringbuffer for playback delay */
typedef struct pcm_record_delay{
    struct pcm *aloop_pcm;
    struct pcm_config pcm_cfg;
    pthread_mutex_t pcm_record_lock;

    bool aloop_write_enable;
    void *aloop_buf;
    int aloop_buf_size;
    int channel_width;
    audio_channel_mask_t channel_mask;
    audio_format_t format;

    int delay_in_ms;
    int last_delay_in_ms;
    ring_buffer_t delay_ringbuffer;
} pcm_record_delay_t;

int aml_audio_aloop_open(pcm_record_delay_t *aml_pcm_record_delay);
int aml_audio_aloop_close(pcm_record_delay_t *aml_pcm_record_delay);
int aml_audio_aloop_write(pcm_record_delay_t *aml_pcm_record_delay, void *buffer, int bytes);
int aml_audio_data_delay(pcm_record_delay_t *aml_pcm_record_delay, void *buffer, int bytes);

#endif /* _AML_AUDIO_ALOOP_RECORD_H_ */
