/*
 * Copyright (C) 2024 Amlogic Corporation.
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

#define LOG_TAG "audio_hw_hal_continuous_standby"

#include "audio_ms12_continuous_standby.h"
#include <sys/time.h>
#include <stdlib.h>
#include <log/log.h>
#include <string.h>
#include "aml_malloc_debug.h"

#define RESET_CHECK_DELAY_CNT   200

#define STANDBY_LIMIT_LEVEL     768

typedef int (*output_callback)(void *buffer, void *priv, size_t size, void *ms12_info);

struct audio_continuous_standby {
    int standby_status;
    void *standby_repeat_buf[STANDBY_MAX_REPEAT_FORMAT];
    int standby_repeat_buf_size[STANDBY_MAX_REPEAT_FORMAT];
    aml_ms12_dec_info_t standby_repeat_info[STANDBY_MAX_REPEAT_FORMAT];
    int frame_is_match[STANDBY_MAX_REPEAT_FORMAT];
    int output_port_enable[STANDBY_MAX_REPEAT_FORMAT];
    int output_mask;
    int atmos_lock;
    output_callback callback;
    void *priv_data;
    int reset_check_cnt;
    int repeat_output_flag;
    pthread_mutex_t lock;
};

int audio_continuous_standby_open(void **pphandle, void *callback, void *priv_data) {
    int ret = -1;
    audio_continuous_standby_t *standby_handle = NULL;
    standby_handle = aml_audio_malloc(sizeof(audio_continuous_standby_t));
    if (standby_handle == NULL) {
        ALOGE("malloc failed\n");
        return -1;
    }

    memset(standby_handle, 0, sizeof(audio_continuous_standby_t));
    standby_handle->callback = callback;
    standby_handle->priv_data = priv_data;
    standby_handle->reset_check_cnt = 0;
    standby_handle->standby_status = 0;
    standby_handle->repeat_output_flag = 0;

    // PCM format malloc default size zero buffer.
    standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_PCM] = aml_audio_calloc(1, 2 * 4 * 1536);//2ch 32bit 1536frame
    standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_MCH] = aml_audio_calloc(1, 8 * 4 * 1536);//8ch 32bit 1536frame
    standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_DAP] = aml_audio_calloc(1, 8 * 4 * 1536);//8ch 32bit 1536frame
    for (int i = 0; i < STANDBY_MAX_REPEAT_FORMAT; i++) {
        standby_handle->frame_is_match[i] = false;
        standby_handle->output_port_enable[i] = false;
        standby_handle->standby_repeat_buf_size[i] = 0;
    }

    ALOGD("%s, output_callback %p, priv_data %p", __FUNCTION__, standby_handle->callback, standby_handle->priv_data);
    *pphandle = (void*)standby_handle;
    return 0;
}

int audio_continuous_standby_close(void **pphandle) {
    audio_continuous_standby_t *standby_handle = NULL;

    if (*pphandle == NULL) {
        return 0;
    }
    standby_handle = (audio_continuous_standby_t *)*pphandle;

    for (int i = 0; i < STANDBY_MAX_REPEAT_FORMAT; i++) {
        if (standby_handle->standby_repeat_buf[i]) {
            aml_audio_free(standby_handle->standby_repeat_buf[i]);
        }
    }
    aml_audio_free(*pphandle);
    *pphandle = NULL;
    ALOGI("%s exit", __func__);
    return 0;
}

int audio_continuous_standby_run(void *phandle, int delay) {
    audio_continuous_standby_t *standby_handle = NULL;
    int ret = 1;
    int pcm_repeat_frame = 1536;
    int pcm_size = 0;
    if (phandle == NULL) {
        ALOGE("%s error, handle %p", __FUNCTION__, phandle);
        return -1;
    }

    standby_handle = (audio_continuous_standby_t *)phandle;
    //ALOGE("%s , delay %d", __FUNCTION__, delay);
    if (delay > STANDBY_LIMIT_LEVEL) {
        usleep(3000);
        return 0;
    }

    standby_handle->repeat_output_flag = 1;
    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_MAT_UPPER]) {
        pcm_repeat_frame = 960;
    } else if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DD] || standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DDP]) {
        pcm_repeat_frame = 1536;
    }


    pcm_size = standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_PCM].output_ch * standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_PCM].output_bitwidth / 8 * pcm_repeat_frame;
    memset(standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_PCM], 0, pcm_size);
    standby_handle->callback(standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_PCM], standby_handle->priv_data, pcm_size, &standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_PCM]);

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_MCH] && standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_MCH]) {
        pcm_size = standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_MCH].output_ch * standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_MCH].output_bitwidth / 8 * pcm_repeat_frame;
        standby_handle->callback(standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_MCH], standby_handle->priv_data, pcm_size, &standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_MCH]);
    }

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DAP] && standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_DAP]) {
        pcm_size = standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_DAP].output_ch * standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_DAP].output_bitwidth / 8 * pcm_repeat_frame;
        standby_handle->callback(standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_DAP], standby_handle->priv_data, pcm_size, &standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_DAP]);
    }

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DD] && standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_DD]) {
        standby_handle->callback(standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_DD], standby_handle->priv_data, standby_handle->standby_repeat_buf_size[STANDBY_REPEAT_FORMAT_DD], &standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_DD]);
    }

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DDP] && standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_DDP]) {
        standby_handle->callback(standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_DDP], standby_handle->priv_data, standby_handle->standby_repeat_buf_size[STANDBY_REPEAT_FORMAT_DDP], &standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_DDP]);
    }

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_MAT_UPPER] && standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_MAT_UPPER] && standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_MAT_LOWER]) {
        standby_handle->callback(standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_MAT_UPPER], standby_handle->priv_data, standby_handle->standby_repeat_buf_size[STANDBY_REPEAT_FORMAT_MAT_UPPER], &standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_MAT_UPPER]);
        standby_handle->callback(standby_handle->standby_repeat_buf[STANDBY_REPEAT_FORMAT_MAT_LOWER], standby_handle->priv_data, standby_handle->standby_repeat_buf_size[STANDBY_REPEAT_FORMAT_MAT_LOWER], &standby_handle->standby_repeat_info[STANDBY_REPEAT_FORMAT_MAT_LOWER]);
    }
    usleep(5000);
    standby_handle->repeat_output_flag = 0;
    return 0;
}

int audio_continuous_standby_attachframe(void *phandle, void *buf, int size, int format, aml_ms12_dec_info_t *info) {
    audio_continuous_standby_t *standby_handle = NULL;

    if (phandle == NULL || buf == NULL || format < 0 || format >= STANDBY_MAX_REPEAT_FORMAT) {
        ALOGE("%s error, handle %p, buf %p, format %d", __FUNCTION__, phandle, buf, format);
        return -1;
    }
    standby_handle = (audio_continuous_standby_t *)phandle;
    if (!standby_handle->standby_status || standby_handle->repeat_output_flag) {
        ALOGV("%s, standby_status %d return.", __FUNCTION__, standby_handle->standby_status);
        return 0;
    }

    standby_handle = (audio_continuous_standby_t *)phandle;
    pthread_mutex_lock(&standby_handle->lock);

    if (format == STANDBY_REPEAT_FORMAT_PCM || format == STANDBY_REPEAT_FORMAT_MCH || format == STANDBY_REPEAT_FORMAT_DAP) {
        if (standby_handle->standby_repeat_buf[format] == NULL) {
            ALOGE("%s error, format %d PCM malloc fail", __FUNCTION__, format);
            if (format == STANDBY_REPEAT_FORMAT_PCM) {
                standby_handle->standby_repeat_buf[format] = aml_audio_calloc(1, 2 * 4 * 1536);
            } else {
                standby_handle->standby_repeat_buf[format] = aml_audio_calloc(1, 8 * 4 * 1536);
            }
            goto EXIT;
        }
        standby_handle->frame_is_match[format] = true;
        memcpy(&standby_handle->standby_repeat_info[format], info, sizeof(aml_ms12_dec_info_t));
        goto EXIT;
    }
    if (standby_handle->standby_repeat_buf_size[format] != size) {
        if (standby_handle->standby_repeat_buf[format]) {
            aml_audio_free(standby_handle->standby_repeat_buf[format]);
            standby_handle->standby_repeat_buf[format] = NULL;
        }
        standby_handle->standby_repeat_buf[format] = aml_audio_malloc(size);
        if (standby_handle->standby_repeat_buf[format] == NULL) {
            ALOGE("%s error, malloc fail, format %d", __FUNCTION__, format);
            goto EXIT;
        }
        standby_handle->standby_repeat_buf_size[format] = size;
    }
    memcpy(standby_handle->standby_repeat_buf[format], buf, size);

    if (format == STANDBY_REPEAT_FORMAT_MAT_UPPER) {
        standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_MAT_UPPER] = true;
        standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_MAT_LOWER] = false;
    } else {
        standby_handle->frame_is_match[format] = true;
    }
    memcpy(&standby_handle->standby_repeat_info[format], info, sizeof(aml_ms12_dec_info_t));

EXIT:
    pthread_mutex_unlock(&standby_handle->lock);
    return 0;
}

int audio_continuous_standby_check(void *phandle) {
    audio_continuous_standby_t *standby_handle;
    int ret = 1;
    if (phandle == NULL) {
        ALOGE("%s error, handle %p", __FUNCTION__, phandle);
        return -1;
    }
    standby_handle = (audio_continuous_standby_t *)phandle;

    pthread_mutex_lock(&standby_handle->lock);

    if (!standby_handle->standby_status) {
        ret = 0;
        goto EXIT;
    }

    if (standby_handle->reset_check_cnt) {
        standby_handle->reset_check_cnt--;
        ret = 0;
        goto EXIT;
    }

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_PCM] && !standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_PCM]) {
        ALOGV("%s PCM check fail", __FUNCTION__);
        ret = 0;
        goto EXIT;
    }

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_MCH] && !standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_MCH]) {
        ALOGV("%s MCH check fail", __FUNCTION__);
        ret = 0;
        goto EXIT;
    }

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DAP] && !standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_DAP]) {
        ALOGV("%s DAP check fail", __FUNCTION__);
        ret = 0;
        goto EXIT;
    }

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DD] && !standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_DD]) {
        ALOGV("%s DD check fail", __FUNCTION__);
        ret = 0;
        goto EXIT;
    }

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DDP] && !standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_DDP]) {
        ALOGV("%s DDP check fail", __FUNCTION__);
        ret = 0;
        goto EXIT;
    }

    if (standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_MAT_UPPER]
        && standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_MAT_LOWER]
        && (!standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_MAT_UPPER] || !standby_handle->frame_is_match[STANDBY_REPEAT_FORMAT_MAT_LOWER])) {
        ALOGV("%s MAT check fail", __FUNCTION__);
        ret = 0;
    }
EXIT:
    ALOGV("%s ret %d", __FUNCTION__, ret);
    pthread_mutex_unlock(&standby_handle->lock);
    return ret;
}

int audio_continuous_standby_reset(void *phandle) {
    audio_continuous_standby_t *standby_handle;

    if (phandle == NULL) {
        ALOGE("%s error, handle %p", __FUNCTION__, phandle);
        return -1;
    }
    standby_handle = (audio_continuous_standby_t *)phandle;
    pthread_mutex_lock(&standby_handle->lock);

    for (int i = 0; i < STANDBY_MAX_REPEAT_FORMAT; i++) {
        standby_handle->frame_is_match[i] = false;
    }
    standby_handle->reset_check_cnt = RESET_CHECK_DELAY_CNT;
    pthread_mutex_unlock(&standby_handle->lock);
    return 1;
}
int audio_continuous_standby_set(void *phandle, int type, int params) {
    audio_continuous_standby_t *standby_handle;

    if (phandle == NULL) {
        ALOGE("%s error, handle %p", __FUNCTION__, phandle);
        return -1;
    }
    standby_handle = (audio_continuous_standby_t *)phandle;
    pthread_mutex_lock(&standby_handle->lock);
    ALOGD("%s, type %d, params %d", __FUNCTION__, type, params);
    if (type == STANDBY_SET_STATUS) {
        if (standby_handle->standby_status != params) {
            standby_handle->standby_status = params;
            for (int i = 0; i < STANDBY_MAX_REPEAT_FORMAT; i++) {
                standby_handle->frame_is_match[i] = false;
            }
            standby_handle->reset_check_cnt = RESET_CHECK_DELAY_CNT;
        }
    } else if (type == STANDBY_SET_OUTPUT_PORT) {
        if (standby_handle->output_mask != params) {
            for (int i = 0; i < STANDBY_MAX_REPEAT_FORMAT; i++) {
                standby_handle->output_port_enable[i] = false;
            }

            if (params & MS12_OUTPUT_MASK_SPEAKER) {
                standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_PCM] = true;
            }

            if (params & MS12_OUTPUT_MASK_MC) {
                standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_MCH] = true;
            }

            if (params & MS12_OUTPUT_MASK_DAP) {
                standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DAP] = true;
            }

            if (params & MS12_OUTPUT_MASK_DD) {
                standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DD] = true;
            }

            if (params & MS12_OUTPUT_MASK_DDP) {
                standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_DDP] = true;
            }

            if (params & MS12_OUTPUT_MASK_MAT) {
                standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_MAT_UPPER] = true;
                standby_handle->output_port_enable[STANDBY_REPEAT_FORMAT_MAT_LOWER] = true;
            }

            for (int i = 0; i < STANDBY_MAX_REPEAT_FORMAT; i++) {
                standby_handle->frame_is_match[i] = false;
            }
            standby_handle->reset_check_cnt = RESET_CHECK_DELAY_CNT;
            standby_handle->output_mask = params;
        }
    } else if (type == STANDBY_SET_ATMOS_LOCK) {
        standby_handle->atmos_lock = params;
    }

    pthread_mutex_unlock(&standby_handle->lock);
    return 0;
}

