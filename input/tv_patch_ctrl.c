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

#define LOG_TAG "audio_hw_input_tv"
//#define LOG_NDEBUG 0

#include <math.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <utils/Timers.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <hardware/audio.h>
#include <aml_data_utils.h>
#include <audio_utils/channels.h>
#include <audio_utils/format.h>
#include <hardware/audio_alsaops.h>
#if ANDROID_PLATFORM_SDK_VERSION >= 25 // 8.0
#include <system/audio-base.h>
#endif

#include "audio_hw.h"
#include "aml_audio_stream.h"
#include "audio_hw_utils.h"
#include "aml_audio_timer.h"
#include "alsa_config_parameters.h"
#include "tv_patch_avsync.h"
#include "aml_ng.h"
#include "alsa_device_parser.h"
#include "audio_hw_ms12_v2.h"
#include "tv_patch_ctrl.h"
#include "audio_hw_resource_mgr.h"
#include "device_patch_mgr.h"
#include "component_picture_mode.h"
#include "audio_data_process.h"

#define INVALID_TYPE                -1
#define MINUS_3_DB_IN_FLOAT M_SQRT1_2 // -3dB = 0.70710678

typedef enum AML_INPUT_STREAM_CONFIG_TYPE {
    AML_INPUT_STREAM_CONFIG_TYPE_CHANNELS   = 0,
    AML_INPUT_STREAM_CONFIG_TYPE_PERIODS    = 1,

    AML_INPUT_STREAM_CONFIG_TYPE_BUTT       = -1,
} AML_INPUT_STREAM_CONFIG_TYPE_E;


/*==================================input commands=========================================*/
static inline int find_61937_sync_word(char *buffer, int size)
{
    int i = -1;
    if (size < 8) {
        return i;
    }

    for (i = 0; i < (size - 3); i++) {
        if (buffer[i + 0] == 0x72 && buffer[i + 1] == 0xF8 && buffer[i + 2] == 0x1F && buffer[i + 3] == 0x4E) {
            return i;
        }
        if (buffer[i + 0] == 0xF8 && buffer[i + 1] == 0x72 && buffer[i + 2] == 0x4E && buffer[i + 3] == 0x1F) {
            return i;
        }
    }
    return -1;
}

/* expand channels or contract channels*/
int input_stream_channels_adjust(struct audio_stream_in *stream, void* buffer, size_t bytes, bool downmix)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    /* for earc layoutB which should discard 6 ch data for every 8ch */
    int norminal_channel_cnt = 0;
    int ret = -1;

    if (!in || !bytes)
        return ret;
    if (!patch) {
        AM_LOGE("%s(),get_dev_patch is fail",__func__);
        return ret;
    }

    int channel_count = audio_channel_count_from_in_mask(in->hal_channel_mask);

    if (!channel_count)
        return ret;

    norminal_channel_cnt = in->config.channels;
    if (is_same_patch_src(adev, SRC_ARCIN)) {
        if (patch->arc_layout_b) {
            norminal_channel_cnt = 8;
            in->tv_param.read_mul_factor = EAC3_MULTIPLIER;
        } else if (patch->input_sample_rate > 96000) {
            in->tv_param.read_mul_factor = HBR_MULTIPLIER;
        } else if (patch->input_sample_rate > 48000) {
            in->tv_param.read_mul_factor = EAC3_MULTIPLIER;
        }
    }

    size_t read_bytes = norminal_channel_cnt * bytes / channel_count;
    if (!in->input_tmp_buffer || in->input_tmp_buffer_size < read_bytes) {
        in->input_tmp_buffer = aml_audio_realloc(in->input_tmp_buffer, read_bytes * 2);
        if (!in->input_tmp_buffer) {
            AM_LOGE("aml_audio_realloc is fail");
            return ret;
        }
        in->input_tmp_buffer_size = read_bytes;
    }

    ret = aml_alsa_input_read(stream, in->input_tmp_buffer, read_bytes);
    if (!ret && get_debug_value(AML_DUMP_AUDIOHAL_TV)) {
        aml_audio_dump_audio_bitstreams("/data/vendor/audiohal/tv_read.raw", in->input_tmp_buffer, read_bytes);
    }
    if (in->config.format == PCM_FORMAT_S16_LE) {
        if (downmix) {
            int samples = read_bytes / 2;
            int output_samples = bytes / 2;
            memcpy_by_audio_format(in->input_tmp_buffer, AUDIO_FORMAT_PCM_FLOAT,
                in->input_tmp_buffer, AUDIO_FORMAT_PCM_16_BIT, samples);
            Downmix_foldFrom7Point1((float *)in->input_tmp_buffer,
                (float *)in->input_tmp_buffer, samples >> 3, false);
            memcpy_by_audio_format(buffer, AUDIO_FORMAT_PCM_16_BIT,
                in->input_tmp_buffer, AUDIO_FORMAT_PCM_FLOAT, output_samples);
        } else {
            adjust_channels(in->input_tmp_buffer, norminal_channel_cnt,
                buffer, channel_count, 2, read_bytes);
        }
    } else if (in->config.format == PCM_FORMAT_S32_LE) {
        if (downmix) {
            int samples = read_bytes / 4;
            int output_samples = bytes / 4;
            memcpy_by_audio_format(in->input_tmp_buffer, AUDIO_FORMAT_PCM_FLOAT,
                in->input_tmp_buffer, AUDIO_FORMAT_PCM_32_BIT, samples);
            Downmix_foldFrom7Point1((float *)in->input_tmp_buffer,
                (float *)in->input_tmp_buffer, samples >> 3, false);
            memcpy_by_audio_format(buffer, AUDIO_FORMAT_PCM_32_BIT,
                in->input_tmp_buffer, AUDIO_FORMAT_PCM_FLOAT, output_samples);
        } else {
            adjust_channels(in->input_tmp_buffer, norminal_channel_cnt,
                buffer, channel_count, 4, read_bytes);
        }
    }
    return ret;
}

void *input_stream_do_resample(struct audio_stream_in *stream, void *buffer, int *bytes)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    void *buf_ret = buffer;
    int ret = 0;

    /* From T7C+, enable HW resampler for earcrx as it supports multi-ch processing
     * and skip this SW resample.
     */
    if (is_earcrx_support_hw_multi_ch_resample(&adev->alsa_mixer))
        return buf_ret;

    if (in->config.channels > 2) {
        /* only pcm support multi-ch config which needs SW resampler */
        int cur_samplerate = audio_parse_get_audio_samplerate(patch->audio_parse_para);
        int output_sr = 48000;

        if (cur_samplerate != output_sr) {
            audio_resample_config_t cfg = {
                .aformat = audio_format_from_pcm_format(in->config.format),
                .channels = 2,
                .input_sr = cur_samplerate,
                .output_sr = output_sr,
            };
            ret = aml_audio_resample_process_ex(&in->resample_handle, &cfg, buffer, *bytes);
            if (ret == 0) {
                buf_ret = in->resample_handle->resample_buffer;
                *bytes = in->resample_handle->resample_size;
            } else {
                AM_LOGE("aml_audio_resample_process_ex fail ret=%d", ret);
            }
        }
    }

    return buf_ret;
}

bool is_HBR_stream(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    bool ret = false;

    if (aml_dev->in_device & AUDIO_DEVICE_IN_HDMI && get_dev_patch(aml_dev)) {
        struct aml_audio_patch *audio_patch = get_dev_patch(aml_dev);
        if (!audio_patch) {
            AM_LOGE("%s(),get_dev_patch is fail",__func__);
            return ret;
        }
        audio_type_parse_t *audio_type_status = (audio_type_parse_t *)audio_patch->audio_parse_para;
        if (audio_type_status && audio_type_status->soft_parser != 1) {
            if (audio_patch->param_config.last_audio_packet_type == AUDIO_PACKET_HBR) {
                ret = true;
            }
        }
    } else if (in->device == AUDIO_DEVICE_IN_HDMI_ARC) {
        return (in->spdif_fmt_hw == MAT);
    }
    return ret;
}

bool is_game_mode(struct aml_audio_device *aml_dev)
{
    if (!is_same_patch_src(aml_dev, SRC_HDMIIN) ||
        !is_dev_patch_exist(aml_dev) ||
        (is_dev_patch_valid(aml_dev) && is_dev_patch_exist(aml_dev) && (get_dev_patch(aml_dev)->input_src != AUDIO_DEVICE_IN_HDMI ||
        get_dev_patch(aml_dev)->IEC61937_format == true))) {
        return false;
    }

    return (is_dev_patch_valid(aml_dev) && get_dev_patch(aml_dev) && get_dev_patch(aml_dev)->pic_mode == PQ_GAME);
}

void aml_check_pic_mode(struct aml_audio_patch *patch)
{
    struct aml_audio_device *aml_dev = NULL;
    if (!patch || patch->input_src != AUDIO_DEVICE_IN_HDMI) {
        return;
    }
    aml_dev = (struct aml_audio_device *)patch->dev;

    if (get_dev_pic_mode(aml_dev) == PQ_GAME && patch->mode_reconfig_flag == true) {
        ALOGD("%s(), IEC61937 data, reconfig audio path", __func__);
        reconfig_dev_pic_mode_in(aml_dev, true);
        reconfig_dev_pic_mode_out(aml_dev, true);
        patch->mode_reconfig_flag = false;
        return;
    }

    /* in PCM data case, picture mode setting changed */
    if (patch->IEC61937_format == false && patch->pic_mode != get_dev_pic_mode(aml_dev)) {
        ALOGD("%s(), pic mode changes from %d to %d", __func__, patch->pic_mode, get_dev_pic_mode(aml_dev));
        reconfig_dev_pic_mode_in(aml_dev, true);
        reconfig_dev_pic_mode_out(aml_dev, true);
        patch->pic_mode = get_dev_pic_mode(aml_dev);
    }

}

bool signal_status_check(audio_devices_t in_device, int *mute_time,
                        struct audio_stream_in *stream) {
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *adev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    hdmiin_audio_packet_t last_audio_packet = patch->param_config.last_audio_packet_type;
    int pre_data_type = patch->param_config.data_type;
    bool is_audio_packet_changed = false, is_data_changed = false;

    hdmiin_audio_packet_t cur_audio_packet = get_hdmiin_audio_packet(&adev->alsa_mixer);
    is_audio_packet_changed = (((cur_audio_packet == AUDIO_PACKET_AUDS) || (cur_audio_packet == AUDIO_PACKET_HBR)) &&
                               (last_audio_packet != cur_audio_packet));

    int cur_data_type = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_HDMIIN_NONAUDIO);
    if (cur_data_type == DATA_NON_PCM && pre_data_type == DATA_PCM) {
        enable_HW_resample(&adev->alsa_mixer, HW_RESAMPLE_DISABLE);
        is_data_changed = true;
        ALOGI("%s Cur_data_type %d", __func__, cur_data_type);
    }

    if (cur_data_type == DATA_PCM && pre_data_type == DATA_NON_PCM) {
        is_data_changed = true;
    }

    patch->param_config.data_type = cur_data_type;
    if (in_device & AUDIO_DEVICE_IN_HDMI) {
        hdmiin_audio_packet_t last_audio_packet = patch->param_config.last_audio_packet_type;
        hdmiin_audio_packet_t cur_audio_packet = get_hdmiin_audio_packet(&adev->alsa_mixer);
        bool is_audio_packet_changed = (((cur_audio_packet == AUDIO_PACKET_AUDS) ||
                                         (cur_audio_packet == AUDIO_PACKET_HBR)) &&
                                        (last_audio_packet != cur_audio_packet));
        bool hw_stable = is_hdmi_in_stable_hw(stream);
        bool hw_format_change = is_hdmi_in_hw_format_change(stream);
        bool hw_sample_rate_change = is_hdmi_in_sample_rate_changed(stream);

        if ((!hw_stable) || is_audio_packet_changed || hw_format_change || hw_sample_rate_change || is_data_changed || adev->reset_hpd) {
            /* HBR audio is stable about 1s */
            *mute_time = 500;

            /* when reset hpd, it takes 2s for audio to be stable */
            if (adev->reset_hpd) {
                *mute_time = 2000;
                adev->reset_hpd = 0;
                ALOGI("%s mute hdmiin %d ms for reset hpd\n", __func__, *mute_time);
            }

            patch->param_config.last_audio_packet_type = cur_audio_packet;
            if (is_audio_packet_changed || hw_format_change) {
                ALOGD("%s() cur_audio_packet = %d, hw_stable = %d, fmt_hw = %d\n",
                    __func__, cur_audio_packet, hw_stable, patch->param_config.spdif_fmt_hw);
            }

            /* only reconfig once for HBR audio*/
            if (hw_stable && cur_audio_packet == AUDIO_PACKET_HBR && patch->param_config.spdif_fmt_hw == MAT) {
                return true;
            }
            return false;
        }
    }
    if ((in_device & AUDIO_DEVICE_IN_TV_TUNER) &&
            !is_atv_in_stable_hw (stream)) {
        *mute_time = 1000;
        return false;
    }
    if ((in_device & AUDIO_DEVICE_IN_SPDIF) &&
            !is_spdif_in_stable_hw(stream)) {
        *mute_time = 1000;
        return false;
    }

    if ((in_device & AUDIO_DEVICE_IN_HDMI_ARC) &&
            !is_earc_in_status_change(stream)) {
        *mute_time = 1000;
        return false;
    }
    if ((in_device & AUDIO_DEVICE_IN_LINE) &&
            !is_av_in_stable_hw(stream)) {
       *mute_time = 100;
       return false;
    }
    return true;
}

bool check_tv_stream_signal(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    struct aml_audio_patch* patch = get_dev_patch(adev);
    int in_mute = 0;
    bool stable = true;
    stable = signal_status_check(adev->in_device, &patch->param_config.mute_mdelay, stream);
    if (!stable) {
        if (patch->param_config.mute_log_cntr == 0)
            ALOGI("%s: audio is unstable, mute channel", __func__);
        if (patch->param_config.mute_log_cntr++ >= 100)
            patch->param_config.mute_log_cntr = 0;
        clock_gettime(CLOCK_MONOTONIC, &patch->param_config.mute_start_ts);
        patch->param_config.mute_flag = true;
    }
    if (patch->param_config.mute_flag) {
        in_mute = Stop_watch(patch->param_config.mute_start_ts, patch->param_config.mute_mdelay);
        if (!in_mute) {
            ALOGI("%s: unmute audio since audio signal is stable", __func__);
            /* The data of ALSA has not been read for a long time in the muted state,
             * resulting in the accumulation of data. So, cache of capture needs to be cleared.
             */
            if (in->pcm && !(in->device & AUDIO_DEVICE_IN_HDMI_ARC || in->device & AUDIO_DEVICE_IN_SPDIF))
                pcm_stop(in->pcm);
            patch->param_config.mute_log_cntr = 0;
            patch->param_config.mute_flag = false;
        }
    }

    /*if need mute input source, don't read data from hardware anymore*/
    if (in_mute) {
        /* when audio is unstable, start avsync*/
        if (patch && in_mute) {
            if (!(in->device & AUDIO_DEVICE_IN_HDMI_ARC || in->device & AUDIO_DEVICE_IN_SPDIF))
                patch->need_do_avsync = true;
            patch->input_signal_stable = false;
            adev->mute_start = true;
            ALOGV("%s: audio is unstable, adev->mute_start %d patch->need_do_avsync %d", __func__, adev->mute_start, patch->need_do_avsync);
        }
        return false;
    } else {
        if (patch) {
            patch->input_signal_stable = true;
        }
    }
    return true;
}

bool check_digital_in_stream_signal(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(aml_dev);
    audio_type_parse_t *audio_type_status = (audio_type_parse_t *)patch->audio_parse_para;
    enum audio_type cur_audio_type = LPCM;

    /* parse thread may have exited ,add the code to avoid NULL point visit*/
    if (audio_type_status == NULL)  {
        return true;
    }

    if (audio_type_status->soft_parser != 1) {
        if (patch->param_config.spdif_fmt_hw == SPDIFIN_AUDIO_TYPE_PAUSE) {
            ALOGV("%s(), hw detect iec61937 PAUSE packet, mute input", __func__);
            return false;
        }
    } else {
        cur_audio_type = audio_parse_get_audio_type_direct(patch->audio_parse_para);
        if (cur_audio_type == PAUSE || cur_audio_type == MUTE) {
            ALOGV("%s(), soft parser iec61937 %s packet, mute input", __func__,
                cur_audio_type == PAUSE ? "PAUSE" : "MUTE");
            return false;
        }
    }

    return true;
}


void audio_raw_data_continuous_check(struct aml_audio_device *aml_dev, audio_type_parse_t *status, char *buffer, int size)
{
    audio_type_parse_t *audio_type_status = status;
    struct aml_audio_patch* patch = get_dev_patch(aml_dev);

    if (!audio_type_status || !get_dev_patch(aml_dev)) {
        return;
    }

    int sync_word_offset = find_61937_sync_word(buffer, size);
    if (sync_word_offset >= 0) {
        patch->sync_offset = sync_word_offset;
        if (patch->start_mute) {
            set_output_device_mute(aml_dev, AUDIO_DEVICE_OUT_SPEAKER, false, true);;
            patch->start_mute = false;
            patch->mdelay = 0;
        }
        if (patch->read_size > 0) {
            patch->read_size = 0;
        }
        if (!patch->read_size) {
            patch->read_size = size;
            patch->read_size -= sync_word_offset;
        }
    } else if (patch->sync_offset >= 0) {
        if ((patch->read_size < audio_type_status->package_size) && ((patch->read_size + size) > audio_type_status->package_size)) {
            set_output_device_mute(aml_dev, AUDIO_DEVICE_OUT_SPEAKER, true, true);
            clock_gettime(CLOCK_MONOTONIC, &patch->start_ts);
            patch->start_mute = true;
            patch->read_size = 0;
            patch->mdelay = DEFAULT_PLAYBACK_PERIOD_SIZE * DEFAULT_PLAYBACK_PERIOD_CNT / (MM_FULL_POWER_SAMPLING_RATE / 1000);
        } else {
            if (patch->start_mute) {
                int flag = Stop_watch(patch->start_ts, patch->mdelay);
                if (!flag) {
                    patch->sync_offset = -1;
                    patch->start_mute = false;
                    set_output_device_mute(aml_dev, AUDIO_DEVICE_OUT_SPEAKER, false, true);;
                }
            } else {
                patch->read_size += size;
            }
        }
    }
}


int in_reset_config_param(struct aml_stream_in *in, AML_INPUT_STREAM_CONFIG_TYPE_E enType, const void *pValue)
{
    struct aml_audio_device *adev = in->dev;
    int s32Ret = 0;

    switch (enType) {
        case AML_INPUT_STREAM_CONFIG_TYPE_CHANNELS:
            in->config.channels = *(unsigned int *)pValue;
            ALOGD("%s:%d Config channel number to %d", __func__, __LINE__, in->config.channels);
            break;

        case AML_INPUT_STREAM_CONFIG_TYPE_PERIODS:
            in->config.period_size = *(unsigned int *)pValue;
            ALOGD("%s:%d Config Period size to %d", __func__, __LINE__, in->config.period_size);
            break;
        default:
            ALOGW("%s:%d not support input stream type:%#x", __func__, __LINE__, enType);
            return -1;
    }

    if (!in->standby) {
        AM_LOGD("reconfig PCM type %d", enType);
        do_input_standby(in);
        usleep(50*1000);
    }
    s32Ret = start_input_stream(in);
    in->standby = 0;
    if (s32Ret < 0) {
        ALOGW("start input stream failed! ret:%#x", s32Ret);
    }
    return s32Ret;
}

int reconfig_read_param_through_hdmiin(struct aml_audio_device *aml_dev,
                                       struct aml_stream_in *stream_in,
                                       ring_buffer_t *ringbuffer, int buffer_size)
{
    int ring_buffer_size = buffer_size;
    int last_channel_count = 2;
    int s32Ret = 0;
    bool is_channel_changed = false;
    bool is_audio_packet_changed = false;
    hdmiin_audio_packet_t last_audio_packet = AUDIO_PACKET_AUDS;
    int period_size = 0;
    int buf_size = 0;
    bool pic_mode_reconfig_in = false;

    if (!aml_dev || !stream_in) {
        ALOGE("%s line %d aml_dev %p stream_in %p\n", __func__, __LINE__, aml_dev, stream_in);
        return -1;
    }

    /* check game mode change and reconfig input */
    get_pic_mode_config(aml_dev, &pic_mode_reconfig_in, NULL, NULL);
    if (pic_mode_reconfig_in) {
        int play_buffer_size = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;

        if (is_game_mode(aml_dev)) {
            period_size = LOW_LATENCY_CAPTURE_PERIOD_SIZE;
            buf_size = 2 * 4 * LOW_LATENCY_PLAYBACK_PERIOD_SIZE;
        } else {
            period_size = DEFAULT_CAPTURE_PERIOD_SIZE;
            buf_size = 4 * 2 * 2 * play_buffer_size * PATCH_PERIOD_COUNT;
        }
        ALOGD("%s(), game pic mode %d, period size %d",
                __func__, is_game_mode(aml_dev), period_size);
        stream_in->config.period_size = period_size;
        if (!stream_in->standby) {
            do_input_standby(stream_in);
        }
        s32Ret = start_input_stream(stream_in);
        stream_in->standby = 0;
        if (s32Ret < 0) {
            ALOGE("[%s:%d] start input stream failed! ret:%#x", __func__, __LINE__, s32Ret);
        }

        if (ringbuffer) {
            ring_buffer_reset_size(ringbuffer, buf_size);
        }

        reconfig_dev_pic_mode_in(aml_dev, false);
    }

    return 0;
}

int stream_check_reconfig_param(struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct audio_board_config *bd_config = &adev->board_config;
    int period_size = 0;
    bool pic_mode_reconfig_out = false;

    get_pic_mode_config(adev, NULL, &pic_mode_reconfig_out, NULL);
    if (pic_mode_reconfig_out) {
        ALOGD("%s(), game mode reconfig out", __func__);
        if (ms12->dolby_ms12_enable && !is_bypass_dolbyms12(stream)) {
            get_hardware_config_parameters(&(adev->ms12_config),
                AUDIO_FORMAT_PCM_16_BIT,
                bd_config->default_alsa_ch,
                ms12->output_samplerate,
                out->is_tv_platform, continuous_mode(adev),
                is_game_mode(adev));

            reconfig_dev_pic_mode_ms12(adev, true);
        }
        alsa_out_reconfig_params(stream);
        reconfig_dev_pic_mode_out(adev, false);
    }
    return 0;
}

bool is_data_packet_change_to_HBR(struct aml_stream_in *stream_in)
{
    struct aml_audio_device *aml_dev = stream_in->dev;
    bool is_audio_packet_changed = false;
    hdmiin_audio_packet_t last_audio_packet = AUDIO_PACKET_AUDS;
    int period_size = 0;
    int buf_size = 0;

    if (!aml_dev || !stream_in) {
        ALOGE("%s line %d aml_dev %p stream_in %p\n", __func__, __LINE__, aml_dev, stream_in);
        return -1;
    }

    last_audio_packet = stream_in->tv_param.audio_packet_type;
    hdmiin_audio_packet_t cur_audio_packet = get_hdmiin_audio_packet(&aml_dev->alsa_mixer);
    is_audio_packet_changed = (((cur_audio_packet == AUDIO_PACKET_AUDS) || (cur_audio_packet == AUDIO_PACKET_HBR)) &&
                               (last_audio_packet != cur_audio_packet));

    stream_in->tv_param.cur_audio_packet_type = cur_audio_packet;

    return ((cur_audio_packet == AUDIO_PACKET_HBR) && is_audio_packet_changed);
}

/*==================================mixer control commands=========================================*/

int set_resample_source(struct aml_mixer_handle *mixer_handle, enum ResampleSource source)
{
    return aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_HW_RESAMPLE_SOURCE, source);
}

int set_spdifin_pao(struct aml_mixer_handle *mixer_handle,int enable)
{
    return aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_SPDIFIN_PAO, enable);
}

int get_spdifin_samplerate(struct aml_mixer_handle *mixer_handle)
{
    int index = aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_SPDIF_IN_SAMPLERATE);

    return index;
}

static inline eMixerHwResample _sr_enum(int sr)
{
    switch (sr) {
        case 32000:
            return HW_RESAMPLE_32K;
        case 44100:
            return HW_RESAMPLE_44K;
        case 48000:
            return HW_RESAMPLE_48K;
        case 88200:
            return HW_RESAMPLE_88K;
        case 96000:
            return HW_RESAMPLE_96K;
        case 176400:
            return HW_RESAMPLE_176K;
        case 192000:
            return HW_RESAMPLE_192K;
        default:
            return HW_RESAMPLE_DISABLE;
    }
}

eMixerHwResample get_eArcIn_samplerate(struct aml_mixer_handle *mixer_handle)
{
    int sr = aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_EARCRX_AUDIO_SAMPLERATE);

    /* eARC mix control does not return sample rate as enum type as HDMIIN and SPDIF */
    return _sr_enum(sr);
}

int get_hdmiin_samplerate(struct aml_mixer_handle *mixer_handle)
{
    int stable = 0;

    stable = aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HDMI_IN_AUDIO_STABLE);
    if (!stable) {
        return -1;
    }

    return aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HDMI_IN_SAMPLERATE);
}

int get_hdmiin_channel(struct aml_mixer_handle *mixer_handle)
{
    int stable = 0;
    int channel_index = 0;

    stable = aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HDMI_IN_AUDIO_STABLE);
    if (!stable) {
        return -1;
    }

    /*hdmirx audio support: N/A, 2, 3, 4, 5, 6, 7, 8*/
    channel_index = aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HDMI_IN_CHANNELS);
    if (channel_index == 0 || channel_index == 1) {
        return 2;
    } else {
        return 8;
    }
}

hdmiin_audio_packet_t get_hdmiin_audio_packet(struct aml_mixer_handle *mixer_handle)
{
    int audio_packet = 0;
    audio_packet = aml_mixer_ctrl_get_int(mixer_handle,AML_MIXER_ID_HDMIIN_AUDIO_PACKET);
    if (audio_packet < 0) {
        return AUDIO_PACKET_NONE;
    }
    return (hdmiin_audio_packet_t)audio_packet;
}

int get_arcin_channel(struct aml_mixer_handle *mixer_handle)
{
    if (aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_EARCRX_ATTENDED_TYPE) == ATTEND_TYPE_EARC) {
        int type = eArcIn_coding_type_detection(mixer_handle);

        if (type == AUDIO_CODING_TYPE_MULTICH_8CH_LPCM)
            return 8;
        else if (type == NOT_READY)
            return NOT_READY;
    }

    return 2;
}

/* return audio channel assignment, see CEA-861-D Table 20 */
int get_arcin_ca(struct aml_mixer_handle *mixer_handle)
{
    if (aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_EARCRX_ATTENDED_TYPE) == ATTEND_TYPE_EARC) {
        return aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_EARCRX_CA);
    }

    return 0;
}

/* get channel mask from HDMI IN, the mask is only used to count right channel numbers from mask */
audio_channel_mask_t aml_map_ch_to_mask(int ch)
{
    switch (ch) {
        case 2:
            return AUDIO_CHANNEL_IN_STEREO;
        case 3:
            return AUDIO_CHANNEL_IN_STEREO | AUDIO_CHANNEL_IN_LOW_FREQUENCY;
        case 4:
            return AUDIO_CHANNEL_IN_STEREO | AUDIO_CHANNEL_IN_BACK_LEFT | AUDIO_CHANNEL_IN_BACK_RIGHT;
        case 5:
            return AUDIO_CHANNEL_IN_STEREO | AUDIO_CHANNEL_IN_CENTER | AUDIO_CHANNEL_IN_BACK_LEFT | AUDIO_CHANNEL_IN_BACK_RIGHT;
        case 6:
            return AUDIO_CHANNEL_IN_STEREO | AUDIO_CHANNEL_IN_CENTER | AUDIO_CHANNEL_IN_LOW_FREQUENCY | AUDIO_CHANNEL_IN_BACK_LEFT | AUDIO_CHANNEL_IN_BACK_RIGHT;
        case 7:
            return AUDIO_CHANNEL_IN_STEREO| AUDIO_CHANNEL_IN_CENTER | AUDIO_CHANNEL_IN_LOW_FREQUENCY | AUDIO_CHANNEL_IN_BACK_LEFT | AUDIO_CHANNEL_IN_BACK_RIGHT | AUDIO_CHANNEL_IN_BACK;
        case 8:
            return AUDIO_CHANNEL_IN_STEREO| AUDIO_CHANNEL_IN_CENTER | AUDIO_CHANNEL_IN_LOW_FREQUENCY | AUDIO_CHANNEL_IN_BACK_LEFT | AUDIO_CHANNEL_IN_BACK_RIGHT | AUDIO_CHANNEL_IN_LEFT_PROCESSED | AUDIO_CHANNEL_IN_RIGHT_PROCESSED;
        default:
            return AUDIO_CHANNEL_IN_STEREO;
    }
}

/* get channel mask from ARC/eARC IN */
audio_channel_mask_t aml_map_ca_to_mask(int ca)
{
    audio_channel_mask_t mask = 0;
    const audio_channel_mask_t mask_tab[] = {
        AUDIO_CHANNEL_IN_STEREO,
        AUDIO_CHANNEL_IN_STEREO | AUDIO_CHANNEL_IN_BACK,
        AUDIO_CHANNEL_IN_STEREO | AUDIO_CHANNEL_IN_BACK_LEFT | AUDIO_CHANNEL_IN_BACK_RIGHT,
        AUDIO_CHANNEL_IN_STEREO | AUDIO_CHANNEL_IN_BACK_LEFT | AUDIO_CHANNEL_IN_BACK_RIGHT | AUDIO_CHANNEL_IN_BACK,
        AUDIO_CHANNEL_IN_STEREO | AUDIO_CHANNEL_IN_BACK_LEFT | AUDIO_CHANNEL_IN_BACK_RIGHT | AUDIO_CHANNEL_IN_LEFT_PROCESSED | AUDIO_CHANNEL_IN_RIGHT_PROCESSED,
        AUDIO_CHANNEL_IN_STEREO,
        AUDIO_CHANNEL_IN_STEREO | AUDIO_CHANNEL_IN_BACK,
        AUDIO_CHANNEL_IN_STEREO | AUDIO_CHANNEL_IN_BACK_LEFT,
    };

    if ((ca >= 0) || (ca <= 0x1f)) {
        mask = AUDIO_CHANNEL_IN_STEREO;
        if (ca & 1)
            mask |= AUDIO_CHANNEL_IN_LOW_FREQUENCY;
        if ((ca >> 1) & 1)
            mask |= AUDIO_CHANNEL_IN_CENTER;
        mask |=  mask_tab[ca/4];
        return mask;
    }

    return 0;
}

int set_hdmiin_audio_mode(struct aml_mixer_handle *mixer_handle, char *mode)
{
    if (mode == NULL || strlen(mode) > 5)
        return -EINVAL;

    return aml_mixer_ctrl_set_str(mixer_handle,
            AML_MIXER_ID_HDMIIN_AUDIO_MODE, mode);
}

enum hdmiin_audio_mode get_hdmiin_audio_mode(struct aml_mixer_handle *mixer_handle)
{
    return (enum hdmiin_audio_mode)aml_mixer_ctrl_get_int(mixer_handle,
            AML_MIXER_ID_HDMIIN_AUDIO_MODE);
}

int get_HW_resample(struct aml_mixer_handle *mixer_handle)
{
    return aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HW_RESAMPLE_ENABLE);
}

int enable_HW_resample(struct aml_mixer_handle *mixer_handle, int enable_sr)
{
    if (enable_sr == 0)
        aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_HW_RESAMPLE_ENABLE, HW_RESAMPLE_DISABLE);
    else
        aml_mixer_ctrl_set_int(mixer_handle, AML_MIXER_ID_HW_RESAMPLE_ENABLE, enable_sr);
    return 0;
}


bool is_hdmi_in_stable_hw(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    int stable = 0;

    stable = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_HDMI_IN_AUDIO_STABLE);
    if (!stable) {
        ALOGV("%s() amixer %s get %d\n", __func__, "HDMIIN audio stable", stable);
        return false;
    }
    return true;
}

bool is_hdmi_in_stable_sw (struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(aml_dev);
    audio_format_t fmt;

    /* now, only hdmiin->(spk, hp, arc) cases init the soft parser thread
     * TODO: init hdmiin->mix soft parser too
     */
    if (!patch)
        return true;

    fmt = audio_parse_get_audio_type (patch->audio_parse_para);
    if (fmt != in->spdif_fmt_sw) {
        ALOGD ("%s(), in type changed from %#x to %#x", __func__, in->spdif_fmt_sw, fmt);
        in->spdif_fmt_sw = fmt;
        return false;
    }

    return true;
}

bool is_atv_in_stable_hw (struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    int type = 0;
    int stable = 0;

    stable = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_ATV_IN_AUDIO_STABLE);
    if (!stable)
        return false;

    return true;
}

bool is_av_in_stable_hw(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *aml_dev = in->dev;
    int type = 0;
    int stable = 0;

    stable = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_AV_IN_AUDIO_STABLE);
    if (!stable)
        return false;

    return true;
}

static int eArcIn_audio_format_detection(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(aml_dev);
    int type = 0;
    int audio_code = 0;

    type = eArcIn_coding_type_detection(&aml_dev->alsa_mixer);
    if (type == EARC_AC3_LAYOUT_B || type >= EARC_EAC3_LAYOUT_B)
        patch->arc_layout_b = true;
    else
        patch->arc_layout_b = false;

    patch->earcin_audio_type = type;

    return earc_coding_type_to_codec(type);
}

bool is_spdif_in_stable_hw(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(aml_dev);
    int type = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_SPDIFIN_AUDIO_TYPE);

    if (type > PAUSE || type < LPCM) {
        AM_LOGV("%s(), in type is not ready yet", __func__);
        return true;
    }

    if (type != patch->param_config.spdif_fmt_hw) {
        ALOGI ("%s(), in type changed from %d to %d", __func__, patch->param_config.spdif_fmt_hw, type);
        patch->param_config.spdif_fmt_hw = type;
        return false;
    }

    return true;
}

bool is_earcrx_stable(struct aml_mixer_handle *alsa_mixer)
{
    return !!aml_mixer_ctrl_get_int(alsa_mixer, AML_MIXER_ID_EARCRX_STABLE);
}

bool is_earc_in_status_change(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(aml_dev);
    int stable = 0, type = 0;

    stable = is_earcrx_stable(&aml_dev->alsa_mixer);
    if (!stable) {
        ALOGV("%s() amixer %s get %d\n", __func__, "HDMIIN audio stable", stable);
        return false;
    }

    type = eArcIn_audio_format_detection(stream);
    if (type == NOT_READY) {
        AM_LOGV("%s(), in type is not ready yet", __func__);
        patch->arc_layout_b = patch->last_layout_b;
        return true;
    }
    if (type != patch->param_config.spdif_fmt_hw) {
        ALOGI ("%s(), in type changed from %d to %d", __func__, patch->param_config.spdif_fmt_hw, type);
        patch->param_config.spdif_fmt_hw = type;
        return false;
    }
    return true;
}

bool is_hdmi_in_sample_rate_changed(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(aml_dev);
    int last_hdmi_in_samplerate = patch->param_config.hdmi_in_samplerate;

    int samplerate = aml_mixer_ctrl_get_int(&aml_dev->alsa_mixer, AML_MIXER_ID_HDMI_IN_SAMPLERATE);
    if (last_hdmi_in_samplerate != samplerate) {
        ALOGD("hdmi in samplerate changes from %d to %d",last_hdmi_in_samplerate, samplerate);
        patch->param_config.hdmi_in_samplerate = samplerate;
        return true;
    }
    return false;
}

bool is_hdmi_in_hw_format_change(struct audio_stream_in *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *aml_dev = in->dev;
    struct aml_audio_patch *audio_patch = get_dev_patch(aml_dev);
    audio_type_parse_t *audio_type_status = (audio_type_parse_t *)audio_patch->audio_parse_para;
    int type = 0;
    bool ret = false;

    /* TL1 do not use HDMIIN_AUDIO_TYPE */
    if (audio_type_status != NULL && audio_type_status->soft_parser != 1) {
        type = aml_mixer_ctrl_get_int (&aml_dev->alsa_mixer, AML_MIXER_ID_HDMIIN_AUDIO_TYPE);
        if ((type != INVALID_TYPE) && (type != audio_patch->param_config.spdif_fmt_hw)) {
            ALOGD ("%s(), in type changed from %d to %d", __func__, audio_patch->param_config.spdif_fmt_hw, type);
            ret = true;
        }
        audio_patch->param_config.spdif_fmt_hw = type;
    }
    return ret;
}

bool is_earcrx_support_hw_multi_ch_resample(struct aml_mixer_handle *alsa_mixer)
{
    return aml_mixer_ctrl_get_int(alsa_mixer, AML_MIXER_ID_AML_CHIP_ID) >= 0x36;
}
