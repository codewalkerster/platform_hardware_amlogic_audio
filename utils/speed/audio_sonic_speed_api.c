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
#include <stdlib.h>
#include "aml_malloc_debug.h"
#include "audio_sonic_speed_api.h"
#include "sonic_speed_wrapper.h"

/*
 * 1024 * 1024 * 8 bytes : protection for exception case, avoid running up out of system memory
 *
 * For 32bit float 8 channel pcm, is about 5461 ms and should be enough
*/
#define OUTPUT_BUF_MAX_SIZE (1024 * 1024 * 8)

#define MINUM_SPEED_OUTPUT_FRAMES 512


int sonic_speed_open(void **handle, audio_speed_config_t *speed_config)
{
    int ret = -1;
    sonic_speed_handle_t *speed = NULL;

    if (speed_config->aformat != AUDIO_FORMAT_PCM_16_BIT
        && speed_config->aformat != AUDIO_FORMAT_PCM_FLOAT
        && speed_config->aformat != AUDIO_FORMAT_PCM_32_BIT) {
        ALOGE("Not support Format =%d \n", speed_config->aformat);
        return -1;
    }

    speed = (sonic_speed_handle_t *)aml_audio_calloc(1, sizeof(sonic_speed_handle_t));
    if (speed == NULL) {
        ALOGE("malloc speed_para failed\n");
        return -1;
    }

    speed->speed  = speed_config->speed;
    speed->channels  = speed_config->channels;
    speed->input_sr  = speed_config->input_sr;
    speed->format = speed_config->aformat;

    ret = sonic_speed_init(speed,
                                speed->speed,
                                speed->input_sr,
                                speed->channels);

    if (ret < 0) {
        ALOGE("sonic_speed_init failed\n");
        goto exit;
    }

    *handle = speed;
    return 0;

exit:
    if (speed) {
        aml_audio_free(speed);
        *handle = 0;
    }
    ALOGE("sonic speed open failed\n");
    return -1;

}

void sonic_speed_close(void *handle)
{
    sonic_speed_handle_t *speed = (sonic_speed_handle_t *)handle;

    if (speed == NULL) {
        ALOGE("sonic speed is NULL\n");
        return;
    }
    ALOGD("speed close\n");
    sonic_speed_release(handle);
    aml_audio_free(speed);

    return;
}

int sonic_speed_process(void *handle, void * in_buffer, size_t bytes, void **p_out_buffer, size_t *p_out_buf_size, size_t *p_data_size)
{
    sonic_speed_handle_t *speed = NULL;
    int ret = -1;

    int framesize, speed_samples = 0, speed_frames = 0;
    int min_outsize = 0;
    size_t request_outsize = 0;

    speed = (sonic_speed_handle_t *)handle;

    if (handle == NULL || in_buffer == NULL || p_out_buffer == NULL || p_out_buf_size == NULL || p_data_size == NULL) {
        ALOGE("%s : invalid parameter !", __func__);
        return ret;
    }

    framesize = audio_bytes_per_sample(speed->format) * speed->channels;

    /*do speed for one period.*/
    sonic_speed_write(speed, (char *)in_buffer, bytes);
    min_outsize = MINUM_SPEED_OUTPUT_FRAMES * framesize;
    request_outsize = min_outsize + 2048;  // 2048 bytes for security distance

    do {
        if (request_outsize > OUTPUT_BUF_MAX_SIZE) {
            ALOGE("%s : request_outsize %zu bytes is too large !", __func__, request_outsize);
            ALOGE("%s : something wrong happened, please check !", __func__);
            break;
        }

        if (request_outsize > *p_out_buf_size) {
            *p_out_buffer = aml_audio_realloc(*p_out_buffer, request_outsize);
            if (*p_out_buffer == NULL) {
                ALOGE("%s : realloc out_buf failed !", __func__);
                *p_out_buf_size = 0;
                *p_data_size = 0;
                return -1;
            }
            ALOGD("%s realloc out_buf_size from %zu to %zu", __func__, *p_out_buf_size, request_outsize);
            *p_out_buf_size = request_outsize;
        }

        speed_samples  = sonic_speed_read(speed, (char *)*p_out_buffer + speed_frames * framesize, min_outsize);
        speed_frames  += speed_samples;
        request_outsize += min_outsize;
    } while (speed_samples >  0);

    ALOGV("input_size = %zu, speed_frames = %d \n", bytes, speed_frames);

    *p_data_size = speed_frames * framesize;
    if (*p_data_size > OUTPUT_BUF_MAX_SIZE) {
         ALOGW("%s : sonic_speed out_size  %zu overflow !!", __func__, *p_data_size);
         *p_data_size = OUTPUT_BUF_MAX_SIZE;
    }
    return 0;
}

audio_speed_func_t audio_sonic_speed_func = {
    .speed_open                 = sonic_speed_open,
    .speed_close                = sonic_speed_close,
    .speed_process              = sonic_speed_process,
    .speed_read                 = sonic_speed_read,
    .speed_flush                = sonic_speed_flush,
    .set_speed                  = sonic_set_speed,
    .speed_avail_frames         = sonic_speed_available_samples,
};

