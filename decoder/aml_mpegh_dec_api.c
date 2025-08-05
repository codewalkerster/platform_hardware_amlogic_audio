/*
 * Copyright (C) 2025 Amlogic Corporation.
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

#define LOG_TAG "aml_audio_mpegh_dec"
#define LOG_NDEBUG 0

#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sound/asound.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <audio_utils/channels.h>
#include <audio_utils/format.h>
#include "aml_dec_api.h"
#include "audio_data_process.h"
#include "aml_malloc_debug.h"
#include "audio_hw_utils.h"
#include "aml_mpegh_dec_api.h"
#include "aml_dump_debug.h"

#define AML_MPEGH_DUMP_FILE_DIR                      "/data/vendor/audiohal/"
#define AML_MPEGH_PROP_DEBUG_FLAG                    "vendor.media.audio.mpeghdebug"

typedef struct mpegh_debug_s {
    bool debug_flag;
    FILE* fp_input_raw;
    FILE* fp_decode_pcm;
} mpegh_debug_t;

static mpegh_debug_t _mpegh_debug = {0};

/* channel mapping info:
Index | Output Channels | Description
----: | --------------: | :----------
-1    |                 | output core decoder signal without rendering (for testing purposes only)
1    |  1              | mono
2    |  2              | stereo
3    |  3              | stereo plus center front
4    |  4              | stereo plus center front and rear center
5    |  5              | 5.1 without LFE
6    |  6              | 5.1
7    |  8              | 7.1 front center: 5.1 with additional left/right front center
9    |  3              | stereo plus rear center
10    |  4              | front and surround speakers
11    |  7              | 5.1 with additional rear center
12    |  8              | 7.1 rear surround: 5.1 with additional rear surrounds
13    | 24              | 22.2 channel mapping
14    |  8              | 7.1 front vertical height: 5.1 with additional front vertical height
15    | 12              | 10.2 : 7.1 rear surround with additional front vertical height, rear center and a second LFE
16    | 10              |  9.1 : 5.1 + front vertical heights + surround vertical heights
17    | 12              | 11.1 : 5.1 + front vertical heights + surround vertical heights + center front vertical height + top center
18    | 14              | 13.1 : 7.1 rear surround + front vertical heights + surround vertical heights + center front vertical
19    | 12              | 11.1 : 7.1 rear surround + front vertical heights + surround vertical heights
20    | 14              | 13.1 : 9.1 + 4
*/
static int mpegh_channel_mapping(int aml_ch) {
    int mpegh_ch_idx = -1;

    if (8 == aml_ch) {
        mpegh_ch_idx = 7;
    } else if (6 >= aml_ch){
        mpegh_ch_idx = aml_ch;  //default direct mapping
    } else {
        AM_LOGE("need mapping channel error!");
    }

    return mpegh_ch_idx;
}
static void mpegh_decoder_endian16_convert(void *buf, int size)
{
    int i;
    unsigned short *p = (unsigned short *)buf;
    for (i = 0; i < size / 2; i++, p++) {
        *p = ((*p & 0xff) << 8) | ((*p) >> 8);
    }
}

static int unload_mpegh_decoder_lib(mpegh_dec_t *mpegh_dec) {
    mpegh_decoder_operations_t *mpegh_op = &mpegh_dec->mpegh_op;
    if (mpegh_op) {
        mpegh_op->init = NULL;
        mpegh_op->decode = NULL;
        mpegh_op->release = NULL;
        mpegh_op->getinfo = NULL;
    }

    if (mpegh_dec->mpeghLibHandler) {
        dlclose(mpegh_dec->mpeghLibHandler);
        mpegh_dec->mpeghLibHandler = NULL;
    }
    return 0;
}

static int load_mpegh_decoder_lib(mpegh_dec_t *mpegh_dec) {
    AM_LOGI("enter");

    mpegh_decoder_operations_t *mpegh_op = &mpegh_dec->mpegh_op;

    mpegh_dec->mpeghLibHandler = dlopen(MPEGH_LIB_PATH, RTLD_NOW);
    if (!mpegh_dec->mpeghLibHandler) {
        AM_LOGE("failed to open (libcdkMpeghDecoder.so), %s!", dlerror());
        return -1;
    }

    AM_LOGV("[mpegh_op->mpeghLibHandler]");
    mpegh_op->init = (int (*) (void *))dlsym(mpegh_dec->mpeghLibHandler, "audio_dec_init");
    if (!mpegh_op->init) {
        goto fail;
    }
    AM_LOGV("[audio_dec_init dlsym success]");

    mpegh_op->decode = (int (*)(void *, char *outbuf, int *outlen, char *inbuf, int inlen))
                          dlsym(mpegh_dec->mpeghLibHandler, "audio_dec_decode");
    if (!mpegh_op->decode) {
        goto fail;
    }
    AM_LOGV("[audio_dec_decode dlsym success]");

    mpegh_op->release = (int (*)(void *)) dlsym(mpegh_dec->mpeghLibHandler, "audio_dec_release");
    if (!mpegh_op->release) {
        goto fail;
    }
    AM_LOGV("[audio_dec_release dlsym success]");

    mpegh_op->getinfo = (int (*)(void *, AudioInfo *pAudioInfo)) dlsym(mpegh_dec->mpeghLibHandler, "audio_dec_getinfo");
    if (!mpegh_op->getinfo) {
        goto fail;
    }
    AM_LOGV("[audio_dec_getinfo dlsym success]");
    return 0;

fail:
    AM_LOGE("cant find decoder function, %s!", dlerror());
    return -1;
}

static int mpegh_decoder_release(aml_dec_t *aml_dec) {
    dec_data_info_t *dec_pcm_data = NULL;
    dec_data_info_t *ad_dec_pcm_data = NULL;
    dec_data_info_t *dec_raw_data = NULL;
    if (!aml_dec) {
        return 0;
    }

    mpegh_dec_t *mpegh_dec = (mpegh_dec_t *)aml_dec;
    mpegh_decoder_operations_t *mpegh_op = &mpegh_dec->mpegh_op;

    dec_pcm_data = &aml_dec->dec_pcm_data;
    if (dec_pcm_data->buf) {
        aml_audio_free(dec_pcm_data->buf);
    }

    ad_dec_pcm_data = &aml_dec->ad_dec_pcm_data;
    if (ad_dec_pcm_data->buf) {
        aml_audio_free(ad_dec_pcm_data->buf);
        ad_dec_pcm_data->buf = NULL;
    }

    dec_raw_data = &aml_dec->dec_raw_data;
    if (dec_raw_data->buf) {
        aml_audio_free(dec_raw_data->buf);
        dec_raw_data->buf = NULL;
    }

    if (!mpegh_dec->encoder_handle) {
      iec61937_encode_close(mpegh_dec->encoder_handle);
      mpegh_dec->encoder_handle = NULL;
    }

    if (_mpegh_debug.fp_input_raw) {
        fclose(_mpegh_debug.fp_input_raw);
        _mpegh_debug.fp_input_raw = NULL;
    }
    if (_mpegh_debug.fp_decode_pcm) {
        fclose(_mpegh_debug.fp_decode_pcm);
        _mpegh_debug.fp_decode_pcm = NULL;
    }

    mpegh_op->release((void *)mpegh_op);
    unload_mpegh_decoder_lib(mpegh_dec);
    aml_audio_free(mpegh_dec);
    return 0;
}

static int mpegh_decoder_init(aml_dec_t **ppaml_dec, aml_dec_config_t *dec_config) {
    aml_dec_t *aml_dec               = NULL;
    mpegh_dec_t *mpegh_dec           = NULL;
    dec_data_info_t *dec_pcm_data    = NULL;
    dec_data_info_t *dec_raw_data    = NULL;
    aml_mpegh_config_t *mpegh_config = NULL;

    if ((!dec_config) || (!ppaml_dec)) {
        AM_LOGE("ppaml_dec:%p, dec_config:%p, failed!", ppaml_dec, dec_config);
        goto fail;
    }

    mpegh_dec = aml_audio_calloc(1, sizeof(mpegh_dec_t));
    if (!mpegh_dec) {
        AM_LOGE("malloc mpegh_dec size:%zu, failed!", sizeof(mpegh_dec_t));
        goto fail;
    }

    aml_dec = &mpegh_dec->aml_dec;
    dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_pcm_data->buf_size = MPEGH_MAX_LENGTH;
    dec_pcm_data->buf = (unsigned char*) aml_audio_calloc(1, dec_pcm_data->buf_size);
    if (dec_pcm_data->buf == NULL) {
        AM_LOGE("malloc dec_mpegh_data->buf size:%d, failed!", dec_pcm_data->buf_size);
        goto fail;
    }

    dec_raw_data = &aml_dec->dec_raw_data;
    dec_raw_data->buf_size = MAX_IEC61937_FRAME_SIZE_BYTES;
    dec_raw_data->buf = (unsigned char*) aml_audio_calloc(1, dec_raw_data->buf_size);
    if (NULL == dec_raw_data->buf) {
        AM_LOGE("malloc dec_raw_data size:%d, failed!", dec_raw_data->buf_size);
        goto fail;
    }

    mpegh_dec->rate_factor = 4;//samplerate factor: 4 or 16, Refer to IEC 61937-13-4.5:IEC 61937-13 latency
    //From my test results, if the factor is 4, the size of IEC is 16384; If the factor is 16, the size of IEC is 65536
    mpegh_dec->encoder_handle = iec61937_encode_open(mpegh_dec->rate_factor);
    if (!mpegh_dec->encoder_handle) {
      AM_LOGE("iec61937_encode_open failed!");
      goto fail;
    }

    if (load_mpegh_decoder_lib(mpegh_dec)) {
        AM_LOGE("load_mpegh_decoder_lib, mpegh_dec:%p, failed!", mpegh_dec);
        goto fail;
    }

    mpegh_config = &dec_config->mpegh_config;
    memcpy(&mpegh_dec->mpegh_config, mpegh_config, sizeof(aml_mpegh_config_t));
    AM_LOGI("MPEGH samplerate:%d ch:%d\n", mpegh_config->samplerate, mpegh_config->channel);
    mpegh_dec->mpegh_op.channels = mpegh_channel_mapping(mpegh_config->channel);

    //Indicates that the audio stream is transmitted in the form of data packets
    mpegh_dec->mpegh_op.nAudioDecoderType = TT_MHAS_PACKETIZED;
    //init mpegh decoder
    int ret = mpegh_dec->mpegh_op.init((void *)&mpegh_dec->mpegh_op);
    if (0 != ret) {
        AM_LOGE("mpegh_op.init, mpegh_dec:%p, failed!", mpegh_dec);
        goto fail;
    }
    if (get_debug_value(AML_DUMP_AUDIOHAL_DECODER)) {
        char name[64] = {0};
        snprintf(name, 64, "%smpegh_decode_pcm.pcm", AML_MPEGH_DUMP_FILE_DIR);
        _mpegh_debug.fp_decode_pcm = fopen(name, "a+");
        if (!_mpegh_debug.fp_decode_pcm) {
            ALOGW("[Error] Can't write to %s", name);
        }

        memset(name, 0x00, sizeof(name));
        snprintf(name, 64, "%smpegh_decode_input.raw", AML_MPEGH_DUMP_FILE_DIR);
        _mpegh_debug.fp_input_raw = fopen(name, "ab+");
        if (!_mpegh_debug.fp_input_raw) {
            ALOGW("[Error] Can't write to %s", name);
        }
    }
    if (property_get_bool(AML_MPEGH_PROP_DEBUG_FLAG, 0)) {
        ALOGD("enable mpegh debug log");
        _mpegh_debug.debug_flag = true;
    } else {
        ALOGD("disable mpegh debug log");
        _mpegh_debug.debug_flag = false;
    }
    AM_LOGI("mpegh_dec:%p, pdecoder:%p, init success!", mpegh_dec, mpegh_dec->mpegh_op.pdecoder);
    *ppaml_dec = (aml_dec_t *)mpegh_dec;
    mpegh_dec->total_pcm_size  = 0;
    mpegh_dec->total_raw_size  = 0;
    mpegh_dec->total_time      = 0;

    return 0;

fail:
    if (mpegh_dec) {
        mpegh_decoder_release(aml_dec);
        mpegh_dec = NULL;
    }

    *ppaml_dec = NULL;
    return -1;
}

static int mpegh_decoder_process(aml_dec_t *aml_dec, unsigned char *buffer, int bytes)
{
    if ((!aml_dec) || (!buffer) || (0 == bytes)) {
        AM_LOGE("aml_dec:%p, buffer:%p, error!", aml_dec, buffer);
        return -1;
    }
    mpegh_dec_t *mpegh_dec = (mpegh_dec_t *)aml_dec;
    mpegh_decoder_operations_t *mpegh_op = &mpegh_dec->mpegh_op;
    dec_data_info_t * dec_pcm_data = &aml_dec->dec_pcm_data;
    dec_data_info_t * dec_raw_data = &aml_dec->dec_raw_data;
    int pcm_len = 0;
    uint32_t duration = 1024;     //Recommend setting: 1024 samples. Refer to IEC 61937-13-4.5:IEC 61937-13 latency
    bool b_read_more_data = false;

    uint32_t iec_out_size = dec_raw_data->buf_size;
    // Encode into iec61937-13 format
    IECENC_RESULT returnValue = iec61937_encode_process(mpegh_dec->encoder_handle, buffer, bytes,\
        &b_read_more_data, duration, dec_raw_data->buf, &iec_out_size);
    if (returnValue != IECENC_OK) {
        AM_LOGE("ERROR: Internal buffer too small or rate factor too small or duration exceeds maximum.");
        dec_raw_data->data_len = 0;
    } else {
        dec_raw_data->data_len = iec_out_size;
        //Since pc pd monitor can only use little endian data to recognize audo format,
        //we convert big endian data to little endian here
        mpegh_decoder_endian16_convert((void*)dec_raw_data->buf, dec_raw_data->data_len);
        if (get_debug_value(AML_DUMP_AUDIOHAL_DECODER)) {
            aml_dump_audio_bitstreams("/data/vendor/audiohal/iec_mpegh_encoder_output.raw", dec_raw_data->buf, iec_out_size);
        }
    }
    dec_raw_data->data_format = AUDIO_FORMAT_IEC61937;
    dec_raw_data->sub_format = AUDIO_FORMAT_MPEGH;
    dec_raw_data->data_sr = 192000;
    if (mpegh_dec->rate_factor == 16) {
        dec_raw_data->data_ch = 8;
    } else {
        dec_raw_data->data_ch = 2;
    }
    AM_LOGD("iec_size:%d", dec_raw_data->data_len);

    if (_mpegh_debug.fp_input_raw) {
        fwrite(buffer, 1, bytes, _mpegh_debug.fp_input_raw);
    }

    int decode_len = mpegh_op->decode(mpegh_op, (char *)dec_pcm_data->buf, &pcm_len, (char *)buffer, bytes);
    if (decode_len < 0) {
        ALOGW("[%s:%d] mpegh decode fail!",__func__, __LINE__);
        return AML_DEC_RETURN_TYPE_FAIL;
    }
    if (pcm_len > 0) {
        AudioInfo audioinfo;
        mpegh_op->getinfo(mpegh_op, &audioinfo);
        dec_pcm_data->data_ch = audioinfo.channels;
        dec_pcm_data->data_sr = audioinfo.samplerate;
        dec_pcm_data->data_len = pcm_len;
        dec_pcm_data->data_format = AUDIO_FORMAT_PCM_32_BIT;
        if (_mpegh_debug.fp_decode_pcm) {
            fwrite(dec_pcm_data->buf, 1, pcm_len, _mpegh_debug.fp_decode_pcm);
        }
    }
    if (_mpegh_debug.debug_flag)
        AM_LOGI("decode bytes:%d, pcm_len:%d, data_ch:%d, data_sr:%d", bytes, pcm_len, dec_pcm_data->data_ch, dec_pcm_data->data_sr);

    return bytes;
}

static int mpegh_decoder_getinfo(aml_dec_t *aml_dec, aml_dec_info_type_t info_type, aml_dec_info_t *dec_info)
{
    mpegh_dec_t *mpegh_dec = (mpegh_dec_t *)aml_dec;

    if (dec_info) {
        memset(&dec_info->dec_info, 0x00, sizeof(aml_dec_stream_info_t));
        memcpy(&dec_info->dec_info, &mpegh_dec->stream_info, sizeof(aml_dec_stream_info_t));
        mpegh_decoder_operations_t *mpegh_op = &mpegh_dec->mpegh_op;
        AudioInfo audioinfo;

        mpegh_op->getinfo(mpegh_op, &audioinfo);

        dec_info->dec_info.stream_bitrate = audioinfo.bitrate;
        dec_info->dec_info.stream_ch = audioinfo.channels;
        dec_info->dec_info.stream_sr = audioinfo.samplerate;
    }
    return 0;
}

aml_dec_func_t *get_mpegh_dec_func_handle(void)
{
    aml_dec_func_t *amlDcvFunc = NULL;

    amlDcvFunc = (struct aml_dec_func *)aml_audio_calloc(1, sizeof(struct aml_dec_func));
    if (amlDcvFunc) {
        amlDcvFunc->f_init       = mpegh_decoder_init;
        amlDcvFunc->f_release    = mpegh_decoder_release;
        amlDcvFunc->f_process    = mpegh_decoder_process;
        amlDcvFunc->f_info       = mpegh_decoder_getinfo;
        amlDcvFunc->f_config     = NULL;
    } else {
        AM_LOGE(" calloc amlDcvFunc:%p failed", amlDcvFunc);
        amlDcvFunc = NULL;
    }

    return amlDcvFunc;
}

