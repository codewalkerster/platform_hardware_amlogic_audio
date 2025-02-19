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

#define LOG_TAG "audio_hw_utils_speed"
// #define LOG_NDEBUG 0

#include <cutils/log.h>
#include <string.h>
#include <stdlib.h>
#include <system/audio.h>
#include <inttypes.h>
#include <math.h>

#include "aml_malloc_debug.h"
#include "audio_sonic_speed_api.h"
#include "aml_dump_debug.h"
#include "aml_async_write.h"


// Speed output buffer initial frame count
#define SPEED_OUTPUT_INITIAL_FRAME_COUNT  1536

static bool is_speed_equal(float a, float b);
static void aml_audio_speed_dump_data(void *buffer, size_t bytes, audio_format_t format, int channels, bool is_input);
static int64_t aml_audio_speed_calc_time_interval_us(struct timespec *ts_start, struct timespec *ts_end);

static audio_speed_func_t * get_speed_function(speed_type_t speed_type)
{
    switch (speed_type) {
    case AML_AUDIO_SIMPLE_SPEED:
        return NULL;
        break;
    case AML_AUDIO_SONIC_SPEED:
        return &audio_sonic_speed_func;
        break;

    default:
        return NULL;
    }

    return NULL;
}

int aml_audio_speed_init(aml_audio_speed_t ** ppaml_audio_speed, speed_type_t speed_type, const audio_speed_config_t *speed_config)
{
    int ret = -1;

    aml_audio_speed_t *aml_audio_speed = NULL;
    audio_speed_func_t * speed_func = NULL;

    if (speed_config == NULL) {
        ALOGE("speed_config is NULL\n");
        return -1;
    }

    if (speed_config->channels == 0 ||
        speed_config->input_sr == 0 ||
        speed_config->speed == 0) {
        ALOGE("Invalid speed config\n");
        return -1;
    }


    if (speed_config->aformat != AUDIO_FORMAT_PCM_16_BIT
        && speed_config->aformat != AUDIO_FORMAT_PCM_FLOAT
        && speed_config->aformat != AUDIO_FORMAT_PCM_32_BIT) {
        ALOGE("%s Not supported audio format = 0x%x\n", __func__, speed_config->aformat);
        return -1;
    }

    aml_audio_speed = (aml_audio_speed_t *)aml_audio_calloc(1, sizeof(aml_audio_speed_t));

    if (aml_audio_speed == NULL) {
        ALOGE("malloc aml_audio_speed failed\n");
        return -1;
    }

    memcpy(&aml_audio_speed->speed_config, speed_config, sizeof(audio_speed_config_t));

    speed_func = get_speed_function(speed_type);

    if (speed_func == NULL) {
        ALOGE("speed_func is NULL\n");
        goto exit;
    }

    aml_audio_speed->speed_type = speed_type;

    aml_audio_speed->speed_rate = (float)speed_config->speed;

    aml_audio_speed->frame_bytes = audio_bytes_per_sample(speed_config->aformat) * speed_config->channels;

    // This size will dynamic grow during playback
    aml_audio_speed->speed_buffer_size =  aml_audio_speed->frame_bytes * SPEED_OUTPUT_INITIAL_FRAME_COUNT;

    aml_audio_speed->speed_buffer = aml_audio_calloc(1, aml_audio_speed->speed_buffer_size);

    aml_audio_speed->bypass_mode = false;

    if (aml_audio_speed->speed_buffer == NULL) {
        ALOGE("speed_buffer is NULL\n");
        goto exit;
    }

    ret = speed_func->speed_open(&aml_audio_speed->speed_handle, &aml_audio_speed->speed_config);
    if (ret < 0) {
        ALOGE("speed_open failed\n");
        goto exit;

    }

    * ppaml_audio_speed = aml_audio_speed;

    return 0;

exit:

    if (aml_audio_speed->speed_buffer) {
        aml_audio_free(aml_audio_speed->speed_buffer);
        aml_audio_speed->speed_buffer = NULL;
    }

    if (aml_audio_speed) {
        aml_audio_free(aml_audio_speed);
    }
    * ppaml_audio_speed = NULL;
    return -1;

}

int aml_audio_speed_close(aml_audio_speed_t * aml_audio_speed)
{

    audio_speed_func_t * speed_func = NULL;

    if (aml_audio_speed == NULL) {
        ALOGE("speed_handle is NULL\n");
        return -1;
    }

    speed_func = get_speed_function(aml_audio_speed->speed_type);
    if (speed_func == NULL) {
        ALOGE("speed_func is NULL\n");
    }

    if (speed_func) {
        speed_func->speed_close(aml_audio_speed->speed_handle);
    }

    if (aml_audio_speed->speed_buffer) {
        aml_audio_free(aml_audio_speed->speed_buffer);
        aml_audio_speed->speed_buffer = NULL;
    }

    aml_audio_free(aml_audio_speed);

    return 0;
}

int aml_audio_speed_process(aml_audio_speed_t * aml_audio_speed, void * in_data, size_t size)
{
    size_t out_size = 0;
    int ret = -1;
    int latency_frames = 0;
    uint64_t out_speed_1X_frames = 0;
    unsigned int frame_bytes = 0;
    audio_speed_func_t * speed_func = NULL;
    int in_frames = 0;
    int out_frames = 0;

    if (aml_audio_speed == NULL) {
        ALOGE("speed_handle is NULL\n");
        return -1;
    }

    frame_bytes = aml_audio_speed->frame_bytes;
    if (frame_bytes == 0) {
        ALOGE("%s : invalid frame_bytes 0 !", __func__);
        return -1;
    }

    speed_func = get_speed_function(aml_audio_speed->speed_type);
    if (speed_func == NULL) {
        ALOGE("speed_func is NULL\n");
        return -1;
    }

    memset(aml_audio_speed->speed_buffer, 0, aml_audio_speed->speed_buffer_size);

    ret = speed_func->speed_process(aml_audio_speed->speed_handle, in_data, size, \
                                    &aml_audio_speed->speed_buffer, &aml_audio_speed->speed_buffer_size, &out_size);
    if (ret < 0) {
        aml_audio_speed->speed_size = 0;
        ALOGE("speed error=%d, output size=%zu, buf size=%zu\n",
            ret, out_size, aml_audio_speed->speed_buffer_size);
        return ret;
    }

    aml_audio_speed->speed_size = out_size;
    aml_audio_speed->total_in += size;
    aml_audio_speed->total_out += out_size;

    in_frames = size/frame_bytes;
    out_frames = out_size/frame_bytes;
    aml_audio_speed->curr_speed_in_frames += in_frames;
    aml_audio_speed->curr_speed_out_frames += out_frames;

    // update latency info
    if (!is_speed_equal(aml_audio_speed->last_speed_rate, aml_audio_speed->speed_rate)) {
        float last_speed = aml_audio_speed->last_speed_rate;
        int cache_frames = aml_audio_speed->last_latency_frames;
        aml_audio_speed->curr_speed_in_frames = in_frames + cache_frames;
        aml_audio_speed->curr_speed_out_frames = out_frames;
        aml_audio_speed->last_speed_rate = aml_audio_speed->speed_rate;

        ALOGI("%s: speed_rate update from %f to %f", __func__, last_speed, aml_audio_speed->speed_rate);
        ALOGI("%s: curr_speed_in_frames %" PRId64 ", (in_frames %d, cache_frames %d), curr_speed_out_frames %" PRId64 " ", __func__,
            aml_audio_speed->curr_speed_in_frames, in_frames, cache_frames, aml_audio_speed->curr_speed_out_frames);
    }
    out_speed_1X_frames = aml_audio_speed->curr_speed_out_frames * aml_audio_speed->speed_rate;
    aml_audio_speed->last_latency_frames = aml_audio_speed->curr_speed_in_frames - out_speed_1X_frames;

    //ALOGE("total rate=%f\n",(float)aml_audio_speed->total_out/(float)aml_audio_speed->total_in);
    return 0;
}

int aml_audio_speed_reset(aml_audio_speed_t * aml_audio_speed)
{
    int ret = -1;

    audio_speed_func_t * speed_func = NULL;

    if (aml_audio_speed == NULL) {
        ALOGE("speed_handle is NULL\n");
        return -1;
    }

    speed_func = get_speed_function(aml_audio_speed->speed_type);
    if (speed_func == NULL) {
        ALOGE("speed_func is NULL\n");
    }

    if (speed_func && aml_audio_speed->speed_handle) {
        speed_func->speed_close(aml_audio_speed->speed_handle);

        ret = speed_func->speed_open(&aml_audio_speed->speed_handle, &aml_audio_speed->speed_config);
        if (ret < 0) {
            ALOGE("speed_reset failed\n");
            return -1;

        }
    }
    aml_audio_speed->total_in = 0;
    aml_audio_speed->total_out = 0;
    aml_audio_speed->curr_speed_in_frames = 0;
    aml_audio_speed->curr_speed_out_frames = 0;
    aml_audio_speed->last_speed_rate = 0;
    aml_audio_speed->last_latency_frames = 0;
    aml_audio_speed->bypass_mode = false;

    ALOGI("%s", __FUNCTION__);
    return 0;
}


static bool aml_audio_speed_prepare_bypass(aml_audio_speed_t *aml_speed, void *in_buffer, size_t in_bytes, float speed)
{
    int ret = 0;
    int avail_frames = 0;
    int avail_bytes = 0;
    int read_bytes = 0;
    int read_frame = 0;
    int new_buf_size = read_bytes;

    if (!is_speed_equal(speed, 1.0f) || aml_speed == NULL) {
        return false;
    }

    /*
     * All latency information or data will be reset, only speed 1.0f support.
    */
    aml_audio_speed_flush(aml_speed);
    avail_frames = aml_audio_speed_get_avail_frames(aml_speed);
    ALOGI("%s : sonic flush, avail_frames %d, last_latency_frames %d", __func__, avail_frames, aml_speed->last_latency_frames);

    avail_bytes = avail_frames * aml_speed->frame_bytes;
    read_bytes = avail_bytes + 8192;       // // 8192 bytes for security distance
    new_buf_size = read_bytes + in_bytes;  // sonic internal data + input data bytes.

    if (new_buf_size > aml_speed->speed_buffer_size) {
        aml_speed->speed_buffer = aml_audio_realloc(aml_speed->speed_buffer, new_buf_size);
        if (aml_speed->speed_buffer == NULL) {
            ALOGE("%s realloc speed_buffer fail !", __func__);
            aml_speed->speed_buffer_size = 0;
            return false;
        }
        ALOGD("%s realloc speed_buffer_size from %zu to %d\n", __func__, aml_speed->speed_buffer_size, new_buf_size);
        aml_speed->speed_buffer_size = new_buf_size;
    }

    read_frame = aml_audio_speed_read(aml_speed, aml_speed->speed_buffer, read_bytes);
    ALOGI("%s : sonic read_frame %d, curr_latency_frames %d", __func__, read_frame, aml_speed->last_latency_frames);

    read_bytes = read_frame * aml_speed->frame_bytes;
    memcpy((uint8_t *)aml_speed->speed_buffer + read_bytes, in_buffer, in_bytes);
    aml_speed->speed_size = read_bytes + in_bytes;

    aml_speed->curr_speed_in_frames = 0;
    aml_speed->curr_speed_out_frames = 0;
    aml_speed->last_speed_rate = 1.0f;
    aml_speed->last_latency_frames = 0;
    aml_speed->bypass_mode = true;
    return true;
}


int aml_audio_speed_process_wrapper(aml_audio_speed_t **speed_handle,
    void *in_buffer, size_t in_bytes,
    void **ptr_out_buffer, size_t *ptr_out_bytes,
    const audio_speed_config_t *speed_config)
{
    int ret = 0;
    float speed = 0;

    if (speed_handle == NULL || in_buffer == NULL || ptr_out_buffer == NULL || ptr_out_bytes == NULL || speed_config == NULL) {
        ALOGE("%s : invalid parameter !", __func__);
        return -1;
    }
    speed = speed_config->speed;
    *ptr_out_buffer = in_buffer;
    *ptr_out_bytes = in_bytes;

    aml_audio_speed_dump_data(in_buffer, in_bytes, speed_config->aformat, speed_config->channels, true);

    if (*speed_handle) {
        (*speed_handle)->debug_count++;
        if (!is_speed_equal(speed, (*speed_handle)->speed_config.speed)) {
            aml_audio_speed_t *aml_speed = *speed_handle;
            audio_speed_func_t *speed_func = get_speed_function(aml_speed->speed_type);
            if (speed_func && speed_func->set_speed) {
                ALOGD("speed is changed from %f to %f\n", aml_speed->speed_config.speed, speed);
                speed_func->set_speed(aml_speed->speed_handle, speed);
                aml_speed->speed_config.speed = speed;
                aml_speed->speed_rate = speed;

                if (aml_audio_speed_prepare_bypass(aml_speed, in_buffer, in_bytes, speed)) {
                    *ptr_out_buffer = aml_speed->speed_buffer;
                    *ptr_out_bytes = aml_speed->speed_size;
                    aml_audio_speed_dump_data(*ptr_out_buffer, *ptr_out_bytes, speed_config->aformat, speed_config->channels, false);
                    return ret;
                } else {
                    aml_speed->bypass_mode = false;
                }
            } else {
                ALOGD("speed is changed from %f to %f, reset the speed \n",(*speed_handle)->speed_config.speed, speed);
                aml_audio_speed_close(*speed_handle);
                *speed_handle = NULL;
            }
        }

        if (speed_handle && (*speed_handle)->bypass_mode) {
            if (get_debug_value(AML_DEBUG_AUDIOHAL_SPEED) && ((*speed_handle)->debug_count % 10 == 0)) {
                ALOGD("%s bypass mode", __func__);
            }
            aml_audio_speed_dump_data(*ptr_out_buffer, *ptr_out_bytes, speed_config->aformat, speed_config->channels, false);
            return 0;
        }
    }

    if (*speed_handle == NULL) {
        ALOGI("init speed to %f \n", speed);
        ret = aml_audio_speed_init((aml_audio_speed_t **)speed_handle, AML_AUDIO_SONIC_SPEED, speed_config);
        if (ret < 0) {
            ALOGE("resample init error\n");
            return -1;
        }
    }

    ret = aml_audio_speed_process(*speed_handle, in_buffer, in_bytes);
    if (ret < 0) {
        ALOGE("speed process error\n");
        return -1;
    }
    *ptr_out_buffer = (*speed_handle)->speed_buffer;
    *ptr_out_bytes = (*speed_handle)->speed_size;
    aml_audio_speed_dump_data(*ptr_out_buffer, *ptr_out_bytes, speed_config->aformat, speed_config->channels, false);

    return ret;
}


// base on speed 1.0f, 48khz
int aml_audio_speed_get_latency_frames(aml_audio_speed_t *aml_audio_speed)
{
    int latency_frames = 0;
    //int debug_enable = get_debug_value(AML_DEBUG_AUDIOHAL_SPEED);

    if (aml_audio_speed == NULL) {
        return 0;
    }

    latency_frames = aml_audio_speed->last_latency_frames;
    /*if (latency_frames < 0) {
        if (debug_enable) {
            ALOGD("%s : invalid latency_frames %d, use 0", __func__, latency_frames);
        }
        latency_frames = 0;
    }*/
    return latency_frames;
}

int aml_audio_speed_get_avail_frames(aml_audio_speed_t *aml_audio_speed)
{
    int avail_frames = 0;
    audio_speed_func_t * speed_func = NULL;

    if (aml_audio_speed == NULL) {
        ALOGE("speed_handle is NULL\n");
        return -1;
    }

    speed_func = get_speed_function(aml_audio_speed->speed_type);
    if (speed_func == NULL) {
        ALOGE("%s speed_func is NULL\n");
    }

    if (speed_func) {
        avail_frames = speed_func->speed_avail_frames(aml_audio_speed->speed_handle);
    }
    return avail_frames;
}

int aml_audio_speed_read(aml_audio_speed_t *aml_audio_speed, void *buf, size_t read_size)
{
    int read_frames = 0;
    int frame_bytes = 0;
    uint64_t out_speed_1X_frames = 0;
    audio_speed_func_t *speed_func = NULL;

    if (aml_audio_speed == NULL) {
        ALOGE("speed_handle is NULL\n");
        return -1;
    }

    speed_func = get_speed_function(aml_audio_speed->speed_type);
    if (speed_func == NULL) {
        ALOGE("%s speed_func is NULL\n");
    }
    frame_bytes = aml_audio_speed->frame_bytes;

    if (speed_func) {
        read_frames = speed_func->speed_read(aml_audio_speed->speed_handle, buf, read_size);
        aml_audio_speed->total_out += (read_frames * aml_audio_speed->frame_bytes);
        aml_audio_speed->curr_speed_out_frames += read_frames;

        out_speed_1X_frames = aml_audio_speed->curr_speed_out_frames * aml_audio_speed->speed_rate;
        aml_audio_speed->last_latency_frames = aml_audio_speed->curr_speed_in_frames - out_speed_1X_frames;
    }
    return read_frames;
}

int aml_audio_speed_flush(aml_audio_speed_t *aml_audio_speed)
{
    audio_speed_func_t *speed_func = NULL;

    if (aml_audio_speed == NULL) {
        ALOGE("speed_handle is NULL\n");
        return -1;
    }

    speed_func = get_speed_function(aml_audio_speed->speed_type);
    if (speed_func == NULL) {
        ALOGE("%s speed_func is NULL\n");
    }

    if (speed_func) {
        speed_func->speed_flush(aml_audio_speed->speed_handle);
    }
    return 0;
}


void aml_audio_speed_init_post_delay(aml_audio_speed_post_delay_t *p_delay, int sample_rate)
{
    if (p_delay == NULL) {
        return;
    }
    memset(p_delay, 0, sizeof(*p_delay));
    p_delay->sample_rate = sample_rate;
    p_delay->last_speed = 1.0f;
    p_delay->next_speed = 1.0f;
    p_delay->transitioning = false;
}

static bool is_speed_equal(float a, float b)
{
    const double PRECISION = 1e-08;
    double da = a;
    double db = b;
    return (fabs(da - db) < PRECISION);
}

// time unit : ms
static uint64_t aml_audio_speed_get_systime(void)
{
    struct timespec ts;
    uint64_t sys_time;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    sys_time = (uint64_t)ts.tv_sec * 1000000LL + (uint64_t)ts.tv_nsec / 1000LL;
    return sys_time/1000LL;
}

static int64_t aml_audio_speed_calc_time_interval_us(struct timespec *ts_start, struct timespec *ts_end)
{
    int64_t start_us, end_us;
    int64_t interval_us;


    start_us = ts_start->tv_sec * 1000000LL +
               ts_start->tv_nsec / 1000LL;

    end_us   = ts_end->tv_sec * 1000000LL +
               ts_end->tv_nsec / 1000LL;

    interval_us = end_us - start_us;

    return interval_us;
}



int aml_audio_speed_update_post_delay(aml_audio_speed_post_delay_t *p_delay, float speed, int last_buffer_frames)
{
    if (p_delay == NULL || speed < 0.0f || last_buffer_frames < 0) {
        ALOGE("%s : Invalid parameter (%p, %f, %d)", __func__, p_delay, speed, last_buffer_frames);
        return -1;
    }
    if (is_speed_equal(speed, p_delay->next_speed)) {
        return 0;
    }

    p_delay->last_buffer_frames = last_buffer_frames;
    p_delay->transition_time_ms = aml_audio_speed_get_systime();
    p_delay->next_speed = speed;
    p_delay->transitioning = true;
    ALOGI("%s speed %.3f last_buffer_frames %d", __func__, speed, last_buffer_frames);
    return 0;
}

int aml_audio_speed_calculate_post_delay(aml_audio_speed_post_delay_t *p_delay, int buffer_frames)
{
    uint64_t curr_time_ms = 0;
    int past_time_ms = 0;
    int buffer_duration_ms = 0;
    int remain_frames = 0;
    int buffer_frame_speed = 0;

    if (p_delay == NULL || p_delay->sample_rate <= 0) {
        ALOGE("%s : Invalid parameter (p_delay %p, sample_rate %d)", __func__, p_delay, p_delay->sample_rate);
        return buffer_frames;
    }
    if (!p_delay->transitioning) {
        return buffer_frames * p_delay->next_speed;
    }

    curr_time_ms = aml_audio_speed_get_systime();
    buffer_duration_ms = p_delay->last_buffer_frames * 1000 / p_delay->sample_rate;
    if (curr_time_ms <= p_delay->transition_time_ms) {
        return buffer_frames * p_delay->last_speed;
    }

    past_time_ms = curr_time_ms - p_delay->transition_time_ms;
    if (past_time_ms >= buffer_duration_ms || p_delay->transition_time_ms == 0) {
        p_delay->transitioning = false;
        p_delay->last_speed = p_delay->next_speed;
        p_delay->last_buffer_frames = 0;
        p_delay->transition_time_ms = 0;
        return buffer_frames * p_delay->next_speed;
    }

    remain_frames = (buffer_duration_ms - past_time_ms) * p_delay->sample_rate / 1000;
    if (remain_frames > buffer_frames) {
        remain_frames = buffer_frames;
    } else if (remain_frames < 0) {
        remain_frames = 0;
    }
    buffer_frame_speed = remain_frames * p_delay->last_speed + (buffer_frames - remain_frames) * p_delay->next_speed;

    return buffer_frame_speed;
}


void aml_audio_speed_init_start_ts(aml_audio_speed_start_ts_t *p_speed_ts)
{
    if (p_speed_ts == NULL) {
        return;
    }
    memset(&p_speed_ts->ts, 0, sizeof(p_speed_ts->ts));
    pthread_mutex_init(&p_speed_ts->lock, NULL);
    ALOGI("%s", __func__);
}


void aml_audio_speed_clear_start_ts(aml_audio_speed_start_ts_t *p_speed_ts)
{
    if (p_speed_ts == NULL) {
        return;
    }
    pthread_mutex_lock(&p_speed_ts->lock);
    memset(&p_speed_ts->ts, 0, sizeof(p_speed_ts->ts));
    p_speed_ts->frames_position = 0;
    pthread_mutex_unlock(&p_speed_ts->lock);
    ALOGI("%s", __func__);
}

bool aml_audio_speed_get_start_ts(aml_audio_speed_start_ts_t *p_speed_ts, struct timespec *p_ts, uint64_t *p_position)
{
    if (p_speed_ts == NULL || p_ts == NULL || p_position == NULL) {
        return false;
    }

    pthread_mutex_lock(&p_speed_ts->lock);
    if (p_speed_ts->ts.tv_sec != 0 || p_speed_ts->ts.tv_nsec != 0) {
        memcpy(p_ts, &p_speed_ts->ts, sizeof(p_speed_ts->ts));
        *p_position = p_speed_ts->frames_position;
        pthread_mutex_unlock(&p_speed_ts->lock);
        return true;
    }
    pthread_mutex_unlock(&p_speed_ts->lock);
    return false;
}

int aml_audio_speed_update_start_ts(aml_audio_speed_start_ts_t *p_speed_ts, struct timespec *p_ts, uint64_t frames_position)
{
    if (p_speed_ts == NULL || p_ts == NULL) {
        return -1;
    }

    if (p_speed_ts->ts.tv_sec != 0 || p_speed_ts->ts.tv_nsec != 0) {
        return 0;
    }
    pthread_mutex_lock(&p_speed_ts->lock);
    p_speed_ts->ts = *p_ts;
    p_speed_ts->frames_position = frames_position;
    pthread_mutex_unlock(&p_speed_ts->lock);
    return 0;
}

void aml_audio_speed_reset_apts_gap(aml_audio_speed_apts_gap_t *p_apts_gap, int duration_ms)
{
    if (p_apts_gap == NULL) {
        return;
    }
    memset(p_apts_gap, 0, sizeof(*p_apts_gap));
    p_apts_gap->duration_ms = duration_ms;
}

bool aml_audio_speed_add_apts_gap(aml_audio_speed_apts_gap_t *p_apts_gap, int gap_ms)
{
    if (p_apts_gap == NULL) {
        return false;
    }
    if (abs(gap_ms) > 80) {
        ALOGE("%s : invalid gap_ms %d, drop it", __func__, gap_ms);
        return false;
    }

    if (p_apts_gap->total_gap_num == 0) {
        clock_gettime(CLOCK_MONOTONIC, &p_apts_gap->start_ts);
    }
    p_apts_gap->total_gap_ms += gap_ms;
    p_apts_gap->total_gap_num += 1;
    return true;
}


bool aml_audio_speed_get_apts_gap_average(aml_audio_speed_apts_gap_t *p_apts_gap, struct timespec *curr_mono_ts, int *p_average_ms)
{
    int64_t past_time_ms = 0;
    int average_ms = 0;
    int debug_enable = get_debug_value(AML_DEBUG_AUDIOHAL_SPEED);

    *p_average_ms = 0;
    if (p_apts_gap == NULL || p_apts_gap->total_gap_num <= 2) {
        return false;
    }
    past_time_ms = aml_audio_speed_calc_time_interval_us(&p_apts_gap->start_ts, curr_mono_ts)/1000;
    if (past_time_ms < p_apts_gap->duration_ms) {
        return false;
    }

    *p_average_ms = (p_apts_gap->total_gap_ms/p_apts_gap->total_gap_num);
    if (debug_enable) {
        ALOGI("apts_gap_average %d, total_gap_ms %" PRId64 ", total_gap_num %d", *p_average_ms, p_apts_gap->total_gap_ms, p_apts_gap->total_gap_num);
    }
    return true;
}

static void aml_audio_speed_dump_data(void *buffer, size_t bytes, audio_format_t format, int channels, bool is_input)
{
    int dump_value = get_debug_value(AML_DUMP_AUDIOHAL_SPEED);
    int ch_index = -1;
    char filepath[80];
    const char *inout_str = "in";

    if (dump_value == 0 || buffer == NULL || bytes == 0) {
        return;
    }
    if (!is_input) {
        inout_str = "out";
    }
    memset(filepath, 0, sizeof(filepath));

    if ((dump_value & AML_SPEED_DUMP_ONE_CHANNEL_ENABLE) == AML_SPEED_DUMP_ONE_CHANNEL_ENABLE) {
        ch_index = (dump_value & AML_SPEED_DUMP_ONE_CHANNEL_MASK) >> 4;
        if (ch_index >= channels) {
            ch_index = -1;  // invalid channel index, dump all channel data
        }
    }

    if (ch_index >= 0) {
        snprintf(filepath, sizeof(filepath)-1, "%s/speed_%s_ch%d.pcm", AML_SPEED_DUMP_DIRECTORY, inout_str, ch_index);
        aml_async_dump_1ch_16bit_pcm(buffer, bytes, format, channels, ch_index, filepath);
    } else {
        snprintf(filepath, sizeof(filepath)-1, "%s/speed_%s.pcm", AML_SPEED_DUMP_DIRECTORY, inout_str);
        aml_async_dump_data(buffer, bytes, filepath);
    }
}
