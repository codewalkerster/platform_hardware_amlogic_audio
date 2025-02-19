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
#ifndef AML_AUDIO_SPEED_MANAGER_H
#define AML_AUDIO_SPEED_MANAGER_H

#define AML_AUDIO_TIMESTRETCH_SPEED_MIN 0.01f
#define AML_AUDIO_TIMESTRETCH_SPEED_MAX 4.0f

#define AML_AUDIO_SPEED_DETECT_GAP_TIME_MS   600

/*
 * Dump all channel data by default.
 *
 * Dump one channel data:
 * value   0x0F  -->  dump channel 0
 * value   0x1F  -->  dump channel 1
 * value   0x5F  -->  dump channel 5
 * value   0x6F  -->  dump channel 6
 * ...
 * value  0x11F  -->  dump channel 17
 *
*/
#define AML_SPEED_DUMP_ONE_CHANNEL_MASK    0xFF0
#define AML_SPEED_DUMP_ONE_CHANNEL_ENABLE    0xF

#define AML_SPEED_DUMP_DIRECTORY     "/data/vendor/audiohal/"


typedef enum {
    AML_AUDIO_SIMPLE_SPEED,
    AML_AUDIO_SONIC_SPEED,
} speed_type_t;


typedef struct audio_speed_config {
    int aformat;
    float speed;
    unsigned int input_sr;
    unsigned int channels;
} audio_speed_config_t;


typedef struct aml_audio_speed {
    speed_type_t speed_type;
    audio_speed_config_t speed_config;
    float speed_rate;
    unsigned int frame_bytes;
    size_t speed_size;   /*the speed data size*/
    size_t speed_buffer_size; /*the total buffer size*/
    void *speed_buffer;
    void * speed_handle;
    uint64_t total_in;
    uint64_t total_out;

    uint64_t curr_speed_in_frames;
    uint64_t curr_speed_out_frames;
    float last_speed_rate;
    int last_latency_frames; // base on speed 1.0f
    bool bypass_mode;        // active when speed changes to 1.0f, clear all latency info
    int debug_count;
} aml_audio_speed_t;


typedef struct audio_speed_func {
    int (*speed_open)(void **handle, audio_speed_config_t *speed_config);
    void (*speed_close)(void *handle);
    int (*speed_process)(void *handle, void * in_buffer, size_t bytes, void **p_out_buffer, size_t *p_out_buf_size, size_t *p_data_size);
    int (*speed_read)(void *handle, void *buffer, size_t bytes);
    int (*speed_flush)(void *handle);
    void (*set_speed)(void *handle, float speed);
    int (*speed_avail_frames)(void *handle);
} audio_speed_func_t;


/*
 * currently, tempo processing is on the head of ms12-continuous-node or submix-input-port
 * (its delay will be converted with speed 1.0f, here name it speed_post_delay).
 *
 * speed_post_delay may change large at a short time, after speed changes(e.g. speed 0.5f -> 2.5f)
 * need to smooth the transition and improve avsync performance
*/
typedef struct aml_audio_speed_post_delay {
    int      sample_rate;
    float    last_speed;
    int      last_buffer_frames;  // base on speed 1.0f
    uint64_t transition_time_ms;  // speed transition system time ms

    float    next_speed;
    bool     transitioning;
} aml_audio_speed_post_delay_t;


typedef struct aml_audio_speed_start_ts {
    pthread_mutex_t lock;
    struct timespec ts;
    uint64_t frames_position;
} aml_audio_speed_start_ts_t;


// apts gap statistic info
typedef struct aml_audio_speed_apts_gap {
    struct timespec start_ts;
    int64_t total_gap_ms;
    int total_gap_num;
    int duration_ms;
    int average_gap_ms;
} aml_audio_speed_apts_gap_t;


typedef struct aml_audio_speed_apts_gap_ease {
    bool start;
    float speed;
    int64_t target_frames;   // base on input, speed 1.0f
    int64_t current_frames;  // base on input, speed 1.0f
} aml_audio_speed_apts_gap_ease_t;


int aml_audio_speed_init(aml_audio_speed_t ** ppspeed_handle, speed_type_t speed_type, const audio_speed_config_t *speed_config);

int aml_audio_speed_close(aml_audio_speed_t * speed_handle);

int aml_audio_speed_process(aml_audio_speed_t * speed_handle, void * in_data, size_t size);

int aml_audio_speed_reset(aml_audio_speed_t * aml_audio_speed);
int aml_audio_speed_process_wrapper(aml_audio_speed_t **speed_handle,
    void *in_buffer, size_t in_bytes,
    void **ptr_out_buffer, size_t *ptr_out_bytes,
    const audio_speed_config_t *speed_config);

int aml_audio_speed_get_latency_frames(aml_audio_speed_t *speed_handle);
int aml_audio_speed_get_avail_frames(aml_audio_speed_t *speed_handle);
int aml_audio_speed_read(aml_audio_speed_t *speed_handle, void *buf, size_t read_size);
int aml_audio_speed_flush(aml_audio_speed_t *aml_audio_speed);

void aml_audio_speed_init_post_delay(aml_audio_speed_post_delay_t *p_delay, int sample_rate);
int aml_audio_speed_update_post_delay(aml_audio_speed_post_delay_t *p_delay, float speed, int last_buffer_frames);
int aml_audio_speed_calculate_post_delay(aml_audio_speed_post_delay_t *p_delay, int buffer_frames);

void aml_audio_speed_init_start_ts(aml_audio_speed_start_ts_t *p_speed_ts);
void aml_audio_speed_clear_start_ts(aml_audio_speed_start_ts_t *p_speed_ts);
bool aml_audio_speed_get_start_ts(aml_audio_speed_start_ts_t *p_speed_ts, struct timespec *p_ts, uint64_t *p_position);
int aml_audio_speed_update_start_ts(aml_audio_speed_start_ts_t *p_speed_ts, struct timespec *p_ts, uint64_t frames_position);

void aml_audio_speed_reset_apts_gap(aml_audio_speed_apts_gap_t *p_apts_gap, int duration_ms);
bool aml_audio_speed_add_apts_gap(aml_audio_speed_apts_gap_t *p_apts_gap, int gap_ms);
bool aml_audio_speed_get_apts_gap_average(aml_audio_speed_apts_gap_t *p_apts_gap, struct timespec *curr_mono_ts, int *p_average_ms);


#endif

