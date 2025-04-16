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

#define LOG_TAG "aml_audio_enhancement"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <cutils/log.h>

#include "audio_post_process.h"
#include "aml_ringbuffer.h"
#include "aml_audio_enhancement.h"
#include "audio_hw_utils.h"

#define AUDIO_ENHANCE_DUMP_BEFORE   "/data/vendor/audiohal/before_audio_enhance.pcm"
#define AUDIO_ENHANCE_DUMP_OUTPUT   "/data/vendor/audiohal/after_audio_enhance.pcm"

#define STEREO_PROCESS_CHANNEL_LAYOUT               (0x3)   // L/R channel
#define MULTICH_PROCESS_CHANNEL_LAYOUT              (0x4)   // center channel
#define MAX_AUDIO_ENHANCEMENT_CHANNEL               (8)     // max support channel
#define AUDIO_ENHANCEMENT_FRAMECOUNT                (512)   // Unit: frames, 10.667ms

//############ IVA Audio Enhancement library APIs #############//
static int iva_libraries_open(audio_enhancement_libraries_context_t *ivaLibContext, const char *lib_path)
{
    void* dl_handle = dlopen(lib_path, RTLD_NOW);
    if (!dl_handle) {
        ALOGE("%s() dlopen() %s error:%s", __func__, lib_path, dlerror());
        return -EINVAL;
    }

    ivaLibContext->iva_init = dlsym(dl_handle, "aai_iva_audio_enhancement_init");
    if (!ivaLibContext->iva_init) {
        ALOGE("%s() dlsym() aai_iva_audio_enhancement_init error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_deinit = dlsym(dl_handle, "aai_iva_audio_enhancement_deinit");
    if (!ivaLibContext->iva_deinit) {
        ALOGE("%s() dlsym() aai_iva_audio_enhancement_deinit error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_process = dlsym(dl_handle, "aai_iva_audio_enhancement_process");
    if (!ivaLibContext->iva_process) {
        ALOGE("%s() dlsym() aai_iva_audio_enhancement_process error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_process_int32 = dlsym(dl_handle, "aai_iva_audio_enhancement_process_int32");
    if (!ivaLibContext->iva_process_int32) {
        ALOGE("%s() dlsym() aai_iva_audio_enhancement_process_int32 error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_enable = dlsym(dl_handle, "aai_iva_audio_enhancement_enable");
    if (!ivaLibContext->iva_enable) {
        ALOGE("%s() dlsym() aai_iva_audio_enhancement_enable error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_clear = dlsym(dl_handle, "aai_iva_audio_enhancement_clear");
    if (!ivaLibContext->iva_clear) {
        ALOGE("%s() dlsym() aai_iva_audio_enhancement_clear error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_setparam = dlsym(dl_handle, "aai_iva_audio_enhancement_setparam");
    if (!ivaLibContext->iva_setparam) {
        ALOGE("%s() dlsym() aai_iva_audio_enhancement_setparam error:%d", __func__, errno);
        goto exit;
    }

    ivaLibContext->iva_getparam = dlsym(dl_handle, "aai_iva_audio_enhancement_getparam");
    if (!ivaLibContext->iva_getparam) {
        ALOGE("%s() dlsym() aai_iva_audio_enhancement_getparam error:%d", __func__, errno);
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

static int iva_libraries_close(audio_enhancement_libraries_context_t *ivaLibContext)
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
    if (ivaLibContext->iva_process_int32) {
        ivaLibContext->iva_process_int32 = NULL;
    }
    if (ivaLibContext->iva_enable) {
        ivaLibContext->iva_enable = NULL;
    }
    if (ivaLibContext->iva_clear) {
        ivaLibContext->iva_clear = NULL;
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

static int aml_audio_enhancement_deinit(aml_audio_enhancement_module_t *pAudioEnhancementModule)
{
    ALOGV("%s, enter", __func__);
    if (!pAudioEnhancementModule) {
        ALOGE("%s() invalid param for audio enhancement deinit", __func__);
        return -1;
    }
    audio_enhancement_libraries_context_t *pstIvaEnhancementHandle = &pAudioEnhancementModule->iva_enhancement_handle;
    int i = 0;

    if (pstIvaEnhancementHandle) {
        for (i = 0; i < MAX_AUDIO_ENHANCEMENT_INSTANCE; i++) {
            if (pstIvaEnhancementHandle->nnans_imple[i] && pstIvaEnhancementHandle->iva_deinit) {
                pstIvaEnhancementHandle->iva_deinit(pstIvaEnhancementHandle->nnans_imple[i]);
            }
        }
        iva_libraries_close(pstIvaEnhancementHandle);
    }

    if (pAudioEnhancementModule->input_samples) {
        free(pAudioEnhancementModule->input_samples);
        pAudioEnhancementModule->input_samples = NULL;
    }
    if (pAudioEnhancementModule->output_samples) {
        free(pAudioEnhancementModule->output_samples);
        pAudioEnhancementModule->output_samples = NULL;
    }
    if (pAudioEnhancementModule->process_buffer) {
        free(pAudioEnhancementModule->process_buffer);
        pAudioEnhancementModule->process_buffer = NULL;
    }

    ring_buffer_release(&pAudioEnhancementModule->input_rbuffer);
    ring_buffer_release(&pAudioEnhancementModule->output_rbuffer);

    free(pAudioEnhancementModule);
    return 0;
}

int aml_open_audio_enhancement_module(struct aml_native_postprocess *native_postprocess, audio_config_base_t *audio_config, int channel_width)
{
    ALOGD("%s, channel_width %d", __func__, channel_width);

    /* only support 48K 16 or 32bit audio processing */
    if (!native_postprocess || !audio_config || audio_config->sample_rate != 48000 ||
        (audio_config->format != AUDIO_FORMAT_PCM_32_BIT && audio_config->format != AUDIO_FORMAT_PCM_16_BIT)) {
        ALOGE("%s() invalid param(%p sampleRate:%d format:%x) for audio enhancement", __func__, native_postprocess, audio_config->sample_rate, audio_config->format);
        return -1;
    }

    aml_audio_enhancement_module_t *pAudioEnhancementModule = NULL;
    audio_enhancement_libraries_context_t *pstIvaEnhancementHandle = NULL;
    int i = 0, ret = 0;



    pAudioEnhancementModule = (aml_audio_enhancement_module_t *)calloc(1, sizeof(aml_audio_enhancement_module_t));
    if (!pAudioEnhancementModule) {
        ALOGE("%s malloc error", __func__);
        return -ENOMEM;
    }

    memcpy(&pAudioEnhancementModule->audio_config, audio_config, sizeof(audio_config_base_t));

    pAudioEnhancementModule->stEnhancementParam.fs = audio_config->sample_rate;
    pAudioEnhancementModule->stEnhancementParam.chunk_size = AUDIO_ENHANCEMENT_FRAMECOUNT;
    if (audio_config->format == AUDIO_FORMAT_PCM_32_BIT) {
        pAudioEnhancementModule->stEnhancementParam.bits_per_sample = 32;
    } else {
        pAudioEnhancementModule->stEnhancementParam.bits_per_sample = 16;
    }
    strcpy(pAudioEnhancementModule->stEnhancementParam.model_path, AUDIO_ENHANCMENT_MODEL_PATH);

    // iva library dlopen/dlsym
    pstIvaEnhancementHandle = &pAudioEnhancementModule->iva_enhancement_handle;
    ret = iva_libraries_open(&pAudioEnhancementModule->iva_enhancement_handle, AUDIO_AI_LIB_PATH);
    if (ret < 0) {
        ALOGI("%s() try to dlopen() %s!", __func__, AUDIO_AI_LIB64_PATH);
        ret = iva_libraries_open(&pAudioEnhancementModule->iva_enhancement_handle, AUDIO_AI_LIB64_PATH);
    }
    if (ret < 0) {
        free(pAudioEnhancementModule);
        pAudioEnhancementModule = NULL;
        return -1;
    }

    for (i = 0; i < MAX_AUDIO_ENHANCEMENT_INSTANCE; i++) {
        pstIvaEnhancementHandle->nnans_imple[i] = pstIvaEnhancementHandle->iva_init(&pAudioEnhancementModule->stEnhancementParam);
        if (pstIvaEnhancementHandle->nnans_imple[i] == NULL) {
            ALOGE("%s() iva audio enhancement init failed", __func__);
            goto iva_audio_enhancement_open_exit;
        }
    }

    // 48K 32bit pcm, 10.667ms
    pAudioEnhancementModule->input_samples = (void *)calloc(1, sizeof(int32_t) * AUDIO_ENHANCEMENT_FRAMECOUNT);
    pAudioEnhancementModule->output_samples = (void *)calloc(1, sizeof(int32_t) * AUDIO_ENHANCEMENT_FRAMECOUNT);
    if (!pAudioEnhancementModule->input_samples || !pAudioEnhancementModule->output_samples) {
        ALOGE("calloc for audio enhancement input failed");
        goto iva_audio_enhancement_open_exit;
    }

    int buffer_size = AUDIO_ENHANCEMENT_FRAMECOUNT * sizeof(int32_t) * MAX_AUDIO_ENHANCEMENT_CHANNEL;
    if (ring_buffer_init(&pAudioEnhancementModule->input_rbuffer, buffer_size * 2)) {
        goto iva_audio_enhancement_open_exit;
    }

    if (ring_buffer_init(&pAudioEnhancementModule->output_rbuffer, buffer_size * 2)) {
        goto iva_audio_enhancement_open_exit;
    }

    // FIXME: alloc buffer according to audio_config. But now fixed size due to the iva performance.
    pAudioEnhancementModule->process_buffer_size = buffer_size;
    pAudioEnhancementModule->process_buffer = calloc(1, pAudioEnhancementModule->process_buffer_size);
    if (pAudioEnhancementModule->process_buffer == NULL) {
        ALOGE("calloc for audio enhancement process buffer failed");
        goto iva_audio_enhancement_open_exit;
    }

    pAudioEnhancementModule->channel_width = channel_width;
    pAudioEnhancementModule->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    pAudioEnhancementModule->processed_channel_layout = STEREO_PROCESS_CHANNEL_LAYOUT;
    native_postprocess->audio_enhancment_handle = pAudioEnhancementModule;
    ALOGI("%s() exit, ret = %d", __func__, ret);

    return 0;

iva_audio_enhancement_open_exit:
    if (pAudioEnhancementModule) {
        aml_audio_enhancement_deinit(pAudioEnhancementModule);
    }

    ALOGI("%s() exit, ret = %d", __func__, ret);
    return ret;
}

int aml_close_audio_enhancement_module(struct aml_native_postprocess *native_postprocess)
{
    ALOGD("%s, enter", __func__);
    if (!native_postprocess || !native_postprocess->audio_enhancment_handle) {
        ALOGE("%s() invalid param for audio enhancement", __func__);
        return -1;
    }

    aml_audio_enhancement_module_t *pAudioEnhancementModule = (aml_audio_enhancement_module_t *)native_postprocess->audio_enhancment_handle;
    aml_audio_enhancement_deinit(pAudioEnhancementModule);

    return 0;
}

int aml_set_audio_enhancement_enable(struct aml_native_postprocess *native_postprocess, bool enable)
{
    ALOGD("%s, set %s", __func__, (enable == 1) ? "enable":"disable");
    if (!native_postprocess || !native_postprocess->audio_enhancment_handle) {
        ALOGE("%s() invalid param for audio enhancement", __func__);
        return -1;
    }

    aml_audio_enhancement_module_t *pAudioEnhancementModule = (aml_audio_enhancement_module_t *)native_postprocess->audio_enhancment_handle;
    audio_enhancement_libraries_context_t *pstIvaEnhancementHandle = &pAudioEnhancementModule->iva_enhancement_handle;
    pAudioEnhancementModule->audio_enhancement_enable = enable;
    if (enable) {
        ring_buffer_reset(&pAudioEnhancementModule->input_rbuffer);
        ring_buffer_reset(&pAudioEnhancementModule->output_rbuffer);
    }

    if (pstIvaEnhancementHandle) {
        for (int i = 0; i < MAX_AUDIO_ENHANCEMENT_INSTANCE; i++) {
            if (pstIvaEnhancementHandle->nnans_imple[i] && pstIvaEnhancementHandle->iva_enable) {
                pstIvaEnhancementHandle->iva_enable(pstIvaEnhancementHandle->nnans_imple[i], pAudioEnhancementModule->audio_enhancement_enable);
            }
        }
    }

    return 0;
}

int aml_update_audio_channel_mask(struct aml_native_postprocess *native_postprocess, audio_channel_mask_t channel_mask)
{
    ALOGV("%s, channel_mask: %x ", __func__, channel_mask);
    if (!native_postprocess || !native_postprocess->audio_enhancment_handle) {
        ALOGE("%s() invalid param for audio enhancement", __func__);
        return -1;
    }

    aml_audio_enhancement_module_t *pAudioEnhancementModule = (aml_audio_enhancement_module_t *)native_postprocess->audio_enhancment_handle;
    pAudioEnhancementModule->channel_mask = channel_mask;

    return 0;
}

int aml_set_audio_enhancement_gain(struct aml_native_postprocess *native_postprocess, int gain_db)
{
    ALOGD("%s, %ddB", __func__, gain_db);
    if (!native_postprocess || !native_postprocess->audio_enhancment_handle) {
        ALOGE("%s() invalid param for audio enhancement", __func__);
        return -1;
    }

    aml_audio_enhancement_module_t *pAudioEnhancementModule = (aml_audio_enhancement_module_t *)native_postprocess->audio_enhancment_handle;
    audio_enhancement_libraries_context_t *pstIvaEnhancementHandle = &pAudioEnhancementModule->iva_enhancement_handle;
    pAudioEnhancementModule->audio_enhancement_gain = gain_db;
    pAudioEnhancementModule->stEnhancementParam.value = gain_db;

    if (pstIvaEnhancementHandle) {
        for (int i = 0; i < MAX_AUDIO_ENHANCEMENT_INSTANCE; i++) {
            if (pstIvaEnhancementHandle->nnans_imple[i] && pstIvaEnhancementHandle->iva_setparam) {
                pstIvaEnhancementHandle->iva_setparam(pstIvaEnhancementHandle->nnans_imple[i], &pAudioEnhancementModule->stEnhancementParam);
            }
        }
    }

    return 0;
}

int aml_get_audio_enhancement_gain(struct aml_native_postprocess *native_postprocess)
{
    ALOGV("%s, enter", __func__);
    if (!native_postprocess || !native_postprocess->audio_enhancment_handle) {
        ALOGE("%s() invalid param for audio enhancement", __func__);
        return -1;
    }

    aml_audio_enhancement_module_t *pAudioEnhancementModule = (aml_audio_enhancement_module_t *)native_postprocess->audio_enhancment_handle;
    return pAudioEnhancementModule->audio_enhancement_gain;
}

int aml_get_audio_enhancement_enable(struct aml_native_postprocess *native_postprocess)
{
    ALOGV("%s, enter", __func__);
    if (!native_postprocess || !native_postprocess->audio_enhancment_handle) {
        ALOGE("%s() invalid param for audio enhancement", __func__);
        return -1;
    }

    aml_audio_enhancement_module_t *pAudioEnhancementModule = (aml_audio_enhancement_module_t *)native_postprocess->audio_enhancment_handle;
    return pAudioEnhancementModule->audio_enhancement_enable;
}

int aml_audio_enhancement_module_process(void *handle, audio_buffer_t *inBuf, audio_buffer_t *outBuf)
{
    if (!handle || !inBuf || !outBuf) {
        ALOGV("%s() line:%d Warning! handle:%p inBuf:%p outBuf:%p", __func__, __LINE__, handle, inBuf, outBuf);
        return -EINVAL;
    }

    aml_audio_enhancement_module_t *pAudioEnhancementModule = (aml_audio_enhancement_module_t *)handle;
    audio_enhancement_libraries_context_t *pstIvaEnhancementHandle = &pAudioEnhancementModule->iva_enhancement_handle;
    aai_iva_audio_enhancement_input_t t_audio_enhancement_input;
    aai_iva_audio_enhancement_output_t t_audio_enhancement_output;

    // need update according to native_postprocess
    audio_format_t format = pAudioEnhancementModule->audio_config.format;
    int sampleRate = pAudioEnhancementModule->audio_config.sample_rate;
    int nChannels = pAudioEnhancementModule->channel_width;
    uint32_t inframeCount = inBuf->frameCount;
    uint32_t src_frame_size = audio_bytes_per_sample(format) * nChannels;
    uint32_t src_data_bytes = src_frame_size * inframeCount;

    if (get_debug_value(AML_DUMP_AUDIOHAL_AIDE)) {
        aml_dump_audio_bitstreams(AUDIO_ENHANCE_DUMP_BEFORE, inBuf->raw, src_data_bytes);
    }

    // bypass audio enhancement processing
    if (!pAudioEnhancementModule->audio_enhancement_enable) {
        if (inBuf->raw != outBuf->raw) {
            memcpy(outBuf->raw, inBuf->raw, src_data_bytes);
        }
        return 0;
    }

    struct ring_buffer *input_rbuffer = &pAudioEnhancementModule->input_rbuffer;
    struct ring_buffer *output_rbuffer = &pAudioEnhancementModule->output_rbuffer;
    unsigned char *u8PackedInBuf = inBuf->u8;
    unsigned char *u8PackedOutBuf = (unsigned char *)pAudioEnhancementModule->process_buffer;
    int process_byte = AUDIO_ENHANCEMENT_FRAMECOUNT * src_frame_size;
    audio_channel_mask_t channel_mask = pAudioEnhancementModule->channel_mask;
    int processed_channel_layout = pAudioEnhancementModule->processed_channel_layout;

    if (channel_mask != pAudioEnhancementModule->audio_config.channel_mask) {
        if (channel_mask == AUDIO_CHANNEL_OUT_STEREO) {
            pAudioEnhancementModule->processed_channel_layout = STEREO_PROCESS_CHANNEL_LAYOUT;
        } else {
            pAudioEnhancementModule->processed_channel_layout = MULTICH_PROCESS_CHANNEL_LAYOUT;
        }
        processed_channel_layout = pAudioEnhancementModule->processed_channel_layout;
        ALOGI("channel mask is changed from 0x%x to 0x%x, processed channel layout %x",
            pAudioEnhancementModule->audio_config.channel_mask, pAudioEnhancementModule->channel_mask, processed_channel_layout);

        pAudioEnhancementModule->audio_config.channel_mask = pAudioEnhancementModule->channel_mask;
    }

    if (get_buffer_write_space(input_rbuffer) < src_data_bytes + process_byte) {
        if (ring_buffer_realloc(input_rbuffer, (src_data_bytes + process_byte) * 2) < 0) {
            ALOGE("realloc ringbuffer(%d) for audio enhancement failed", (src_data_bytes + process_byte) * 2);
            return -1;
        }
        ALOGI("realloc input_ringbuffer for audio enhancement failed, new size:%d", input_rbuffer->size);
    }

    if (get_buffer_write_space(output_rbuffer) < src_data_bytes) {
        if (ring_buffer_realloc(output_rbuffer, (src_data_bytes + process_byte) * 2) < 0) {
            ALOGE("realloc ringbuffer(%d) for audio enhancement failed", (src_data_bytes + process_byte) * 2);
            return -1;
        }
        ALOGI("realloc output_ringbuffer for audio enhancement failed, new size:%d", output_rbuffer->size);
    }

    ALOGV("%s(): src_frame_size:%d, inBuf.frameCount:%zu, ringbuffer read space:%d, process_buffer:%p src_data_bytes:%d",
            __func__, src_frame_size, inBuf->frameCount, get_buffer_read_space(input_rbuffer),
            pAudioEnhancementModule->process_buffer, src_data_bytes);

    ring_buffer_write(input_rbuffer, u8PackedInBuf, src_data_bytes, UNCOVER_WRITE);

    while (get_buffer_read_space(input_rbuffer) >= process_byte) {
        // read input data to process buffer
        ring_buffer_read(input_rbuffer, u8PackedOutBuf, process_byte);

        if (format == AUDIO_FORMAT_PCM_32_BIT) {
            int32_t *s32PackedInBuf = (int32_t *)pAudioEnhancementModule->process_buffer;
            int32_t *s32PackedOutBuf = (int32_t *)pAudioEnhancementModule->process_buffer;
            int32_t *input_samples = (int32_t *)pAudioEnhancementModule->input_samples;
            int32_t *output_samples = (int32_t *)pAudioEnhancementModule->output_samples;
            int n = 0;

            for (int i = 0; i < nChannels; i++) {
                if ((processed_channel_layout & 0x1) && n < MAX_AUDIO_ENHANCEMENT_INSTANCE) {
                    for (int j = 0; j < AUDIO_ENHANCEMENT_FRAMECOUNT; j++) {
                        input_samples[j] = s32PackedInBuf[i + j * nChannels];
                    }

                    t_audio_enhancement_input.int_samples = (int *)pAudioEnhancementModule->input_samples;
                    t_audio_enhancement_output.int_samples = (int *)pAudioEnhancementModule->output_samples;

                    pstIvaEnhancementHandle->iva_process_int32(pstIvaEnhancementHandle->nnans_imple[n],
                        &t_audio_enhancement_input, &t_audio_enhancement_output);

                    for (int j = 0; j < AUDIO_ENHANCEMENT_FRAMECOUNT; j++) {
                        s32PackedOutBuf[i + j * nChannels] = output_samples[j];
                    }
                    n++;
                }
                processed_channel_layout >>= 1;
            }
        } else {
            int16_t *s16PackedInBuf = (int16_t *)pAudioEnhancementModule->process_buffer;
            int16_t *s16PackedOutBuf = (int16_t *)pAudioEnhancementModule->process_buffer;
            int16_t *input_samples = (int16_t *)pAudioEnhancementModule->input_samples;
            int16_t *output_samples = (int16_t *)pAudioEnhancementModule->output_samples;
            int n = 0;

            for (int i = 0; i < MAX_AUDIO_ENHANCEMENT_INSTANCE; i++) {
                if ((processed_channel_layout & 0x1) && n < MAX_AUDIO_ENHANCEMENT_INSTANCE) {
                    for (int j = 0; j < AUDIO_ENHANCEMENT_FRAMECOUNT; j++) {
                        input_samples[j] = s16PackedInBuf[i + j * nChannels];
                    }

                    t_audio_enhancement_input.samples = (short *)pAudioEnhancementModule->input_samples;
                    t_audio_enhancement_output.samples = (short *)pAudioEnhancementModule->output_samples;

                    pstIvaEnhancementHandle->iva_process(pstIvaEnhancementHandle->nnans_imple[n],
                        &t_audio_enhancement_input, &t_audio_enhancement_output);

                    for (int j = 0; j < AUDIO_ENHANCEMENT_FRAMECOUNT; j++) {
                        s16PackedOutBuf[i + j * nChannels] = output_samples[j];
                    }
                    n++;
                }
                processed_channel_layout >>= 1;
            }
        }

        ALOGV("%s(): src_frame_size:%d, inBuf.frameCount:%zu, output_rbuffer write space:%d",
            __func__, src_frame_size, inBuf->frameCount, get_buffer_write_space(output_rbuffer));

        ring_buffer_write(output_rbuffer, u8PackedOutBuf, process_byte, UNCOVER_WRITE);
    }

    if (get_buffer_read_space(output_rbuffer) >= src_data_bytes)
        ring_buffer_read(output_rbuffer, outBuf->raw, src_data_bytes);

    if (get_debug_value(AML_DUMP_AUDIOHAL_AIDE)) {
        aml_dump_audio_bitstreams(AUDIO_ENHANCE_DUMP_OUTPUT, outBuf->raw, src_data_bytes);
    }

    return 0;
}

