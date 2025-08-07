/*
* Copyright 2023 Amlogic Inc. All rights reserved.
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

#define LOG_TAG "aml_audio_vocal_isolate"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <pthread.h>
#include <cutils/log.h>
#include <audio_utils/primitives.h>

#include "audio_post_process.h"
#include "aml_ringbuffer.h"
#include "audio_hw_utils.h"
#include "aml_audio_vocal_isolate.h"
#include "aml_volume_utils.h"

//dump debug: setprop vendor.media.audiohal.aiaudio.dump 1
#define AUDIO_VOCAL_ISOLATE_DUMP_INPUT    "/data/vendor/audiohal/input_vocal_isolate.pcm"
#define AUDIO_VOCAL_ISOLATE_DUMP_OUTPUT   "/data/vendor/audiohal/output_vocal_isolate.pcm"
#define AUDIO_VOCAL_ISOLATE_DUMP_BEFORE   "/data/vendor/audiohal/before_vocal_isolate.pcm"
#define AUDIO_VOCAL_ISOLATE_DUMP_AFTER    "/data/vendor/audiohal/after_vocal_isolate.pcm"

#define MAX_AUDIO_VOCAL_ISOLATE_INSTANCE            (2)       // stereo process
#define MAX_AUDIO_VOCAL_ISOLATE_CHANNEL             (10)      // max support channels: 7.1.2
#define AUDIO_VOCAL_ISOLATE_FRAMECOUNT              (512)     // Unit: frames, 10ms
#define AUDIO_VOCAL_ISOLATE_FRAME                   (9600)    // max 200ms

//############ IVA Audio Vocal Isolate library APIs #############//
static int iva_libraries_open(audio_vocal_isolate_libraries_context_t *ivaLibContext, const char *lib_path)
{
    void* dl_handle = dlopen(lib_path, RTLD_NOW);
    if (!dl_handle) {
        ALOGE("%s() dlopen() %s error:%s", __func__, lib_path, dlerror());
        return -EINVAL;
    }

    ivaLibContext->iva_init = dlsym(dl_handle, "aai_iva_karaoke_init");
    if (!ivaLibContext->iva_init) {
        ALOGE("%s() dlsym() aai_iva_karaoke_init error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_deinit = dlsym(dl_handle, "aai_iva_karaoke_deinit");
    if (!ivaLibContext->iva_deinit) {
        ALOGE("%s() dlsym() aai_iva_karaoke_deinit error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_process = dlsym(dl_handle, "aai_iva_karaoke_process");
    if (!ivaLibContext->iva_process) {
        ALOGE("%s() dlsym() aai_iva_karaoke_process error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_reset = dlsym(dl_handle, "aai_iva_karaoke_reset");
    if (!ivaLibContext->iva_reset) {
        ALOGE("%s() dlsym() aai_iva_karaoke_reset error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_setparam = dlsym(dl_handle, "aai_iva_karaoke_param_set");
    if (!ivaLibContext->iva_setparam) {
        ALOGE("%s() dlsym() aai_iva_karaoke_param_set error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_getparam = dlsym(dl_handle, "aai_iva_karaoke_param_get");
    if (!ivaLibContext->iva_getparam) {
        ALOGE("%s() dlsym() aai_iva_karaoke_param_get error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->dl_handle = dl_handle;
    ALOGI("%s() parse iva library symbol SUCCESS!", __func__);
    return 0;
exit:
    ivaLibContext->dl_handle = NULL;
    dlclose(dl_handle);
    return -EINVAL;
}

static int iva_libraries_close(audio_vocal_isolate_libraries_context_t *ivaLibContext)
{
    if (ivaLibContext->iva_init) {
        ivaLibContext->iva_init = NULL;
    }
    if (ivaLibContext->iva_deinit) {
        ivaLibContext->iva_deinit = NULL;
    }
    if (ivaLibContext->iva_process) {
        ivaLibContext->iva_process = NULL;
    }
    if (ivaLibContext->iva_reset) {
        ivaLibContext->iva_reset = NULL;
    }
    if (ivaLibContext->iva_setparam) {
        ivaLibContext->iva_setparam = NULL;
    }
    if (ivaLibContext->iva_getparam) {
        ivaLibContext->iva_getparam = NULL;
    }
    if (ivaLibContext->dl_handle) {
        dlclose(ivaLibContext->dl_handle);
        ivaLibContext->dl_handle = NULL;
    }
    return 0;
}

static int aml_audio_vocal_isolate_deinit(aml_audio_vocal_isolate_module_t *pAudioVocalIsolateModule)
{
    if (!pAudioVocalIsolateModule) {
        ALOGE("%s() invalid param for audio vocal isolate deinit", __func__);
        return -1;
    }

    audio_vocal_isolate_libraries_context_t *pAudioVocalIsolateHandle = &pAudioVocalIsolateModule->iva_vocal_isolate_handle;

    if (pAudioVocalIsolateModule) {
        if (pAudioVocalIsolateHandle->iva_karaoke_handle && pAudioVocalIsolateHandle->iva_deinit) {
            pAudioVocalIsolateHandle->iva_deinit(pAudioVocalIsolateHandle->iva_karaoke_handle);
        }
        iva_libraries_close(pAudioVocalIsolateHandle);
    }
    if (pAudioVocalIsolateModule->input_samples) {
        free(pAudioVocalIsolateModule->input_samples);
        pAudioVocalIsolateModule->input_samples = NULL;
    }
    if (pAudioVocalIsolateModule->output_samples) {
        free(pAudioVocalIsolateModule->output_samples);
        pAudioVocalIsolateModule->output_samples = NULL;
    }
    if (pAudioVocalIsolateModule->process_buffer) {
        free(pAudioVocalIsolateModule->process_buffer);
        pAudioVocalIsolateModule->process_buffer = NULL;
    }

    ring_buffer_release(&pAudioVocalIsolateModule->input_rbuffer);
    ring_buffer_release(&pAudioVocalIsolateModule->output_rbuffer);

    free(pAudioVocalIsolateModule);
    return 0;
}

static void *iva_lib_thread_loop(void *arg)
{
    aml_audio_vocal_isolate_module_t *pAudioVocalIsolateModule = (aml_audio_vocal_isolate_module_t *)arg;
    audio_vocal_isolate_libraries_context_t *pAudioVocalIsolateHandle = &pAudioVocalIsolateModule->iva_vocal_isolate_handle;
    struct ring_buffer *input_rbuffer = &pAudioVocalIsolateModule->input_rbuffer;
    struct ring_buffer *output_rbuffer = &pAudioVocalIsolateModule->output_rbuffer;
    int ret = 0;

    ALOGI("+%s() enter", __func__);
    while (true) {
        pthread_mutex_lock(&pAudioVocalIsolateModule->mutex);
        if (pAudioVocalIsolateModule->req_thread_exit) {
            ALOGI("%s() exit!", __func__);
            pthread_mutex_unlock(&pAudioVocalIsolateModule->mutex);
            break;
        }
        audio_format_t format = pAudioVocalIsolateModule->audio_config.format;
        uint32_t src_frame_size = audio_bytes_per_sample(format) * pAudioVocalIsolateModule->channel_width;
        int process_byte = AUDIO_VOCAL_ISOLATE_FRAMECOUNT * src_frame_size;
        int len = get_buffer_read_space(input_rbuffer);
        if (len < process_byte) {
            struct timespec ts;
            ts_wait_time_us(&ts, 5 * 1000);
            pthread_cond_timedwait(&pAudioVocalIsolateModule->cond, &pAudioVocalIsolateModule->mutex, &ts);
            ALOGV("%s() leave wait: process_byte = %d, read space = %d", __func__, process_byte, len);
            pthread_mutex_unlock(&pAudioVocalIsolateModule->mutex);
        } else {
            pthread_mutex_unlock(&pAudioVocalIsolateModule->mutex);
            unsigned char *u8PackedOutBuf = (unsigned char *)pAudioVocalIsolateModule->process_buffer;
            int nChannels = pAudioVocalIsolateModule->channel_width;

            aai_iva_karaoke_input_t t_audio_karaoke_input;
            aai_iva_karaoke_output_t t_audio_karaoke_output;
            t_audio_karaoke_input.sample_rate = 48000;
            t_audio_karaoke_input.vocal_gain = pAudioVocalIsolateModule->audio_vocal_gain;
            t_audio_karaoke_input.num_samples = AUDIO_VOCAL_ISOLATE_FRAMECOUNT;
            t_audio_karaoke_output.num_samples = 0;

            pAudioVocalIsolateModule->total_input_sample += AUDIO_VOCAL_ISOLATE_FRAMECOUNT;

            ALOGV("%s() input: gain = %f, read space = %d, num_samples = %d, total_sample = %d, total_input_sample = %d",
                __func__, t_audio_karaoke_input.vocal_gain, len / src_frame_size, t_audio_karaoke_input.num_samples,
                pAudioVocalIsolateModule->total_sample, pAudioVocalIsolateModule->total_input_sample);

            ring_buffer_read(input_rbuffer, u8PackedOutBuf, process_byte);

            float *s32PackedInBuf = (float *)pAudioVocalIsolateModule->process_buffer;
            float *s32PackedOutBuf = (float *)pAudioVocalIsolateModule->process_buffer;
            float *input_samples = (float *)pAudioVocalIsolateModule->input_samples;
            float *output_samples = (float *)pAudioVocalIsolateModule->output_samples;

            if (nChannels > MAX_AUDIO_VOCAL_ISOLATE_INSTANCE) {
                for (int j = 0; j < AUDIO_VOCAL_ISOLATE_FRAMECOUNT; j++) {
                    input_samples[MAX_AUDIO_VOCAL_ISOLATE_INSTANCE * j] = s32PackedInBuf[nChannels * j];
                    input_samples[MAX_AUDIO_VOCAL_ISOLATE_INSTANCE * j + 1] = s32PackedInBuf[nChannels * j + 1];
                }
            } else {
                if (format == AUDIO_FORMAT_PCM_32_BIT) {
                    memcpy_to_float_from_i32(input_samples, (const int32_t *)u8PackedOutBuf, process_byte / sizeof(int32_t));
                } else if (format == AUDIO_FORMAT_PCM_16_BIT) {
                    memcpy_to_float_from_i16(input_samples, (const int16_t *)u8PackedOutBuf, process_byte / sizeof(int16_t));
                }
            }

            // unify the input to full scale
            if (pAudioVocalIsolateModule->music_gain < 0 && pAudioVocalIsolateModule->music_gain > -6000) {
                apply_volume_float(pAudioVocalIsolateModule->compensation_gain, input_samples, AUDIO_VOCAL_ISOLATE_FRAMECOUNT * MAX_AUDIO_VOCAL_ISOLATE_INSTANCE);
            }

            if (get_debug_value(AML_DUMP_AUDIOHAL_AIAUDIO)) {
                aml_dump_audio_bitstreams(AUDIO_VOCAL_ISOLATE_DUMP_BEFORE, input_samples, AUDIO_VOCAL_ISOLATE_FRAMECOUNT * MAX_AUDIO_VOCAL_ISOLATE_INSTANCE * sizeof(float));
            }

            t_audio_karaoke_input.samples = (float *)pAudioVocalIsolateModule->input_samples;
            t_audio_karaoke_output.samples = (float *)pAudioVocalIsolateModule->output_samples;

            IVA_STATUS_E ret = pAudioVocalIsolateHandle->iva_process(pAudioVocalIsolateHandle->iva_karaoke_handle,
                                &t_audio_karaoke_input, &t_audio_karaoke_output);
            if (ret != IVA_STATUS_OK) {
                ALOGE("%s() audio vocal isolate process error %d", __func__, ret);
            }

            if (t_audio_karaoke_output.num_samples > 0) {
                pAudioVocalIsolateModule->total_output_sample += t_audio_karaoke_output.num_samples;
                ALOGV("%s() output(%p): num_samples: %d, total_samples = %d, total input: %d, (total sample - input): %d, total output: %d, (input - output): %d", __func__,
                    t_audio_karaoke_output.samples, t_audio_karaoke_output.num_samples, pAudioVocalIsolateModule->total_sample,
                    pAudioVocalIsolateModule->total_input_sample, pAudioVocalIsolateModule->total_sample - pAudioVocalIsolateModule->total_input_sample,
                    pAudioVocalIsolateModule->total_output_sample, pAudioVocalIsolateModule->total_input_sample - pAudioVocalIsolateModule->total_output_sample);

                if (get_debug_value(AML_DUMP_AUDIOHAL_AIAUDIO)) {
                    aml_dump_audio_bitstreams(AUDIO_VOCAL_ISOLATE_DUMP_AFTER, output_samples, t_audio_karaoke_output.num_samples * MAX_AUDIO_VOCAL_ISOLATE_INSTANCE * sizeof(float));
                }

                if (pAudioVocalIsolateModule->music_gain < 0 && pAudioVocalIsolateModule->music_gain > -6000) {
                    apply_volume_float(pAudioVocalIsolateModule->volume_gain, output_samples, t_audio_karaoke_output.num_samples * MAX_AUDIO_VOCAL_ISOLATE_INSTANCE);
                }

                if (nChannels > MAX_AUDIO_VOCAL_ISOLATE_INSTANCE) {
                    for (int j = 0; j < t_audio_karaoke_output.num_samples; j++) {
                        s32PackedOutBuf[nChannels * j] = output_samples[MAX_AUDIO_VOCAL_ISOLATE_INSTANCE * j];
                        s32PackedOutBuf[nChannels * j + 1] = output_samples[MAX_AUDIO_VOCAL_ISOLATE_INSTANCE * j + 1];
                    }
                } else {
                    if (format == AUDIO_FORMAT_PCM_32_BIT) {
                        memcpy_to_i32_from_float((int32_t *)u8PackedOutBuf, output_samples, t_audio_karaoke_output.num_samples * nChannels);
                    } else if (format == AUDIO_FORMAT_PCM_16_BIT) {
                        memcpy_to_i16_from_float((int16_t *)u8PackedOutBuf, output_samples, t_audio_karaoke_output.num_samples * nChannels);
                    }
                }

                int output_byte = t_audio_karaoke_output.num_samples * audio_bytes_per_sample(format) * nChannels;
                ring_buffer_write(output_rbuffer, u8PackedOutBuf, output_byte, UNCOVER_WRITE);
            }
        }
    }

    ALOGI("-%s() exit", __func__);
    return NULL;
}

int aml_open_audio_vocal_isolate_module(struct aml_native_postprocess *native_postprocess, audio_config_base_t *audio_config)
{
    /* only support 48K 16 or 32bit audio processing */
    if (!native_postprocess || !audio_config || audio_config->sample_rate != 48000 ||
        (audio_config->format != AUDIO_FORMAT_PCM_32_BIT && audio_config->format != AUDIO_FORMAT_PCM_16_BIT && audio_config->format != AUDIO_FORMAT_PCM_FLOAT)) {
        ALOGE("%s() invalid param(%p sampleRate:%d format:%x) for audio vocal isolate", __func__,
                native_postprocess, audio_config->sample_rate, audio_config->format);
        return -1;
    }

    int ret = 0;
    aml_audio_vocal_isolate_module_t *pAudioVocalIsolateModule = NULL;
    audio_vocal_isolate_libraries_context_t *pAudioVocalIsolateHandle = NULL;

    pAudioVocalIsolateModule = (aml_audio_vocal_isolate_module_t *)calloc(1, sizeof(aml_audio_vocal_isolate_module_t));
    if (!pAudioVocalIsolateModule) {
        ALOGE("%s malloc error", __func__);
        return -ENOMEM;
    }
    memcpy(&pAudioVocalIsolateModule->audio_config, audio_config, sizeof(audio_config_base_t));

    // iva library dlopen/dlsym
    pAudioVocalIsolateHandle = &pAudioVocalIsolateModule->iva_vocal_isolate_handle;
    ret = iva_libraries_open(&pAudioVocalIsolateModule->iva_vocal_isolate_handle, AUDIO_AI_LIB_PATH);
    if (ret < 0) {
        ALOGI("%s() try to dlopen() %s!", __func__, AUDIO_AI_LIB64_PATH);
        ret = iva_libraries_open(&pAudioVocalIsolateModule->iva_vocal_isolate_handle, AUDIO_AI_LIB64_PATH);
    }
    if (ret < 0) {
        free(pAudioVocalIsolateModule);
        pAudioVocalIsolateModule = NULL;
        return -ENOMEM;
    }

    strcpy(pAudioVocalIsolateModule->iva_karaoke_param.model_path, AUDIO_VOCAL_ISOLATE_MODEL_PATH);
    strcpy(pAudioVocalIsolateModule->iva_karaoke_param.model_type, "karaoke21");
    pAudioVocalIsolateModule->iva_karaoke_param.debug_en = 0;

    pAudioVocalIsolateHandle->iva_karaoke_handle = pAudioVocalIsolateHandle->iva_init(&pAudioVocalIsolateModule->iva_karaoke_param);
    if (pAudioVocalIsolateHandle->iva_karaoke_handle == NULL) {
        ALOGE("%s() iva audio vocal isolate init failed", __func__);
        goto iva_audio_vocal_isolate_open_exit;
    }

    pAudioVocalIsolateModule->input_samples = (void *)calloc(1,
                    sizeof(float) * MAX_AUDIO_VOCAL_ISOLATE_INSTANCE * AUDIO_VOCAL_ISOLATE_FRAMECOUNT * 2);
    pAudioVocalIsolateModule->output_samples = (void *)calloc(1,
                    sizeof(float) * MAX_AUDIO_VOCAL_ISOLATE_INSTANCE * AUDIO_VOCAL_ISOLATE_FRAME * 2);
    if (!pAudioVocalIsolateModule->input_samples || !pAudioVocalIsolateModule->output_samples) {
        ALOGE("calloc for audio vocal isolate input failed");
        goto iva_audio_vocal_isolate_open_exit;
    }

    //about 32*20ms input ringbuffer
    int buffer_size = sizeof(float) * MAX_AUDIO_VOCAL_ISOLATE_CHANNEL * AUDIO_VOCAL_ISOLATE_FRAMECOUNT;
    if (ring_buffer_init(&pAudioVocalIsolateModule->input_rbuffer, buffer_size * 32)) {
        goto iva_audio_vocal_isolate_open_exit;
    }

    //about 4*200ms output ringbuffer
    buffer_size = sizeof(float) * MAX_AUDIO_VOCAL_ISOLATE_CHANNEL * AUDIO_VOCAL_ISOLATE_FRAME;
    if (ring_buffer_init(&pAudioVocalIsolateModule->output_rbuffer, buffer_size * 4)) {
        goto iva_audio_vocal_isolate_open_exit;
    }

    //process buffer the same as output buffer
    pAudioVocalIsolateModule->process_buffer_size = buffer_size;
    pAudioVocalIsolateModule->process_buffer = calloc(1, pAudioVocalIsolateModule->process_buffer_size);
    if (pAudioVocalIsolateModule->process_buffer == NULL) {
        ALOGE("calloc for audio vocal isolate process buffer failed");
        goto iva_audio_vocal_isolate_open_exit;
    }

    pAudioVocalIsolateModule->channel_width = 2;
    pAudioVocalIsolateModule->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    native_postprocess->audio_vocal_isolate_handle = pAudioVocalIsolateModule;
    pAudioVocalIsolateModule->music_gain = 0;

    pthread_condattr_t condattr;
    pthread_mutex_init(&pAudioVocalIsolateModule->mutex, NULL);
    pthread_condattr_init(&condattr);
    pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
    pthread_cond_init(&pAudioVocalIsolateModule->cond, &condattr);
    pthread_condattr_destroy(&condattr);
    int pthread_ret = pthread_create(&pAudioVocalIsolateModule->iva_thread_id, NULL, iva_lib_thread_loop, pAudioVocalIsolateModule);
    if (pthread_ret != 0) {
        ALOGE("pthread_create fail");
        goto iva_audio_vocal_isolate_open_exit;
    }

    ALOGI("%s() success exit, ret = %d", __func__, ret);
    return 0;

iva_audio_vocal_isolate_open_exit:
    if (pAudioVocalIsolateModule) {
        aml_audio_vocal_isolate_deinit(pAudioVocalIsolateModule);
        pAudioVocalIsolateModule = NULL;
    }

    ALOGI("%s() error exit, ret = %d", __func__, ret);
    return ret;
}

int aml_close_audio_vocal_isolate_module(struct aml_native_postprocess *native_postprocess)
{
    ALOGD("%s, enter", __func__);
    if (!native_postprocess || !native_postprocess->audio_vocal_isolate_handle) {
        ALOGE("%s() invalid param for audio vocal isolate", __func__);
        return -1;
    }

    aml_audio_vocal_isolate_module_t *pAudioVocalIsolateModule = (aml_audio_vocal_isolate_module_t *)native_postprocess->audio_vocal_isolate_handle;

    //make pthread exit and clean up
    pAudioVocalIsolateModule->req_thread_exit = true;
    pthread_mutex_lock(&pAudioVocalIsolateModule->mutex);
    pthread_cond_signal(&pAudioVocalIsolateModule->cond);
    pthread_mutex_unlock(&pAudioVocalIsolateModule->mutex);

    //wait iva thread exit
    pthread_join(pAudioVocalIsolateModule->iva_thread_id, NULL);
    pthread_cond_destroy(&pAudioVocalIsolateModule->cond);
    pthread_mutex_destroy(&pAudioVocalIsolateModule->mutex);

    aml_audio_vocal_isolate_deinit(pAudioVocalIsolateModule);
    pAudioVocalIsolateModule = NULL;
    return 0;
}

int aml_set_audio_vocal_isolate_enable(struct aml_native_postprocess *native_postprocess, bool enable)
{
    ALOGD("%s, set %s", __func__, (enable == 1) ? "enable":"disable");
    if (!native_postprocess || !native_postprocess->audio_vocal_isolate_handle) {
        ALOGE("%s() invalid param for audio vocal isolate", __func__);
        return -1;
    }

    aml_audio_vocal_isolate_module_t *pAudioVocalIsolateModule = (aml_audio_vocal_isolate_module_t *)native_postprocess->audio_vocal_isolate_handle;
    audio_vocal_isolate_libraries_context_t *pAudioVocalIsolateHandle = &pAudioVocalIsolateModule->iva_vocal_isolate_handle;
    pAudioVocalIsolateModule->audio_vocal_isolate_enable = enable;

    if (enable) {
        ring_buffer_reset(&pAudioVocalIsolateModule->input_rbuffer);
        ring_buffer_reset(&pAudioVocalIsolateModule->output_rbuffer);
        native_postprocess->external_audio_latency = 150;
    } else {
        pAudioVocalIsolateHandle->iva_reset(pAudioVocalIsolateHandle->iva_karaoke_handle);
        pAudioVocalIsolateModule->total_input_sample = 0;
        pAudioVocalIsolateModule->total_output_sample = 0;
        pAudioVocalIsolateModule->total_sample = 0;
        native_postprocess->external_audio_latency = 0;
    }

    return 0;
}

int aml_get_audio_vocal_isolate_enable(struct aml_native_postprocess *native_postprocess)
{
    if (!native_postprocess || !native_postprocess->audio_vocal_isolate_handle) {
        ALOGE("%s() invalid param for audio vocal isolate", __func__);
        return -1;
    }

    aml_audio_vocal_isolate_module_t *pAudioVocalIsolateModule = (aml_audio_vocal_isolate_module_t *)native_postprocess->audio_vocal_isolate_handle;
    return pAudioVocalIsolateModule->audio_vocal_isolate_enable;
}

int aml_update_audio_vocal_isolate_volume(struct aml_native_postprocess *native_postprocess)
{
    if (!native_postprocess || !native_postprocess->audio_vocal_isolate_handle) {
        ALOGE("%s() invalid param for audio vocal isolate", __func__);
        return -1;
    }

    aml_audio_vocal_isolate_module_t *pAudioVocalIsolateModule = (aml_audio_vocal_isolate_module_t *)native_postprocess->audio_vocal_isolate_handle;
    if (pAudioVocalIsolateModule->music_gain != native_postprocess->music_gain) {
        float volume_gain = (float)native_postprocess->music_gain / 100;
        pAudioVocalIsolateModule->volume_gain = DbToAmpl(volume_gain);
        pAudioVocalIsolateModule->compensation_gain = DbToAmpl(-1*volume_gain);
        ALOGD("%s, volume change from %ddB to %ddB, volume gain = %f, compensation gain = %f", __func__, pAudioVocalIsolateModule->music_gain/100,
            native_postprocess->music_gain/100, pAudioVocalIsolateModule->volume_gain, pAudioVocalIsolateModule->compensation_gain);
        pAudioVocalIsolateModule->music_gain = native_postprocess->music_gain;
    }
    return 0;
}

int aml_audio_vocal_isolate_module_process(void *handle, audio_buffer_t *inBuf, audio_buffer_t *outBuf)
{
    if (!handle || !inBuf || !outBuf) {
        ALOGV("%s() line:%d Warning! handle:%p inBuf:%p outBuf:%p", __func__, __LINE__, handle, inBuf, outBuf);
        return -EINVAL;
    }

    aml_audio_vocal_isolate_module_t *pAudioVocalIsolateModule = (aml_audio_vocal_isolate_module_t *)handle;
    audio_format_t format = pAudioVocalIsolateModule->audio_config.format;
    int sampleRate = pAudioVocalIsolateModule->audio_config.sample_rate;
    int nChannels = pAudioVocalIsolateModule->channel_width;
    audio_channel_mask_t channel_mask = pAudioVocalIsolateModule->channel_mask;
    uint32_t inframeCount = inBuf->frameCount;
    uint32_t src_frame_size = audio_bytes_per_sample(format) * nChannels;
    uint32_t src_data_bytes = src_frame_size * inframeCount;

    if (get_debug_value(AML_DUMP_AUDIOHAL_AIAUDIO)) {
        aml_dump_audio_bitstreams(AUDIO_VOCAL_ISOLATE_DUMP_INPUT, inBuf->raw, src_data_bytes);
    }

    // bypass audio processing
    if (!pAudioVocalIsolateModule->audio_vocal_isolate_enable || channel_mask != AUDIO_CHANNEL_OUT_STEREO) {
        if (inBuf->raw != outBuf->raw) {
            memcpy(outBuf->raw, inBuf->raw, src_data_bytes);
        }
        return 0;
    }

    struct ring_buffer *input_rbuffer = &pAudioVocalIsolateModule->input_rbuffer;
    struct ring_buffer *output_rbuffer = &pAudioVocalIsolateModule->output_rbuffer;

    unsigned char *u8PackedInBuf = inBuf->u8;

    if (channel_mask != pAudioVocalIsolateModule->audio_config.channel_mask) {
        ALOGI("channel mask is changed from 0x%x to 0x%x", pAudioVocalIsolateModule->audio_config.channel_mask, pAudioVocalIsolateModule->channel_mask);
        pAudioVocalIsolateModule->audio_config.channel_mask = pAudioVocalIsolateModule->channel_mask;
    }

    ALOGV("%s(): src_frame_size:%d, inBuf.frameCount:%zu, ringbuffer write space:%d, src_data_bytes:%d", __func__,
        src_frame_size, inBuf->frameCount, get_buffer_write_space(input_rbuffer), src_data_bytes);

    pAudioVocalIsolateModule->total_sample += inframeCount;

    if (get_buffer_write_space(input_rbuffer) >= src_data_bytes) {
        ring_buffer_write(input_rbuffer, u8PackedInBuf, src_data_bytes, UNCOVER_WRITE);
        pthread_cond_signal(&pAudioVocalIsolateModule->cond);
    } else {
        ALOGV("%s() not space to write to ringbuffer", __func__);
    }

    if (get_buffer_read_space(output_rbuffer) >= src_data_bytes) {
        ring_buffer_read(output_rbuffer, outBuf->raw, src_data_bytes);
    } else {
        ALOGV("%s() not data to read from ringbuffer", __func__);
    }

    if (get_debug_value(AML_DUMP_AUDIOHAL_AIAUDIO)) {
        aml_dump_audio_bitstreams(AUDIO_VOCAL_ISOLATE_DUMP_OUTPUT, outBuf->raw, src_data_bytes);
    }

    return 0;
}

int aml_set_audio_vocal_gain(struct aml_native_postprocess *native_postprocess, float gain)
{
    if (!native_postprocess || !native_postprocess->audio_vocal_isolate_handle) {
        ALOGE("%s() invalid param for audio vocal isolate", __func__);
        return -1;
    }

    if (gain > 1.0)
        gain = 1.0;
    else if (gain < 0)
        gain = 0;

    aml_audio_vocal_isolate_module_t *pAudioVocalIsolateModule = (aml_audio_vocal_isolate_module_t *)native_postprocess->audio_vocal_isolate_handle;
    pAudioVocalIsolateModule->audio_vocal_gain = gain;
    ALOGD("%s, %f", __func__, gain);

    return 0;
}

float aml_get_audio_vocal_gain(struct aml_native_postprocess *native_postprocess)
{
    if (!native_postprocess || !native_postprocess->audio_vocal_isolate_handle) {
        ALOGE("%s() invalid param for audio vocal isolate", __func__);
        return -1;
    }

    aml_audio_vocal_isolate_module_t *pAudioVocalIsolateModule = (aml_audio_vocal_isolate_module_t *)native_postprocess->audio_vocal_isolate_handle;
    return pAudioVocalIsolateModule->audio_vocal_gain;
}

