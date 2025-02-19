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

#define LOG_TAG "audio_hw_utils_resampler"
// #define LOG_NDEBUG 0

#include <stdio.h>
#include <cutils/log.h>
#include "aml_malloc_debug.h"
#include "sonic_speed_wrapper.h"

int sonic_speed_init(sonic_speed_handle_t *handle,
                          float speed,
                          int sr,
                          int ch)
{

    float pitch = 1.0f;
    float rate = 1.0f;
    float volume = 1.0f;
    int emulateChordPitch = 1;
    int quality = 0;

    handle->stream = sonicCreateStream(sr, ch);
    sonicSetSpeed(handle->stream, speed);
    sonicSetPitch(handle->stream, pitch);
    sonicSetRate(handle->stream, rate);
    sonicSetVolume(handle->stream, volume);
    sonicSetChordPitch(handle->stream, emulateChordPitch);
    sonicSetQuality(handle->stream, quality);

    handle->speed   = speed;
    //sonicFlushStream(handle->stream);
    ALOGI("init sonic speed %f sr %d ch %d rate %f format %d", speed, sr ,ch, rate, handle->format);
    return 0;

}

static void aml_memcpy_to_i16_from_i32(int16_t *dst, const int32_t *src, size_t count)
{
    for (; count > 0; --count) {
        *dst++ = *src++ >> 16;
    }
}

static void aml_memcpy_to_i32_from_i16(int32_t *dst, const int16_t *src, size_t count)
{
    dst += count;
    src += count;
    for (; count > 0; --count) {
        *--dst = (int32_t)*--src << 16;
    }
}

int sonic_speed_write(sonic_speed_handle_t *handle, void *buf, size_t in_size) {
    int ret = -1, in_frame = 0;
    if (handle == NULL) {
        ALOGI("aml_speed_handle is NULL\n");
        return -1;
    }
    in_frame = in_size / audio_bytes_per_frame(handle->channels, handle->format);
    if (handle->format == AUDIO_FORMAT_PCM_FLOAT) {
        ret = sonicWriteFloatToStream(handle->stream, buf, in_frame);
    } else {
        if (handle->format == AUDIO_FORMAT_PCM_32_BIT) {
            // currently, sonic internal data use int16, api only support int16 and float pcm
            aml_memcpy_to_i16_from_i32(buf, buf, in_frame * handle->channels);
        }

        ret = sonicWriteShortToStream(handle->stream, buf, in_frame);
    }
    ALOGV("ret %d in_frame %d", ret, in_frame);
    return in_frame;
}

int sonic_speed_read(void *handle, void *buf, size_t read_size) {

    int samplesprocess;
    int read_frame;

    if (handle == NULL) {
        ALOGI("aml_speed_handle is NULL\n");
        return -1;
    }
    sonic_speed_handle_t *sonic_handle = (sonic_speed_handle_t *)handle;
    read_frame = read_size / audio_bytes_per_frame(sonic_handle->channels, sonic_handle->format);
    if (sonic_handle->format == AUDIO_FORMAT_PCM_FLOAT) {
        samplesprocess = sonicReadFloatFromStream(sonic_handle->stream, buf, read_frame);
    } else {
        samplesprocess = sonicReadShortFromStream(sonic_handle->stream, buf, read_frame);

        if (sonic_handle->format == AUDIO_FORMAT_PCM_32_BIT) {
            // currently, sonic internal data use int16, api only support int16 and float pcm
            aml_memcpy_to_i32_from_i16(buf, buf, samplesprocess * sonic_handle->channels);
        }
    }
    ALOGV("samplesprocess=%d\n", samplesprocess);
    return samplesprocess;

}

int sonic_speed_available_samples(void *handle)
{
    if (handle == NULL) {
        ALOGI("aml_speed_handle is NULL\n");
        return -1;
    }
    sonic_speed_handle_t *sonic_handle = (sonic_speed_handle_t *)handle;
    return sonicSamplesAvailable(sonic_handle->stream);
}

int sonic_speed_flush(void *handle)
{
    if (handle == NULL) {
        ALOGI("aml_speed_handle is NULL\n");
        return -1;
    }
    sonic_speed_handle_t *sonic_handle = (sonic_speed_handle_t *)handle;
    return sonicFlushStream(sonic_handle->stream);
}

void sonic_set_speed(void *handle, float speed)
{
    if (handle == NULL) {
        ALOGI("aml_speed_handle is NULL\n");
        return;
    }
    sonic_speed_handle_t *sonic_handle = (sonic_speed_handle_t *)handle;
    sonicSetSpeed(sonic_handle->stream, speed);
}

int sonic_speed_release(sonic_speed_handle_t *handle)
{
    if (handle == NULL) {
        ALOGI("aml_speed_handle is NULL\n");
        return -1;
    }
    if (handle) {
        sonicDestroyStream(handle->stream);
    }

    return 0;
}

