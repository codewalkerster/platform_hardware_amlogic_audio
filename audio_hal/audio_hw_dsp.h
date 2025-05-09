/*
 * Copyright (C) 2017 Amlogic Corporation.
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
#ifndef _AUDIO_HW_DSP_H_
#define _AUDIO_HW_DSP_H_

enum soundtriggerevent {
    SOUND_TRIGGER_DEFAULT = 0,
    SOUND_TRIGGER_WAKEUP_KEYWORD = 1,
    SOUND_TRIGGER_WAKEUP_OTHER = 2,
    SOUND_TRIGGER_CLOSE_DEVICE = 3
};

struct pcm_open_config {
    unsigned int card;
    unsigned int device;
    unsigned int flags;
    struct pcm_config *config;
    void* dsp_pcm_handles[10];
};

typedef uint64_t xpointer;

typedef struct {
    uint32_t times;
    int32_t is_dsp_clk;
    int32_t is_arm_on;
    xpointer hdl;
} __attribute__((packed)) vad_awe_wakeup_dsp;


void send_pcm_open_config_dsp(unsigned int card,
        unsigned int device,
        unsigned int flags,
        struct pcm_config *config);

struct pcm_open_config*  get_pcm_open_config_sound_trigger();


void* pcm_open_dsp(unsigned int card,
        unsigned int device,
        unsigned int flags,
        struct pcm_config *config);

int pcm_close_dsp(void* hdl);
uint32_t pcm_client_bytes_to_frame_dsp(int sound_trigger_hdl_num, uint32_t bytes);

int alsa_device_update_pcm_index_dsp(int alsaPORT, int stream);

int pcm_read_dsp(void* hdl, void *data, unsigned int bytes);
int fetch_suspend_data_from_dsp(void* buf);
uint64_t pcm_get_timestamp_dsp(int sound_trigger_hdl_num, uint32_t sample_rate, unsigned int isOutput, uint64_t total_read, struct timespec ts);
void send_ffv_suspend_status(int sound_trigger_hdl_num, bool ffv_status);

void set_sound_trigger_cmd(int x);
int get_sound_trigger_cmd(void);
void callback_wakeup_event(void);
void aml_enable_ffv_to_dsp(bool enable_ffv);

#endif
