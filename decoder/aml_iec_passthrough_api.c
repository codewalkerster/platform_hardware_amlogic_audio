/*
 * Copyright (C) 2022 Amlogic Corporation.
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

#define LOG_TAG "audio_hw_decoder_iecpassthrough"
//#define LOG_NDEBUG 0

#include <dlfcn.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include "aml_dec_api.h"
#include "audio_data_process.h"
#include "aml_malloc_debug.h"
#include "aml_iec_passthrough_api.h"

#define AML_IEC_PROP_DEBUG_FLAG                    "vendor.media.audio.iecdebug"
#define AML_IEC_PROP_DUMP_RAW                      "vendor.media.audio.iecdump.output.raw"
#define AML_IEC_DUMP_FILE_DIR                      "/data/vendor/audiohal/"

#define IEC_MAX_LENGTH (1024 * 64 * 2)
typedef struct iec_debug_s {
    bool debug_flag;
    FILE* fp_output_raw;
} iec_debug_t;

static iec_debug_t _iec_debug = {0};

static int iec_passthrough_init(aml_dec_t **ppaml_dec, aml_dec_config_t * dec_config)
{
    struct iec_dec_t  *iec_dec = NULL;
    aml_dec_t  *aml_dec = NULL;

    dec_data_info_t * dec_pcm_data = NULL;
    dec_data_info_t * dec_raw_data = NULL;
    dec_data_info_t * raw_in_data  = NULL;
    dec_data_info_t * ad_dec_pcm_data = NULL;

    ALOGI("%s enter\n", __func__);
    iec_dec = aml_audio_calloc(1, sizeof(struct iec_dec_t));
    if (iec_dec == NULL) {
        ALOGE("[%s:%d] calloc iec_dec failed\n", __func__, __LINE__);
        return -1;
    }

    aml_dec = &iec_dec->aml_dec;
    memcpy(&iec_dec->iec_config, &dec_config->iec_config, sizeof(aml_iec_config_t));

    dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_pcm_data->buf_size = IEC_MAX_LENGTH * 2;
    dec_pcm_data->data_ch = dec_config->iec_config.channel;
    if (dec_config->iec_config.samplerate % 48000 == 0) {
        dec_pcm_data->data_sr = 48000;
    } else if (dec_config->iec_config.samplerate % 44100 == 0) {
        dec_pcm_data->data_sr = 44100;
    } else {
        dec_pcm_data->data_sr = dec_config->iec_config.samplerate;
    }
    dec_pcm_data->buf = (unsigned char*) aml_audio_calloc(1, dec_pcm_data->buf_size);
    if (!dec_pcm_data->buf) {
        ALOGE("[%s:%d] calloc dec_pcm_data->buf failed\n", __func__, __LINE__);
        goto exit;
    }
    dec_pcm_data->data_format = AUDIO_FORMAT_PCM_16_BIT;

    dec_raw_data = &aml_dec->dec_raw_data;
    dec_raw_data->buf_size = IEC_MAX_LENGTH * 2;
    dec_raw_data->data_ch = dec_config->iec_config.channel;
    dec_raw_data->data_sr = dec_config->iec_config.samplerate;
    dec_raw_data->data_format = AUDIO_FORMAT_IEC61937;
    dec_raw_data->sub_format = dec_config->iec_config.format;
    dec_raw_data->buf = (unsigned char*) aml_audio_calloc(1, dec_raw_data->buf_size);
    if (!dec_raw_data->buf) {
        ALOGE("[%s:%d] calloc dec_raw_data->buf failed\n", __func__, __LINE__);
        goto exit;
    }

    raw_in_data  = &aml_dec->raw_in_data;
    raw_in_data->buf_size = IEC_MAX_LENGTH * 2;
    raw_in_data->data_ch = dec_config->iec_config.channel;
    raw_in_data->data_sr = dec_config->iec_config.samplerate;
    raw_in_data->data_format = AUDIO_FORMAT_IEC61937;
    raw_in_data->sub_format = dec_config->iec_config.format;
    raw_in_data->buf = (unsigned char*) aml_audio_calloc(1, raw_in_data->buf_size);
    if (!raw_in_data->buf) {
        ALOGE("[%s:%d] calloc raw_in_data->buf failed\n", __func__, __LINE__);
        goto exit;
    }

    if (dec_config->iec_config.is_iec61937) {
        aml_dec->format = AUDIO_FORMAT_IEC61937;
    }
    *ppaml_dec = (aml_dec_t *)iec_dec;

    if (property_get_bool(AML_IEC_PROP_DUMP_RAW, 0)) {
        char name[64] = {0};
        snprintf(name, 64, "%siec_passthr_output.raw", AML_IEC_DUMP_FILE_DIR);
        _iec_debug.fp_output_raw = fopen(name, "a+");
        if (!_iec_debug.fp_output_raw) {
            ALOGW("[Error] Can't write to %s", name);
        }
    }

    if (property_get_bool(AML_IEC_PROP_DEBUG_FLAG, 0)) {
        ALOGD("enable iec_passthrough debug log");
        _iec_debug.debug_flag = true;
    } else {
        ALOGD("disable iec_passthrough debug log");
        _iec_debug.debug_flag = false;
    }
    ALOGI("%s success", __func__);
    return 0;

exit:
    if (iec_dec) {
        if (dec_pcm_data->buf) {
            aml_audio_free(dec_pcm_data->buf);
        }
        if (dec_raw_data->buf) {
            aml_audio_free(dec_raw_data->buf);
        }
        if (raw_in_data->buf) {
            aml_audio_free(raw_in_data->buf);
        }
        aml_audio_free(iec_dec);
    }
    *ppaml_dec = NULL;
    ALOGE("%s failed", __func__);
    return -1;
}

static int iec_passthrough_release(aml_dec_t * aml_dec)
{
    ALOGI("%s enter\n", __func__);
    struct iec_dec_t  *iec_dec = (struct iec_dec_t *)aml_dec;

    if (iec_dec) {
        if (aml_dec->dec_pcm_data.buf) {
            aml_audio_free(aml_dec->dec_pcm_data.buf);
        }

        if (aml_dec->dec_raw_data.buf) {
            aml_audio_free(aml_dec->dec_raw_data.buf);
        }

        if (aml_dec->raw_in_data.buf) {
            aml_audio_free(aml_dec->raw_in_data.buf);
        }

        if (aml_dec->decFunc) {
            aml_audio_free(aml_dec->decFunc);
            aml_dec->decFunc = NULL;
        }
        aml_audio_free(aml_dec);
        aml_dec = NULL;
    }
    ALOGI("%s success", __func__);
    return 0;
}

static int iec_passthrough_process(aml_dec_t * aml_dec, unsigned char*buffer, int bytes)
{
    dec_data_info_t * dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_data_info_t * dec_raw_data = &aml_dec->dec_raw_data;
    dec_data_info_t * raw_in_data  = &aml_dec->raw_in_data;
    struct iec_dec_t  *iec_dec = NULL;
    if (_iec_debug.debug_flag) {
        ALOGI("%s bytes: %d\n", __func__, bytes);
    }

    int channel_convert = 1;
    dec_pcm_data->data_len = 0;
    dec_raw_data->data_len = 0;
    raw_in_data->data_len = 0;
    iec_dec = (struct iec_dec_t *)aml_dec;

    if (bytes > raw_in_data->buf_size) {
        ALOGE("%s input size too big =%d max=%d", __func__, bytes, raw_in_data->buf_size);
        return 0;
    }

    channel_convert = iec_dec->iec_config.channel / 2;

    /*we always convert to 2ch data*/
    dec_pcm_data->data_len = bytes / (channel_convert);
    dec_pcm_data->data_ch  = 2;

    if (dec_raw_data->sub_format == AUDIO_FORMAT_AC3 || dec_raw_data->sub_format == AUDIO_FORMAT_DTS) {
        dec_pcm_data->data_len = bytes;
        dec_pcm_data->data_ch  = 2;
        dec_raw_data->data_ch  = 2;
    } else if (dec_raw_data->sub_format == AUDIO_FORMAT_E_AC3) {
        dec_pcm_data->data_len = bytes / 4;
        dec_pcm_data->data_ch  = 2;
        dec_raw_data->data_ch  = 2;
        if (raw_in_data->data_sr >= 176400) {
            dec_raw_data->data_sr  = raw_in_data->data_sr / 4;
        } else {
            dec_raw_data->data_sr = raw_in_data->data_sr;
        }
    } else if (dec_raw_data->sub_format == AUDIO_FORMAT_DTS_HD && raw_in_data->data_ch == 2) {
        dec_pcm_data->data_len = bytes / 4;
        dec_pcm_data->data_ch  = 2;
        dec_raw_data->data_ch  = 2;
        dec_raw_data->data_sr = raw_in_data->data_sr;
    } else if (dec_raw_data->sub_format == AUDIO_FORMAT_DTS_HD
            || dec_raw_data->sub_format == AUDIO_FORMAT_MAT
            || dec_raw_data->sub_format == AUDIO_FORMAT_DOLBY_TRUEHD) {
        dec_pcm_data->data_len = bytes / 16;
        dec_pcm_data->data_ch  = 2;
        dec_raw_data->data_ch  = 8;
        dec_raw_data->data_sr = raw_in_data->data_sr;
    }

    memset(dec_pcm_data->buf, 0, sizeof(char)*dec_pcm_data->data_len);

    dec_raw_data->data_len = bytes;
    memcpy(dec_raw_data->buf, buffer, sizeof(char)*bytes);

    raw_in_data->data_len = bytes;
    memcpy(raw_in_data->buf, buffer, sizeof(char)*bytes);

    if (dec_raw_data->data_len && _iec_debug.fp_output_raw) {
        fwrite(dec_raw_data->buf, 1, dec_raw_data->data_len, _iec_debug.fp_output_raw);
    }
    if (_iec_debug.debug_flag) {
        ALOGD("%s pcm: [length:%d sr:%d ch:%d] raw: [length:%d sr:%d ch:%d sub_format:0x%x]", __func__,
            dec_pcm_data->data_len, dec_pcm_data->data_sr, dec_pcm_data->data_ch,
            dec_raw_data->data_len, dec_raw_data->data_sr, dec_raw_data->data_ch, dec_raw_data->sub_format);
    }
    return bytes;
}

static int iec_passthrough_getinfo(aml_dec_t *aml_dec __unused, aml_dec_info_type_t info_type __unused, aml_dec_info_t * dec_info __unused)
{
    int ret = 0;
    ALOGV("iec_passthrough_getinfo \n");
    return ret;
}

int iec_passthrough_config(aml_dec_t * aml_dec __unused, aml_dec_config_type_t config_type __unused, aml_dec_config_t * dec_config __unused)
{
    int ret = 0;
    ALOGV("iec_passthrough_config \n");
    return ret;
}

aml_dec_func_t *get_iec_dec_func_handle(void)
{
    aml_dec_func_t *amlDcvFunc = NULL;

    amlDcvFunc = (struct aml_dec_func *)aml_audio_calloc(1, sizeof(struct aml_dec_func));
    if (amlDcvFunc) {
        amlDcvFunc->f_init       = iec_passthrough_init;
        amlDcvFunc->f_release    = iec_passthrough_release;
        amlDcvFunc->f_process    = iec_passthrough_process;
        amlDcvFunc->f_config     = iec_passthrough_config;
        amlDcvFunc->f_info       = iec_passthrough_getinfo;
    } else {
        AM_LOGE(" calloc amlDcvFunc:%p failed", amlDcvFunc);
        amlDcvFunc = NULL;
    }

    return amlDcvFunc;
}

