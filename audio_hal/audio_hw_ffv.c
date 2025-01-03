/*
 * Copyright (C) 2010 Amlogic Corporation.
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

#define LOG_TAG "audio_hw_hal_ffv"

#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>


#include <dlfcn.h>
#include "audio_hw_dsp.h"
#include "audio_hw_ffv.h"
#include "audio_hw_utils.h"

#define SOUND_TRIGGER_HAL_LIBRARY_PATH "/vendor/lib/hw/sound_trigger.primary.amlogic.so"

int sound_trigger_open(struct aml_stream_in *in, unsigned int port, unsigned int card)
{
    struct aml_audio_device *adev = in->dev;

    unsigned int alsa_device = alsa_device_update_pcm_index_dsp(port, CAPTURE);
    send_pcm_open_config_dsp(card, alsa_device, PCM_IN | PCM_NONEBLOCK, &in->config);
    if (!adev->dsp_ffv->sound_trigger_open_for_streaming) {
        ALOGE("%s: No handle to sound trigger HAL", __func__);
        return -EIO;
    }
    in->pcm = NULL;
    in->dsp_ffv_in_t->sound_trigger_handle =
            adev->dsp_ffv->sound_trigger_open_for_streaming();
    if (in->dsp_ffv_in_t->sound_trigger_handle <= 0) {
        ALOGE("%s: Failed to open DSP for streaming", __func__);
        return -EIO;
    }
    ALOGV("Opened DSP successfully");
    return 0;
}

int sound_trigger_close(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;
    if (!adev->dsp_ffv->sound_trigger_close_for_streaming) {
        ALOGE("%s: No handle to sound trigger HAL", __func__);
        return -EIO;
    }
    adev->dsp_ffv->sound_trigger_close_for_streaming(in->dsp_ffv_in_t->sound_trigger_handle);
    in->dsp_ffv_in_t->sound_trigger_handle = 0;
    return 0;
}

int sound_trigger_to_suspend(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;
    if ((get_sound_trigger_cmd() == SOUND_TRIGGER_CLOSE_DEVICE) && (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC)) {
        int ret = sound_trigger_close(in);
        if (ret != 0)
            return ret;
        return 0;
    }
    if ((in->dsp_ffv_in_t->sound_trigger_handle > 0) && (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC)) {
        switch_to_suspend(in->dsp_ffv_in_t->sound_trigger_handle);
        adev->dsp_ffv->signal_thread = true;
        set_sound_trigger_cmd(SOUND_TRIGGER_DEFAULT);
    }
    return 0;
}

ssize_t in_read_from_fetch_buf(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    if (get_sound_trigger_cmd() == SOUND_TRIGGER_WAKEUP_KEYWORD) {
        if (in->dsp_ffv_in_t->fetch_buffer == NULL) {
            in->dsp_ffv_in_t->fetch_size = in->config.rate * (pcm_format_to_bits(in->config.format) >> 3) * in->config.channels * 2;
            in->dsp_ffv_in_t->fetch_buffer = aml_audio_malloc(in->dsp_ffv_in_t->fetch_size);
            in->dsp_ffv_in_t->fetch_size = fetch_suspend_data_from_dsp(in->dsp_ffv_in_t->fetch_buffer);
            in->dsp_ffv_in_t->fetched_size = 0;
        }
        ssize_t rd = MIN(bytes, in->dsp_ffv_in_t->fetch_size);
        memcpy(buffer, (void*)((char *)in->dsp_ffv_in_t->fetch_buffer + in->dsp_ffv_in_t->fetched_size), rd);
        if (getprop_bool("vendor.media.audiohal.indump")) {
            aml_dump_audio_bitstreams("/data/fetch_buffer.raw", buffer, rd);
        }
        if (rd >= 0) {
            in->frames_read += rd / (pcm_format_to_bits(in->config.format) >> 3) * in->config.channels;
        }
        in->dsp_ffv_in_t->fetched_size += rd;
        in->dsp_ffv_in_t->fetch_size -= rd;
        if (in->dsp_ffv_in_t->fetch_size == 0) {
            set_sound_trigger_cmd(SOUND_TRIGGER_DEFAULT);
            aml_audio_free(in->dsp_ffv_in_t->fetch_buffer);
            in->dsp_ffv_in_t->fetch_buffer = NULL;
        }
        return rd;
    }

    return 0;
}

int sound_trigger_read(struct aml_stream_in *in, void* buffer, size_t bytes, struct timespec *ts)
{
    int ret;
    struct aml_audio_device *adev = in->dev;
    if (!adev->dsp_ffv->sound_trigger_read_samples) {
        ALOGE("%s: No handle to sound trigger HAL", __func__);
        return -EIO;
    }
    clock_gettime(CLOCK_MONOTONIC, ts);
    ret = adev->dsp_ffv->sound_trigger_read_samples(in->dsp_ffv_in_t->sound_trigger_handle, buffer, bytes);
    if (ret <= 0) {
        ALOGE("fail to read ret=%d\n", ret);
    }
    return ret;
}

void dsp_ffv_stream_init(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;

    in->dsp_ffv_in_t = aml_audio_calloc(1, sizeof(struct dsp_ffv_in));
    in->dsp_ffv_in_t->fetch_buffer = NULL;
    in->dsp_ffv_in_t->sound_trigger_handle = 0;
    if ((in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) && adev->dsp_ffv->signal_thread) {
        adev->dsp_ffv->signal_thread = false;
    }
}

void dsp_ffv_stream_deinit(struct aml_stream_in *in)
{
    if (in->dsp_ffv_in_t)
        aml_audio_free(in->dsp_ffv_in_t);
}

void dsp_ffv_dev_init(struct aml_audio_device *adev)
{
    adev->dsp_ffv = aml_audio_calloc(1, sizeof(struct dsp_ffv_dev));
    adev->dsp_ffv->suspend_mode_fd = open("/sys/class/meson_pm/suspend_mode", O_RDWR);
    if (adev->dsp_ffv->suspend_mode_fd < 0) {
        ALOGE("%s suspend_mode open failed fd = %d", __func__, adev->dsp_ffv->suspend_mode_fd);
        return;
    }
    adev->dsp_ffv->suspend_mode_size = write(adev->dsp_ffv->suspend_mode_fd, "1\n", 1);
    if (adev->dsp_ffv->suspend_mode_size != 1) {
        ALOGE("%s suspend_mode write failed size = %d", __func__, adev->dsp_ffv->suspend_mode_size);
        return;
    }
    if (access(SOUND_TRIGGER_HAL_LIBRARY_PATH, R_OK) == 0) {
        adev->dsp_ffv->sound_trigger_lib = dlopen(SOUND_TRIGGER_HAL_LIBRARY_PATH,
                                         RTLD_NOW);
        if (adev->dsp_ffv->sound_trigger_lib == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__,
                  SOUND_TRIGGER_HAL_LIBRARY_PATH);
        } else {
            ALOGE("%s: DLOPEN successful for %s", __func__,
                  SOUND_TRIGGER_HAL_LIBRARY_PATH);
            adev->dsp_ffv->sound_trigger_open_for_streaming =
                    (int (*)(void))dlsym(adev->dsp_ffv->sound_trigger_lib,
                                         "sound_trigger_open_for_streaming");
            adev->dsp_ffv->sound_trigger_read_samples =
                    (size_t (*)(int, void *, size_t))dlsym(
                            adev->dsp_ffv->sound_trigger_lib,
                            "sound_trigger_read_samples");
            adev->dsp_ffv->sound_trigger_close_for_streaming =
                        (int (*)(int))dlsym(
                                adev->dsp_ffv->sound_trigger_lib,
                                "sound_trigger_close_for_streaming");
            if (!adev->dsp_ffv->sound_trigger_open_for_streaming ||
                !adev->dsp_ffv->sound_trigger_read_samples ||
                !adev->dsp_ffv->sound_trigger_close_for_streaming) {
                ALOGE("%s: Error grabbing functions in %s", __func__,
                      SOUND_TRIGGER_HAL_LIBRARY_PATH);
                adev->dsp_ffv->sound_trigger_open_for_streaming = 0;
                adev->dsp_ffv->sound_trigger_read_samples = 0;
                adev->dsp_ffv->sound_trigger_close_for_streaming = 0;
            }
        }
    }
}

void dsp_ffv_dev_deinit(struct aml_audio_device *adev)
{
    if (adev->dsp_ffv->suspend_mode_size == 1)
        close(adev->dsp_ffv->suspend_mode_fd);
    adev->dsp_ffv->suspend_mode_size = 0;
    if (adev->dsp_ffv)
        aml_audio_free(adev->dsp_ffv);
}

void get_vwe_wakeup_event(struct aml_audio_device *adev)
{
    if (adev->dsp_ffv->signal_thread) {
        callback_wakeup_event();
        set_sound_trigger_cmd(SOUND_TRIGGER_WAKEUP_KEYWORD);
    } else {
        set_sound_trigger_cmd(SOUND_TRIGGER_DEFAULT);
    }
}


