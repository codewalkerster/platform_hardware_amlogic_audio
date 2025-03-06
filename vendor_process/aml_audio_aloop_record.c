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

#define LOG_TAG "aml_audio_aloop_record"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <cutils/log.h>
#include <audio_utils/primitives.h>
#include <audio_utils/channels.h>

#include "aml_audio_aloop_record.h"
#include "alsa_device_parser.h"
#include "aml_malloc_debug.h"
#include "audio_hw_utils.h"

int aml_audio_aloop_open(pcm_record_delay_t *aml_pcm_record_delay) {
    struct pcm *pcm = aml_pcm_record_delay->aloop_pcm;
    int card = 0, device = 0;
    struct pcm_config *pcm_cfg = &aml_pcm_record_delay->pcm_cfg;

    if (aml_pcm_record_delay->aloop_write_enable) {
        ALOGI("%s, aloop device is already open!\n", __func__);
        return 0;
    }

    if (pcm) {
        ALOGI("%s aloop device already opened, close it!\n", __func__);
        pthread_mutex_lock(&aml_pcm_record_delay->pcm_record_lock);
        pcm_close (pcm);
        aml_pcm_record_delay->aloop_pcm = NULL;
        pthread_mutex_unlock(&aml_pcm_record_delay->pcm_record_lock);
    }

    card = alsa_device_get_card_index_by_name("Loopback");
    if (card < 0) {
        ALOGI("%s,can not find snd-aloop\n",__func__);
        return -1;
    }

    /* set default pcm config if it doesn't be defined */
    if (!pcm_cfg->channels || !pcm_cfg->rate || !pcm_cfg->period_size || !pcm_cfg->period_count) {
        pcm_cfg->channels = 2;
        pcm_cfg->rate = 48000;
        pcm_cfg->format = PCM_FORMAT_S16_LE;
        pcm_cfg->period_size = 512;
        pcm_cfg->period_count = 8;
        pcm_cfg->start_threshold = pcm_cfg->period_size * pcm_cfg->period_count / 2;
    }

    pthread_mutex_lock(&aml_pcm_record_delay->pcm_record_lock);
    pcm = pcm_open(card, device, PCM_OUT | PCM_MONOTONIC, pcm_cfg);
    if (!pcm || !pcm_is_ready(pcm)) {
        ALOGE("%s: cannot open loopback: %s", __func__, pcm_get_error(pcm));
        pcm_close (pcm);
        pthread_mutex_unlock(&aml_pcm_record_delay->pcm_record_lock);
        return -EINVAL;
    }
    aml_pcm_record_delay->aloop_pcm = pcm;
    aml_pcm_record_delay->aloop_write_enable = true;
    pthread_mutex_unlock(&aml_pcm_record_delay->pcm_record_lock);

    ALOGI("%s, loopback card = %d, device = %d, pcm handle = %p\n", __func__, card, device, pcm);

    return 0;
}

int aml_audio_aloop_close(pcm_record_delay_t *aml_pcm_record_delay) {
    struct pcm *aloop_pcm = aml_pcm_record_delay->aloop_pcm;

    aml_pcm_record_delay->aloop_write_enable = false;
    if (!aloop_pcm) {
        ALOGI("no snd-aloop device, no need close\n");
        return -1;
    } else {
        ALOGI("%s(), pcm_close audio device aloop_pcm handle %p", __func__, aloop_pcm);
        pthread_mutex_lock(&aml_pcm_record_delay->pcm_record_lock);
        pcm_close (aloop_pcm);
        aml_pcm_record_delay->aloop_pcm = NULL;
        if (aml_pcm_record_delay->aloop_buf) {
            aml_audio_free(aml_pcm_record_delay->aloop_buf);
            aml_pcm_record_delay->aloop_buf = NULL;
            aml_pcm_record_delay->aloop_buf_size = 0;
        }
        ring_buffer_release(&aml_pcm_record_delay->delay_ringbuffer);
        aml_pcm_record_delay->last_delay_in_ms = 0;
        pthread_mutex_unlock(&aml_pcm_record_delay->pcm_record_lock);
    }

    return 0;
}

int aml_audio_aloop_write(pcm_record_delay_t *aml_pcm_record_delay, void *buffer, int bytes) {
    struct pcm *aloop_pcm = aml_pcm_record_delay->aloop_pcm;
    int ret = -1, write_bytes = bytes;

    if (!aloop_pcm || !buffer || bytes <= 0 || !aml_pcm_record_delay->aloop_write_enable) {
        return ret;
    }

    pthread_mutex_lock(&aml_pcm_record_delay->pcm_record_lock);

    if (aml_pcm_record_delay->aloop_buf_size < bytes) {
        void *tmp_addr = aml_audio_realloc(aml_pcm_record_delay->aloop_buf, bytes);
        aml_pcm_record_delay->aloop_buf = tmp_addr;
        aml_pcm_record_delay->aloop_buf_size = bytes;
        ALOGI("%s(), realloc buffer size: %d", __func__, bytes);
    }

    memcpy(aml_pcm_record_delay->aloop_buf, buffer, bytes);
    void *out_buffer = aml_pcm_record_delay->aloop_buf;

    /* always convert the audio data format to int16 */
    if (aml_pcm_record_delay->format == AUDIO_FORMAT_PCM_FLOAT) {
        memcpy_to_i16_from_float(out_buffer, out_buffer, bytes/sizeof(float));
        write_bytes /= 2;
    } else if (aml_pcm_record_delay->format == AUDIO_FORMAT_PCM_32_BIT) {
        memcpy_to_i16_from_i32(out_buffer, out_buffer, bytes/sizeof(int32_t));
        write_bytes /= 2;
    }

    /* always convert the audio output to stereo */
    if (aml_pcm_record_delay->channel_width != 2) {
        int channels = aml_pcm_record_delay->channel_width;
        if (aml_pcm_record_delay->channel_mask == AUDIO_CHANNEL_OUT_STEREO) {
            adjust_channels(out_buffer, channels, out_buffer, 2, sizeof(int16_t), write_bytes);
        } else {
            /* multi channel stream, always copy center channel to L/R */
            int frames = write_bytes / sizeof(int16_t) / channels;
            int16_t *buf = (int16_t *)out_buffer;
            for (int i = 0; i < frames; i++) {
                buf[2 * i] = buf[channels * i + 2];
                buf[2 * i + 1] = buf[channels * i + 2];
            }
        }
        write_bytes /= (channels >> 1);
    }

    ALOGV("%s(), bytes = %d, write_bytes = %d", __func__, bytes, write_bytes);
    ret = pcm_write(aloop_pcm, (void *)out_buffer, write_bytes);
    if (ret < 0) {
        const char *err_str = pcm_get_error(aloop_pcm);
        ALOGE("pcm_write ALoop failed ret = %d, pcm_get_error(port->pcm):%s", ret, err_str);
    }
    pthread_mutex_unlock(&aml_pcm_record_delay->pcm_record_lock);
    return 0;
}

int aml_audio_data_delay(pcm_record_delay_t *aml_pcm_record_delay, void *buffer, int bytes) {
    pcm_record_delay_t *aml_delay = aml_pcm_record_delay;
    int ret = -1;

    if (!buffer || bytes <= 0) {
        return ret;
    }

    if (!aml_pcm_record_delay->aloop_write_enable || !aml_delay->delay_in_ms) {
        aml_delay->last_delay_in_ms = 0;
        return 0;
    }

    pthread_mutex_lock(&aml_delay->pcm_record_lock);
    int need_delay_buffer_size = (aml_delay->pcm_cfg.rate / 1000 + 1) * sizeof(aml_delay->format) * aml_delay->channel_width * aml_delay->delay_in_ms;
    if (aml_delay->last_delay_in_ms != aml_delay->delay_in_ms || (need_delay_buffer_size + bytes) != aml_delay->delay_ringbuffer.size) {
        ALOGI("%s(), update audio delay: last_delay_in_ms = %d, delay_in_ms = %d, delay buffer size = %d, bytes = %d, ringbuffer size = %d",
            __func__, aml_delay->last_delay_in_ms, aml_delay->delay_in_ms, need_delay_buffer_size, bytes, aml_delay->delay_ringbuffer.size);

        ring_buffer_realloc(&aml_delay->delay_ringbuffer, need_delay_buffer_size + bytes);
        aml_delay->last_delay_in_ms = aml_delay->delay_in_ms;
    }

    ring_buffer_write(&aml_delay->delay_ringbuffer, (unsigned char*)buffer, bytes, UNCOVER_WRITE);
    if (get_buffer_read_space(&aml_delay->delay_ringbuffer) >= need_delay_buffer_size) {
        ring_buffer_read(&aml_delay->delay_ringbuffer, (unsigned char*)buffer, bytes);
    } else {
        memset(buffer, 0, bytes);
    }

    pthread_mutex_unlock(&aml_delay->pcm_record_lock);
    return 0;
}

