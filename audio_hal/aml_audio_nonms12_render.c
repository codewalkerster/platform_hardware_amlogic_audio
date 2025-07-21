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

#define LOG_TAG "audio_hw_hal_render"
//#define LOG_NDEBUG 0

#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <cutils/log.h>
#include <aml_volume_utils.h>
#include <aml_android_utils.h>
#include <inttypes.h>

#include "audio_hw.h"
#include "audio_hw_utils.h"
#ifdef ENABLE_DVB_PATCH
#include "dtv_patch.h"
#endif
#include "aml_dec_api.h"
#include "aml_ddp_dec_api.h"
#include "aml_audio_spdifout.h"
#include "alsa_config_parameters.h"
#include "aml_data_utils.h"
#include "aml_audio_ms12_sync.h"
#include "aml_audio_output.h"
#include "dolby_lib_api.h"
#include <cutils/properties.h>
#include <fcntl.h>
#include "audio_hw_resource_mgr.h"
#include "dtv_patch_hal_avsync.h"
#include "audio_hw_ms12.h"
#include "aml_stream_manager.h"
#include "audio_data_process.h"


static bool aml_nonms12_stream_speed_process(struct aml_stream_out *aml_out,
    void *in_buffer, int in_bytes,
    void **p_out_buffer, size_t *p_out_bytes,
    const audio_speed_config_t *speed_config);

static void aml_audio_stream_volume_process(struct audio_stream_out *stream, void *buf, int sample_size, int channels, int bytes) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    float volume[8],last_volume[8];
    volume[0]   = aml_out->volume_l;
    volume[1]   = aml_out->volume_r;

    /* If there is a dev->mix patch, the volume is handled in the in_read func. */
    if (aml_dev->dev2mix_patch) {
        volume[0] = volume[1] = 1.0;
    }
    /*
    Indeed,all the input source main need to be applied before the mixer
    need hdmi/av.. source gain here also.now only DTV available.
    */
    last_volume[0] = aml_out->last_volume_l;
    last_volume[1] = aml_out->last_volume_r;
    /*
    android only support max stereo stream volume configuration,we have to reuse left volume as
    C/LFE/Ls/Rs/Lrs/Rrs volume
    */
    if (channels > 2) {
        for (int ch = 2; ch < channels; ch ++) {
            last_volume[ch] = last_volume[0];
            volume[ch] = volume[0];
        }
    }
    apply_volume_fade(last_volume, volume, buf, sample_size, channels, bytes);
    aml_out->last_volume_l = aml_out->volume_l;
    aml_out->last_volume_r = aml_out->volume_r;
    return;
}

static inline bool check_sink_pcm_sr_cap(struct aml_audio_device *adev, int sample_rate)
{
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);

    switch (sample_rate) {
        case 32000: return !!(hdmi_desc->pcm_fmt.sample_rate_mask & (1<<0));
        case 44100: return !!(hdmi_desc->pcm_fmt.sample_rate_mask & (1<<1));
        case 48000: return !!(hdmi_desc->pcm_fmt.sample_rate_mask & (1<<2));
        case 88200: return !!(hdmi_desc->pcm_fmt.sample_rate_mask & (1<<3));
        case 96000: return !!(hdmi_desc->pcm_fmt.sample_rate_mask & (1<<4));
        case 176400: return !!(hdmi_desc->pcm_fmt.sample_rate_mask & (1<<5));
        case 192000: return !!(hdmi_desc->pcm_fmt.sample_rate_mask & (1<<6));
        default: return false;
    }

    return false;
}

ssize_t aml_audio_spdif_output(struct audio_stream_out *stream, void **spdifout_handle, dec_data_info_t * data_info)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    int ret = 0;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;

    if (data_info->data_len <= 0) {
        return -1;
    }

    //current stream not write spdif alsa, if there had active raw stream.
    ret = aml_check_spdif_write_policy(aml_out);
    if (ret != AML_WRITE_POLICY_APPROVAL) {
        AM_LOGV(" aml_out:%p  format:0x%x  writePolicy:%d", aml_out, aml_out->hal_internal_format, ret);
        return ret;
    }

    // non-dolby/ddp/dts case
    if (!is_float_equal(speed_info->speed, 1.0f)) {
        if (!audio_is_linear_pcm(data_info->data_format) || (is_dts_format(aml_out->hal_internal_format) && data_info->data_ch > 2)) {
            if (get_debug_value(AML_DEBUG_AUDIOHAL_DEBUG)) {
                AM_LOGI("speed %f enable, drop non-pcm/multi-ch-pcm output", speed_info->speed);
            }
            return ret;
        }
    }

    if (*spdifout_handle == NULL) {
        spdif_config_t spdif_config = { 0 };
        spdif_config.audio_format = data_info->data_format;
        spdif_config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
        spdif_config.mute = aml_out->offload_mute;
        spdif_config.data_ch = data_info->data_ch;
        if (spdif_config.data_ch == 0) {
            spdif_config.data_ch = 2;
        }
        if (spdif_config.audio_format == AUDIO_FORMAT_IEC61937) {
            spdif_config.sub_format = data_info->sub_format;
            if ((spdif_config.sub_format == AUDIO_FORMAT_MPEGH || spdif_config.sub_format == AUDIO_FORMAT_DTS_HD
                || spdif_config.sub_format == AUDIO_FORMAT_MAT || spdif_config.sub_format == AUDIO_FORMAT_DOLBY_TRUEHD)
                && spdif_config.data_ch == 8) {
                spdif_config.channel_mask = AUDIO_CHANNEL_OUT_7POINT1;
            }
            if (spdif_config.sub_format == AUDIO_FORMAT_IEC61937) {
                spdif_config.channel_mask = audio_channel_out_mask_from_count(data_info->data_ch);
            }
        } else if (audio_is_linear_pcm(spdif_config.audio_format)) {
            if (data_info->data_ch == 6) {
                spdif_config.channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
            } else if (data_info->data_ch == 8) {
                spdif_config.channel_mask = AUDIO_CHANNEL_OUT_7POINT1;
            }
        }
        spdif_config.is_dtscd = data_info->is_dtscd;
        spdif_config.rate = data_info->data_sr;
        ret = aml_audio_spdifout_open(spdifout_handle, &spdif_config);
        if (ret != 0) {
            return -1;
        }
        ALOGI("%s, aml_out->offload_mute=%d, spdifout_handle:%p, spdifout2_handle:%p\n",
                __FUNCTION__, aml_out->offload_mute, aml_out->spdifout_handle, aml_out->spdifout2_handle);
        if (true == aml_out->offload_mute && aml_out->spdifout_handle) {
            aml_audio_spdifout_mute(aml_out->spdifout_handle, aml_out->offload_mute);
        }
    }

    ALOGV("[%s:%d] format =0x%x length =%d", __func__, __LINE__, data_info->data_format, data_info->data_len);
    ret = aml_audio_spdifout_process(*spdifout_handle, data_info->buf, data_info->data_len);
    if (ret == AML_SPDIFOUT_PROCESS_ALSA_IS_NULL) {
        ALOGW("%s: close spdifout %p, then auto reopen it next call", __func__,*spdifout_handle);
        aml_audio_spdifout_close(*spdifout_handle);
        *spdifout_handle = NULL;
    }

    return ret;
}

int aml_audio_nonms12_render(struct audio_stream_out *stream, void *abuffer)
{
    aml_audio_buffer_t *inBuffer = (aml_audio_buffer_t *)abuffer;
    const void *buffer = inBuffer->pData;
    size_t bytes = inBuffer->size;

    int decoder_ret = -1,ret = -1;
    int dec_used_size = 0;
    int left_bytes = 0;
    int used_size = 0;
    bool try_again = false;
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    struct aml_native_postprocess *VX_postprocess = &adev->native_postprocess;
    struct aml_mixer_handle *mixer_handle = &(adev->alsa_mixer);
    audio_type_parse_t *audio_type_status = NULL;

    int return_bytes = bytes;
    int out_frames = 0;
    void *input_buffer = (void *)buffer;
    audio_data_info_t data_info = { 0 };
    bool dts_pcm_direct_output = false;
    int ret_size;

    aml_audio_buffer_info_t *pBuffer = aml_out->audio_buffer;
    aml_audio_buffer_t *outPcmBuffer = pBuffer->outBuffer[AUDIO_BUFFER_OUT_PCM];
    aml_audio_buffer_t *outRawBuffer = pBuffer->outBuffer[AUDIO_BUFFER_OUT_RAW];
    aml_audio_buffer_t *outConvertRawBuffer = pBuffer->outBuffer[AUDIO_BUFFER_OUT_CONVERT_RAW];
    buffer_data_format_t pcmDataFormat = {0};
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;
    bool useAudioMixer = false;
    bool dtv_stream_flag = is_dtv_stream_out(stream);
    bool speed_enable = false;
    float speed_select = 1.0f;

    if (aml_out->aml_dec == NULL) {
        config_output(stream, true);
    }

    aml_dec_t *aml_dec = aml_out->aml_dec;

    if (patch != NULL) {
        audio_type_status = (audio_type_parse_t *)patch->audio_parse_para;
    }

    if (aml_dec) {
        dec_data_info_t * dec_pcm_data = &aml_dec->dec_pcm_data;
        dec_data_info_t * dec_raw_data = &aml_dec->dec_raw_data;
        dec_data_info_t * raw_in_data  = &aml_dec->raw_in_data;
        left_bytes = bytes;

        do {
            ALOGV("%s() in raw len=%d", __func__, left_bytes);
            used_size = 0;
            pthread_mutex_lock(&aml_out->dec_MutexLock);
            decoder_ret = aml_decoder_process(aml_dec, (unsigned char *)buffer + dec_used_size, left_bytes, &used_size);
            pthread_mutex_unlock(&aml_out->dec_MutexLock);

            if (decoder_ret == AML_DEC_RETURN_TYPE_CACHE_DATA) {
                ALOGV("[%s:%d] cache the data to decode", __func__, __LINE__);
                break;
            } else if (decoder_ret < 0) {
                ALOGV("[%s:%d] aml_decoder_process error, ret:%d", __func__, __LINE__, decoder_ret);
            }
            if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
                if (dec_pcm_data->data_len)
                check_audio_level("dec pcm", dec_pcm_data->buf, dec_pcm_data->data_len);
            }

            pcmDataFormat.channelCount = dec_pcm_data->data_ch;
            pcmDataFormat.channelMask = audio_channel_out_mask_from_count(dec_pcm_data->data_ch);
            pcmDataFormat.format = AUDIO_FORMAT_PCM_16_BIT;
            pcmDataFormat.sampleRate = dec_pcm_data->data_sr;
            left_bytes -= used_size;
            dec_used_size += used_size;
            ALOGV("used_size %d total used size %d %s() left_bytes =%d pcm len =%d raw len=%d",
                used_size, dec_used_size, __func__, left_bytes, dec_pcm_data->data_len, dec_raw_data->data_len);

            if (aml_out->optical_format != adev->optical_format ||
                adev->sink_format_changed) {
                if (aml_out->optical_format != adev->optical_format) {
                    ALOGI("optical format change from 0x%x --> 0x%x", aml_out->optical_format, adev->optical_format);
                } else {
                    ALOGI("%s", __func__);
                }

                aml_out->optical_format = adev->optical_format;
                adev->sink_format_changed = false;

                if (aml_out->spdifout_handle != NULL) {
                    aml_audio_spdifout_close(aml_out->spdifout_handle);
                    aml_out->spdifout_handle = NULL;
                }
                if (aml_out->spdifout2_handle != NULL) {
                    aml_audio_spdifout_close(aml_out->spdifout2_handle);
                    aml_out->spdifout2_handle = NULL;
                }

            }

            if (adev->reset_hdmitx_audio) {
                ALOGI("%s reset hdmitx", __func__);
                adev->reset_hdmitx_audio = false;
                if (aml_out->spdifout_handle != NULL) {
                    aml_audio_spdifout_reset_hdmitx(aml_out->spdifout_handle);

                }
                if (aml_out->spdifout2_handle != NULL) {
                    aml_audio_spdifout_reset_hdmitx(aml_out->spdifout2_handle);
                }
            }

            if (dec_pcm_data->data_len > 0) {
                // aml_dump_audio_bitstreams("/data/vendor/audiohal/dec_data.raw", dec_pcm_data->buf, dec_pcm_data->data_len);
#ifdef ENABLE_DVB_PATCH
                if (dtv_stream_flag)
                    dtv_audio_sync_nonms12_pts_update (stream, dec_pcm_data, raw_in_data->data_len);
#endif
                //TODO: using decoder output buffer format as next processing format
                //Now, it's wrong so add some handle here
                audio_format_t output_format;
                if (audio_is_linear_pcm(aml_out->hal_internal_format)) {
                    output_format = aml_out->hal_internal_format;
                } else {
                    output_format = AUDIO_FORMAT_PCM_16_BIT;
                }
                void  *dec_data = (void *)dec_pcm_data->buf;
                int pcm_len = dec_pcm_data->data_len;

                if (patch) {
                    patch->sample_rate = dec_pcm_data->data_sr;
                }

                /* For dts certification:
                 * DTS 88.2K/96K pcm direct output case.
                 * If the PCM after decoding is 88.2k/96k, then direct output.
                 * Need to check whether HDMI sink supports 88.2k/96k or not.*/
                if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_PCM
                    && is_dts_format(aml_out->hal_internal_format)
                    && dec_pcm_data->data_sr > 48000
                    && check_sink_pcm_sr_cap(adev, dec_pcm_data->data_sr)) {

                    dts_pcm_direct_output = true;
                }

                int input_sr = dec_pcm_data->data_sr;
                int output_sr = OUTPUT_ALSA_SAMPLERATE;
                if (!is_dts_format(aml_out->hal_internal_format))
                    output_sr = nego_sample_rate(input_sr, dec_pcm_data->data_format,
                                                 adev->cur_out_devices);
                if (input_sr != output_sr) {
                    audio_resample_config_t cfg = {
                        .aformat = AUDIO_FORMAT_PCM_16_BIT, // TODO
                        .channels = dec_pcm_data->data_ch,
                        .input_sr = input_sr,
                        .output_sr = output_sr,
                    };
                    ret = aml_audio_resample_process_ex(&aml_out->resample_handle, &cfg, dec_data, pcm_len);
                    if (ret != 0) {
                        AM_LOGE("aml_audio_resample_process_ex fail ret=%d", ret);
                    } else {
                        dec_data = aml_out->resample_handle->resample_buffer;
                        pcm_len = aml_out->resample_handle->resample_size;
                    }
                }
                aml_out->config.rate = output_sr;
                if (!is_TV(adev)) {
                    aml_out->config.channels = dec_pcm_data->data_ch;
                }
                pcmDataFormat.sampleRate = output_sr;

                /*process the stream volume before mix*/
                aml_audio_stream_volume_process(stream, dec_data, sizeof(int16_t), dec_pcm_data->data_ch, pcm_len);

                if ((adev->effect_ctrl.effect_mode == EFFECT_MODE_DAP) || (adev->effect_ctrl.effect_mode == EFFECT_MODE_OFF)) {
                   //Do nothing
                } else {
                    if (dec_pcm_data->data_ch == 6 || dec_pcm_data->data_ch == 8) {
                        ret = audio_VX_post_process(VX_postprocess, (int16_t *)dec_data, pcm_len);
                        if (ret > 0) {
                            pcm_len = ret; /* VX will downmix 6ch/8ch to 2ch, pcm size will be changed */
                            dec_pcm_data->data_ch = 2;
                            pcmDataFormat.channelCount = 2;
                            pcmDataFormat.channelMask = audio_channel_out_mask_from_count(pcmDataFormat.channelCount);
                        }
                    }
                }

                if (dtv_stream_flag && aml_out->output_speed && !is_float_equal(aml_out->output_speed, 1.0f)) {
                    speed_enable = true;
                    speed_select = aml_out->output_speed;
                } else if (!is_float_equal(speed_info->speed, 1.0f) || speed_info->speed_handle) {
                    speed_enable = true;
                    speed_select = speed_info->speed;
                }
                if (speed_enable) {
                    audio_speed_config_t speed_config;
                    void *speed_out_buffer = NULL;
                    size_t speed_out_bytes = 0;

                    speed_config.aformat = output_format;
                    speed_config.speed = speed_select;
                    speed_config.input_sr = OUTPUT_ALSA_SAMPLERATE;
                    speed_config.channels = dec_pcm_data->data_ch;

                    aml_nonms12_stream_speed_process(aml_out, dec_data, pcm_len, &speed_out_buffer, &speed_out_bytes, &speed_config);
                    dec_data = speed_out_buffer;
                    pcm_len = speed_out_bytes;
                }

                if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
                    check_audio_level("render pcm", dec_data, pcm_len);
                }

                if (is_tv_stream_out(aml_out)) {
                    if (patch && patch->need_do_avsync) {
                         memset(dec_data, 0, pcm_len);
                    } else {
                        tv_do_ease(aml_out, dec_data, pcm_len);
                    }

                    /* if audio channel status changes to "NONAUDIO", software parser doesn't detect audio format change, mute audio */
                    if (is_same_patch_src(adev, SRC_HDMIIN) && audio_type_status != NULL &&
                            audio_type_status->soft_parser && patch && patch->IEC61937_format == false &&
                            aml_mixer_ctrl_get_int(mixer_handle, AML_MIXER_ID_HDMIIN_NONAUDIO) == 1) {
                        memset(dec_data, 0, pcm_len);
                    }

                    if (get_debug_value(AML_DUMP_AUDIOHAL_TV)) {
                        aml_dump_audio_bitstreams("/data/vendor/audiohal/tv_non12_before_mixer.raw",
                            dec_data, pcm_len);
                    }
                }

                if (adev->effect_ctrl.effect_mode == EFFECT_MODE_DAP && eDolbyMS12Lib == adev->dolby_lib_type_last) {
                    if (dec_pcm_data->data_ch == 2) {
                        /* amlogic simple dap(stereo pcm input) init and process */
                        int pp_ret = 0;
                        if (!adev->ms12.dap_only_enable) {
                            pp_ret = aml_dap_open(aml_out, AUDIO_FORMAT_PCM_16_BIT, AUDIO_CHANNEL_OUT_STEREO, OUTPUT_ALSA_SAMPLERATE);
                            if (pp_ret) {
                                ALOGE("%s line %d pp_ret error %d lxs debug!\n", __func__, __LINE__, pp_ret);
                            }
                        }
                        else {
                            size_t n_dap_used_bytes = 0;
                            pp_ret = aml_dap_process(stream, (char*)dec_data, pcm_len, &n_dap_used_bytes);
                            if (pp_ret) {
                                ALOGI("%s line %d pp_ret %d lxs debug!\n", __func__, __LINE__, pp_ret);
                            }
                        }
                    }
                } else {
                    if (adev->ms12.dap_only_enable)
                        aml_dap_close(&(adev->ms12));
                }

                //wrap this out pcm Buffer
                {
                    outPcmBuffer->pData = dec_data;
                    outPcmBuffer->size = pcm_len;
                    outPcmBuffer->apts = inBuffer->apts;
                    outPcmBuffer->isAptsValid = inBuffer->isAptsValid;
                    outPcmBuffer->bufFormat.channelCount = pcmDataFormat.channelCount;
                    outPcmBuffer->bufFormat.channelMask = pcmDataFormat.channelMask;
                    outPcmBuffer->bufFormat.format = pcmDataFormat.format;
                    outPcmBuffer->bufFormat.sampleRate = pcmDataFormat.sampleRate;
                    //AM_LOGI("aml_out:%p aml_out->inputPortID:%d channelCount:%d format:0x%x sampleRate:%d",
                    //    aml_out, aml_out->inputPortID, outPcmBuffer->bufFormat.channelCount, outPcmBuffer->bufFormat.format, outPcmBuffer->bufFormat.sampleRate);
                }
                if (aml_out->inputPortID == -1) {//need to init input port when stream first run here.
                    aml_out->audioCfg.channel_mask = outPcmBuffer->bufFormat.channelMask;//aml_out->hal_channel_mask;
                    aml_out->audioCfg.sample_rate = outPcmBuffer->bufFormat.sampleRate;
                    aml_out->audioCfg.format = outPcmBuffer->bufFormat.format;
                    //init input port
                    if (adev->useAudioMixer) {
                        init_mixer_input_port(adev->mixerData, &aml_out->audioCfg, aml_out->flags,
                            on_notify_cbk, aml_out, on_input_avail_cbk, aml_out, NULL, NULL, 1.0);
#ifdef ENABLE_DVB_PATCH
                        if (dtv_stream_flag) {
                            int start_threshold = 3 * MIXER_FRAME_COUNT * audio_bytes_per_sample(output_format) * pcmDataFormat.channelCount;//24ms
                            mixer_set_inport_start_threshold(adev->mixerData, aml_out->inputPortID, start_threshold );
                        }
#endif
                        AM_LOGI("direct port:%s", mixerInputType2Str(get_input_port_type(&aml_out->audioCfg, aml_out->flags)));
                    }
                    if (!adev->useAudioMixer && is_dts_format(aml_out->hal_internal_format) && eDolbyMS12Lib != adev->dolby_lib_type_last) {
                        adev->useAudioMixer = true;
                        ret = initHalSubMixing(MIXER_LPCM, adev, is_TV(adev));
                        adev->raw_to_pcm_flag = false;
                        init_mixer_input_port(adev->mixerData, &aml_out->audioCfg, aml_out->flags,
                            on_notify_cbk, aml_out, on_input_avail_cbk, aml_out, NULL, NULL, 1.0);
                        AM_LOGI("direct port:%s", mixerInputType2Str(get_input_port_type(&aml_out->audioCfg, aml_out->flags)));
                    }
                }

                if (adev->ms12.dap_only_enable) {
                    ;//do nothing here.
                }
                else {
                    /* For MS12 lib with DTS output, no submixer exists */
                    if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
                        if (outPcmBuffer) {
                            //AM_LOGI("pData:%p size:%zu apts:%"PRIu64" ms", buffer, bytes, apts/90);
                            if (aml_out->hw_sync_mode && aml_out->hwsync && aml_out->hwsync->mediasync)
                                aml_do_hwsync_action(stream, (void *)outPcmBuffer);
                        }
                        struct aml_hw_mixer_buffer in_buf;
                        in_buf.data = dec_data;
                        in_buf.bytes = pcm_len;
                        in_buf.format = output_format;
                        struct aml_hw_mixer_buffer out_buf;
                        out_buf.data = NULL;
                        out_buf.bytes = 0;
                        //using primary output format as hw mixer output format
                        out_buf.format = get_primary_out_format(adev);
                        aml_hw_mixer_mixing_by_format(&adev->hw_mixer, &in_buf, &out_buf);
                        if (out_buf.bytes > 0) {
                            data_info.audio_format = out_buf.format;
                            data_info.channel_mask = audio_channel_out_mask_from_count(dec_pcm_data->data_ch);
                            ret = aml_audio_pcm_output((struct audio_stream_out *)aml_out, out_buf.data, out_buf.bytes, &data_info);
                        }
                    } else if (adev->useAudioMixer) {
                        if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
                            check_audio_level("after process", dec_data, pcm_len);
                        }
                        aml_out->hwsync_header_stripped = true;
                        useAudioMixer = true;
                        if (adev->dev2mix_patch) {
                            if (patch && (patch->need_do_avsync == true) && (patch->input_signal_stable == false) &&
                                ((adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) || (adev->out_device & AUDIO_DEVICE_OUT_ALL_USB)) &&
                                ((adev->in_device & AUDIO_DEVICE_IN_HDMI) || (adev->in_device & AUDIO_DEVICE_IN_LINE))){
                            } else {
                                tv_in_write(stream, dec_data, pcm_len);
                                memset((char *)dec_data, 0, pcm_len);
                            }
                        }

                       {

                            //config audioCfg with decoder output format of AudioBuffer
                            aml_out->audioCfg.format = outPcmBuffer->bufFormat.format;
                            aml_out->audioCfg.channel_mask = outPcmBuffer->bufFormat.channelMask;
                            aml_out->audioCfg.sample_rate = outPcmBuffer->bufFormat.sampleRate;
                            ret = out_write_pcm_to_AudioMixer(stream, dec_data, pcm_len, outPcmBuffer);
                            if (ret < 0) {//-22 is INVALID
                                AM_LOGI(" written to audioMxier error:%d", ret);
                                break;
                            }
                        }
                    } else { /*no submix */
                        //AM_LOGI("aml_hw_mixer -> hw_mix -> audio_output");
                        struct aml_hw_mixer_buffer in_buf;
                        in_buf.data = dec_data;
                        in_buf.bytes = pcm_len;
                        in_buf.format = output_format;
                        struct aml_hw_mixer_buffer out_buf;
                        out_buf.format = get_primary_out_format(adev);
                        aml_hw_mixer_mixing_by_format(&adev->hw_mixer, &in_buf, &out_buf);
                        if (out_buf.bytes > 0) {
                            data_info.audio_format = out_buf.format;
                            data_info.channel_mask = audio_channel_out_mask_from_count(dec_pcm_data->data_ch);
                            ret = aml_audio_pcm_output((struct audio_stream_out *)aml_out, out_buf.data, out_buf.bytes, &data_info);
                        }
                    }
                }
            }


            // write raw data
            /*for pcm case, we check whether it has multi channel pcm or 96k/88.2k pcm
            **multi channel pcm, no go through here, as multi ch pcm send to audioMixer with out_write_pcm_to_AudioMixer.
            **And AudioMixer has mc output/spdifout to handle this case.
            */
            if (!dts_pcm_direct_output && audio_is_linear_pcm(aml_dec->format) && raw_in_data->data_ch > 2 && (adev->dolby_lib_type_last == eDolbyMS12Lib)) {
                aml_audio_stream_volume_process(stream, raw_in_data->buf, sizeof(int16_t), raw_in_data->data_ch, raw_in_data->data_len);
                aml_audio_spdif_output(stream, &aml_out->spdifout_handle, raw_in_data);
            } else if (dts_pcm_direct_output) {
                aml_audio_stream_volume_process(stream, dec_pcm_data->buf, sizeof(int16_t), dec_pcm_data->data_ch, dec_pcm_data->data_len);
                aml_audio_spdif_output(stream, &aml_out->spdifout_handle, dec_pcm_data);
            }

            //wrap this out Raw Buffer
            {
                outRawBuffer->pData = raw_in_data->buf;
                outRawBuffer->size = raw_in_data->data_len;
                outRawBuffer->apts = inBuffer->apts;
                outRawBuffer->isAptsValid = inBuffer->isAptsValid;
                outRawBuffer->bufFormat.channelCount = raw_in_data->data_ch;
                outRawBuffer->bufFormat.channelMask = audio_channel_out_mask_from_count(raw_in_data->data_ch);
                outRawBuffer->bufFormat.format = raw_in_data->data_format;
                outRawBuffer->bufFormat.sampleRate = raw_in_data->data_sr;
            }
            if (!dts_pcm_direct_output && aml_out->optical_format != AUDIO_FORMAT_PCM_16_BIT) {
                if (aml_dec->format == AUDIO_FORMAT_E_AC3 || aml_dec->format == AUDIO_FORMAT_AC3) {
                    if (adev->dual_spdif_support) {
                        /*output raw ddp to hdmi*/
                        if (aml_dec->format == AUDIO_FORMAT_E_AC3 && aml_out->optical_format == AUDIO_FORMAT_E_AC3) {
                            if (raw_in_data->data_len)
                                aml_audio_spdif_output(stream, &aml_out->spdifout_handle, raw_in_data);
                        }

                        /*output dd data to spdif*/
                        if (dec_raw_data->data_len > 0)
                            aml_audio_spdif_output(stream, &aml_out->spdifout2_handle, dec_raw_data);
                    } else {
                        if (aml_dec->format == AUDIO_FORMAT_E_AC3 && aml_out->optical_format == AUDIO_FORMAT_AC3) {
                            /* DDP transcode to DD data for spdif or DD support only output */
                            if (dec_raw_data->data_len)
                                aml_audio_spdif_output(stream, &aml_out->spdifout_handle, dec_raw_data);
                        } else {
                            if (raw_in_data->data_len)
                                aml_audio_spdif_output(stream, &aml_out->spdifout_handle, raw_in_data);
                        }
                    }
                } else {
                    aml_audio_spdif_output(stream, &aml_out->spdifout_handle, dec_raw_data);
                }
            } else if (is_dts_format(aml_out->hal_internal_format) && (dec_raw_data->data_format == AUDIO_FORMAT_PCM_16_BIT ||
                        dec_raw_data->data_format ==AUDIO_FORMAT_PCM_32_BIT)) {
                aml_audio_stream_volume_process(stream, dec_raw_data->buf, audio_bytes_per_sample(dec_raw_data->data_format), dec_raw_data->data_ch, dec_raw_data->data_len);
                aml_audio_spdif_output(stream, &aml_out->spdifout_handle, dec_raw_data);
            } else if (aml_out->hal_format == AUDIO_FORMAT_IEC61937 && !aml_out->is_tv_src_stream) {
                aml_audio_spdif_output(stream, &aml_out->spdifout_handle, dec_raw_data);
            }
            if (useAudioMixer) {
                audio_mixer_post_sleep(aml_out);
            }

            /*special case  for dts , dts decoder need to follow aml_dec_api.h */
            if (is_dts_format(aml_out->hal_internal_format) && decoder_ret == AML_DEC_RETURN_TYPE_NEED_DEC_AGAIN ) {
                try_again = true;
            }

            /* DTS update audio format to display audio info banner.*/
            if (is_dts_format(aml_out->hal_internal_format))
                update_audio_format(adev, aml_out->hal_internal_format);

        } while ((left_bytes > 0) || aml_dec->fragment_left_size || try_again);
    }

    return return_bytes;
}

bool aml_decoder_output_compatible(struct audio_stream_out *stream, audio_format_t sink_format __unused, audio_format_t optical_format) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    bool is_compatible = true;

    if (aml_out->hal_internal_format != aml_out->aml_dec->format) {
        ALOGI("[%s:%d] not compatible. dec format:%#x -> cur format:%#x", __func__, __LINE__,
            aml_out->aml_dec->format, aml_out->hal_internal_format);
        return false;
    }

    if ((aml_out->aml_dec->format == AUDIO_FORMAT_AC3)
        || (aml_out->aml_dec->format == AUDIO_FORMAT_E_AC3)) {
        aml_dcv_config_t* dcv_config = &aml_out->dec_config.dcv_config;
        if (((optical_format == AUDIO_FORMAT_PCM_16_BIT) && (dcv_config->digital_raw > AML_DEC_CONTROL_DECODING))
            || ((optical_format == AUDIO_FORMAT_E_AC3) && (dcv_config->digital_raw != AML_DEC_CONTROL_RAW))
            || (optical_format == AUDIO_FORMAT_AC3 && dcv_config->decoding_mode != DDP_DECODE_MODE_SINGLE)) {
                is_compatible = false;
        }
    } else if (is_dts_format(aml_out->aml_dec->format)) {
        if (adev->dts_lib_type == eDTSXLib) {
            /* DTSX currently only supports one IEC61937 raw outputs.
             * According to the edid, @audio_format_t in Android audio only defines the difference between AUDIO_FORMAT_DTS
             * and AUDIO_FORMAT_DTS_HD, while DTS-HD EDID vsdb has four situations(0x0/0x1/0x3/0x7 see input\include\hdmirx_utils.h).
             * To simplify the process, the decoder is reinitialized every time.
             * */
            return false;
        } else {
            aml_dca_config_t* dca_config = &aml_out->dec_config.dca_config;
            if ((optical_format == AUDIO_FORMAT_PCM_16_BIT) && (dca_config->digital_raw > AML_DEC_CONTROL_DECODING)) {
                is_compatible = false;
            }
        }
    }

    return is_compatible;
}

int dca_get_out_ch_internal(void)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)aml_adev_get_handle();

    if (!adev)
        return -1;

    if (adev->dts_lib_type == eDTSXLib) {
        return dtsx_get_out_ch_internal(&adev->dts_x);
    } else {
        return dtshd_get_out_ch_internal();
    }
}

int dca_set_out_ch_internal(int ch_num)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)aml_adev_get_handle();

    if (!adev)
        return -1;

    if (adev->dts_lib_type == eDTSXLib) {
        return dtsx_set_out_ch_internal(&adev->dts_x, ch_num);
    } else {
        return dtshd_set_out_ch_internal(ch_num);
    }
}

static void ddp_decoder_config_prepare(struct audio_stream_out *stream, aml_dcv_config_t * ddp_config)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct aml_arc_hdmi_desc *p_hdmi_descs = get_arc_hdmi_cap(adev);
    aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)aml_out->audio_buffer;
    aml_audio_buffer_t *audioBuffer = pBuffer->inBuffer;

    adev->dcvlib_bypass_enable = 0;
    ddp_config->digital_raw = AML_DEC_CONTROL_CONVERT;
    ddp_config->decoding_mode = DDP_DECODE_MODE_SINGLE;

#ifdef ENABLE_DVB_PATCH
    aml_dtv_audiopara_t *dtv_audio_info = NULL;
    if (audioBuffer->isDtv) {
        dtv_audio_info = (aml_dtv_audiopara_t *)audioBuffer->privObject;
    }

    if (dtv_audio_info && dtv_audio_info->dual_decoder_support) {
        ddp_config->decoding_mode = DDP_DECODE_MODE_AD_DUAL;
    } else if (aml_out->ad_substream_supported) {
        ddp_config->decoding_mode = DDP_DECODE_MODE_AD_SUBSTREAM;
    } else {
        ddp_config->decoding_mode = DDP_DECODE_MODE_SINGLE;
    }
#endif
    /*passthrough raw output priority level higher than ad output*/
    if (adev->sink_format != AUDIO_FORMAT_PCM_16_BIT &&  adev->sink_format != AUDIO_FORMAT_PCM_32_BIT) {
        ddp_config->decoding_mode = DDP_DECODE_MODE_SINGLE;
    }

    if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) {
        ddp_config->nIsEc3 = 1;
    } else if (aml_out->hal_internal_format == AUDIO_FORMAT_AC3) {
        ddp_config->nIsEc3 = 0;
    }
    /*check if the input format is contained with 61937 format*/
    if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
        ddp_config->is_iec61937 = true;
    } else {
        ddp_config->is_iec61937 = false;
    }
    //aml parser use the individual iec/spdif parser,
    //so ddp decoder can't setup this flag.
    if (aml_out->aml_parser) {
        ddp_config->is_iec61937 = false;
    }

    ALOGI("%s digital_raw:%d, dual_output_flag:%d, is_61937:%d, IsEc3:%d decoding_mode %d"
        , __func__, ddp_config->digital_raw, aml_out->dual_output_flag, ddp_config->is_iec61937, ddp_config->nIsEc3,ddp_config->decoding_mode);
    return;
}

static void dts_decoder_config_prepare(struct audio_stream_out *stream, aml_dec_config_t *dec_config)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct aml_arc_hdmi_desc *p_hdmi_descs = get_arc_hdmi_cap(adev);

    adev->dtslib_bypass_enable = 0;

    if ( (adev->cur_out_devices == OUTPORT_HEADPHONE) || (adev->cur_out_devices == OUTPORT_A2DP) ||
         (adev->cur_out_devices == OUTPORT_HDMI_ARC) || (adev->effect_ctrl.effect_mode == EFFECT_MODE_DAP) ||
         (adev->native_postprocess.vx_force_stereo == 1)) {
        if (adev->native_postprocess.libvx_exist) {
            ALOGD("%s(): set 2 ch", __func__);
            dca_set_out_ch_internal(2);
        }
    } else {
        if (adev->native_postprocess.libvx_exist) {
            ALOGD("%s(): set auto ch", __func__);
            dca_set_out_ch_internal(0);
        }
    }

    if (adev->dts_lib_type == eDTSXLib) {
        aml_dtsx_config_t *dtsx_config = &dec_config->dtsx_config;
        dtsx_config->digital_raw = AML_DEC_CONTROL_CONVERT;
        dtsx_config->is_dtscd = aml_out->is_dtscd;
        if ((aml_out->hal_format == AUDIO_FORMAT_IEC61937 || is_tv_stream_out(aml_out)) && !dtsx_config->is_dtscd) {
            dtsx_config->is_iec61937 = true;
        } else {
            dtsx_config->is_iec61937 = false;
        }
        //aml parser use the individual iec/spdif parser,
        //so dts decoder can't setup this flag.
        if (aml_out->aml_parser) {
            dtsx_config->is_iec61937 = false;
        }
        dtsx_config->dev = (void *)adev;

        if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS) {
            dtsx_config->passthroug_enable = 1;
        } else {
            dtsx_config->passthroug_enable = 0;
        }

        if ((adev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) != 0 || (adev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI) != 0) {
            dtsx_config->is_hdmi_output = 1;
        } else {
            dtsx_config->is_hdmi_output = 0;
        }

        if (p_hdmi_descs->dtshd_fmt.is_support) {
            dtsx_config->sink_dev_type = p_hdmi_descs->dtshd_fmt.dts_vsdb_byte3;
        } else {
            dtsx_config->sink_dev_type = 0; //CA(0),MA(1),P1(2),P2(4)
        }

        if (is_STB(adev)) {
            dtsx_config->device_type = STB;
            if (p_hdmi_descs->pcm_fmt.max_channels == 8 && adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_PCM) {
                ALOGI("%s sink support multi-ch pcm, and dtsx decoder bus0 output multi-ch pcm when stream channel != 2", __func__);
                dtsx_config->sink_support_multich_pcm = true;
            }
        }
        else
            dtsx_config->device_type = TV;

        ALOGI("[%s:%d] digital_raw:%d, dual_output_flag:%d, is_iec61937:%d, is_dtscd:%d, passthroug:%d, is_hdmi_output:%d, sink_dev_type:%d, sink_support_multich_pcm:%d", __func__, __LINE__,
            dtsx_config->digital_raw, aml_out->dual_output_flag, dtsx_config->is_iec61937,
            dtsx_config->is_dtscd, dtsx_config->passthroug_enable, dtsx_config->is_hdmi_output,
            dtsx_config->sink_dev_type, dtsx_config->sink_support_multich_pcm);
    } else if (adev->dts_lib_type == eDTSHDLib) {
        aml_dca_config_t * dts_config = &dec_config->dca_config;
        dts_config->digital_raw = AML_DEC_CONTROL_CONVERT;
        dts_config->is_dtscd = aml_out->is_dtscd;
        if ((aml_out->hal_format == AUDIO_FORMAT_IEC61937 || is_tv_stream_out(aml_out)) && !dts_config->is_dtscd) {
            dts_config->is_iec61937 = true;
        } else {
            dts_config->is_iec61937 = false;
        }
        //aml parser use the individual iec/spdif parser,
        //so dts decoder can't setup this flag.
        if (aml_out->aml_parser) {
            dts_config->is_iec61937 = false;
        }

        dts_config->dev = (void *)adev;
        ALOGI("%s digital_raw:%d, dual_output_flag:%d, is_iec61937:%d, is_dtscd:%d"
            , __func__, dts_config->digital_raw, aml_out->dual_output_flag, dts_config->is_iec61937, dts_config->is_dtscd);
    } else {
        ALOGE("[%s:%d] Without any dts library", __func__, __LINE__);
    }

    return;
}

static void mad_decoder_config_prepare(struct audio_stream_out *stream, aml_mad_config_t * mad_config){
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    mad_config->channel    = aml_out->hal_ch;
    mad_config->samplerate = aml_out->hal_rate;
    mad_config->mpeg_format = aml_out->hal_format;
    return;
}

static void faad_decoder_config_prepare(struct audio_stream_out *stream, aml_faad_config_t * faad_config){
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;

    faad_config->channel    = aml_out->hal_ch;
    faad_config->samplerate = aml_out->hal_rate;
    faad_config->aac_format = aml_out->hal_format;

    return;
}

static void iec_decoder_config_prepare(struct audio_stream_out *stream, aml_iec_config_t * iec_config){
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;

    if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
        iec_config->is_iec61937 = true;
    } else {
        iec_config->is_iec61937 = false;
    }
    if (aml_out->hal_internal_format == AUDIO_FORMAT_DTS_HD) {
        iec_config->is_dtshd = true;
    }
    iec_config->channel = aml_out->hal_ch;
    iec_config->samplerate = aml_out->hal_rate;
    iec_config->format = aml_out->hal_internal_format;

    return;
}

static void mpegh_decoder_config_prepare(struct audio_stream_out *stream, aml_mpegh_config_t * mpegh_config){
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;

    if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
        mpegh_config->is_iec61937 = true;
    } else {
        mpegh_config->is_iec61937 = false;
    }
    mpegh_config->channel = aml_out->hal_ch;
    mpegh_config->samplerate = aml_out->hal_rate;
    mpegh_config->format = aml_out->hal_internal_format;

    return;
}

static void pcm_decoder_config_prepare(struct audio_stream_out *stream, aml_pcm_config_t * pcm_config)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct aml_arc_hdmi_desc * hdmi_descs = get_arc_hdmi_cap(adev);

    pcm_config->input_channel = aml_out->hal_ch;
    pcm_config->samplerate = aml_out->hal_rate;
    pcm_config->pcm_format = aml_out->hal_format;
    pcm_config->max_out_channels = hdmi_descs->pcm_fmt.max_channels;
    pcm_config->output_channel = 0;
    if (ATTEND_TYPE_EARC  == aml_audio_earctx_get_type(adev)) {
        pcm_config->max_out_channels = 8;
    }
    if (adev->dolby_lib_type_last == eDolbyMS12Lib && adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS ) {
        pcm_config->output_channel = 2;
    }
    ALOGV("%s max_out_channels:%d, hdmi_descs max_channels:%d, pcm_config->output_channel:%d", __func__,
            pcm_config->max_out_channels, hdmi_descs->pcm_fmt.max_channels, pcm_config->output_channel);

    return;
}

int aml_decoder_config_prepare(struct audio_stream_out *stream, audio_format_t format, aml_dec_config_t * dec_config)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct audio_board_config *bd_config = &adev->board_config;
    aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)aml_out->audio_buffer;
    aml_audio_buffer_t *audioBuffer = pBuffer->inBuffer;

#ifdef ENABLE_DVB_PATCH
    aml_dtv_audiopara_t *dtv_audio_info = (aml_dtv_audiopara_t *)audioBuffer->privObject;
    if (dtv_audio_info && audioBuffer->isDtv) {
        dec_config->ad_decoder_supported = dtv_audio_info->dual_decoder_support;
        dec_config->ad_mixing_enable = dtv_audio_info->associate_audio_mixing_enable;
        dec_config->mixer_level = dtv_audio_info->mixing_level;
        dec_config->advol_level = dtv_audio_info->advol_level;
        ALOGI("mixer_level %d associate_audio_mixing_enable %d advol_level %d",
            dtv_audio_info->mixing_level, dtv_audio_info->associate_audio_mixing_enable, dtv_audio_info->advol_level);
    }
#endif

    dec_config->dts_decode_enable = adev->dts_decode_enable;
    dec_config->dts_lib_type = adev->dts_lib_type;
    dec_config->dolby_lib_type = adev->dolby_lib_type;
    dec_config->output_format = get_primary_out_format(adev);

    switch ((uint32_t)format) {
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3: {
        ddp_decoder_config_prepare(stream, &dec_config->dcv_config);
        break;
    }
    /*coverity[unterminated_case]*/
    case AUDIO_FORMAT_DTS: {
        if (bd_config->DTS_output_ch)
            dca_set_out_ch_internal(bd_config->DTS_output_ch);

        dts_decoder_config_prepare(stream, dec_config);
    }
    case AUDIO_FORMAT_DTS_HD:
    case AUDIO_FORMAT_DTS_UHD_P2: {
        if (adev->dts_decode_enable && bd_config->DTS_output_ch)
            dca_set_out_ch_internal(bd_config->DTS_output_ch);

        if (adev->dts_lib_type != eDTSNull) {
            dts_decoder_config_prepare(stream, dec_config);
        } else {
            iec_decoder_config_prepare(stream, &dec_config->iec_config);
        }
        break;
    }
    case AUDIO_FORMAT_PCM_16_BIT:
    case AUDIO_FORMAT_PCM_FLOAT:
    case AUDIO_FORMAT_PCM_32_BIT:
    case AUDIO_FORMAT_PCM_8_BIT:
    case AUDIO_FORMAT_PCM_8_24_BIT: {
        pcm_decoder_config_prepare(stream, &dec_config->pcm_config);
        break;
    }
    case AUDIO_FORMAT_MP3:
    case AUDIO_FORMAT_MP2: {
        mad_decoder_config_prepare(stream, &dec_config->mad_config);
        break;
    }
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_AAC_LATM: {
        faad_decoder_config_prepare(stream, &dec_config->faad_config);
        break;
    }
    case AUDIO_FORMAT_DOLBY_TRUEHD:
    case AUDIO_FORMAT_MAT: {
        iec_decoder_config_prepare(stream, &dec_config->iec_config);
        break;
    }
    case AUDIO_FORMAT_MPEGH:
    case AUDIO_FORMAT_MPEGH_BL_L3:
    case AUDIO_FORMAT_MPEGH_BL_L4:
    case AUDIO_FORMAT_MPEGH_LC_L3:
    case AUDIO_FORMAT_MPEGH_LC_L4: {
        mpegh_decoder_config_prepare(stream, &dec_config->mpegh_config);
        break;
    }
    case AUDIO_FORMAT_IEC61937: {
        iec_decoder_config_prepare(stream, &dec_config->iec_config);
        break;
    }
    default:
        break;

    }

    return 0;
}

static inline bool check_average_gap_ms(int gap_ms)
{
    if (abs(gap_ms) > 50) {
        ALOGE("gap_ms %d, too large !", gap_ms);
        return false;
    } else if (abs(gap_ms) >= 8) {
        return true;
    }
    return false;
}

static void aml_nonms12_config_apts_gap_easing(struct aml_stream_out *aml_out, int average_gap_ms)
{
    int adjust_ms = 0;
    int easing_frames = 0;
    float easing_speed = 1.0f;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;
    aml_audio_speed_apts_gap_ease_t *gap_ease = &speed_info->apts_gap_ease;

    if (!check_average_gap_ms(average_gap_ms)) {
        return;
    }

    /*
     * These viewpoints are observed by our local test:
     *
     * 1. For classic speed (like 0.25, 0.50, 1.0, 1.25, 1.5, ...)
     *    sonic latency calculation(output/speed - input) is precise, generate data size also precise.
     *
     * 2. Casual speed's latency calculation may not be well as expected,
     *    so we pick up some special speed for netflix(0.95, 1.05), other scenarios use default 0.02
     *
    */

    // pcr_pts_gap = ((int)(apts64 - pcr)) / 90;
    if (is_float_equal(speed_info->speed, 0.95)) {
        if (average_gap_ms > 0) {
            easing_speed = 0.935;
        } else {
            easing_speed = 0.965;
        }
    } else if (is_float_equal(speed_info->speed, 1.05)) {
        if (average_gap_ms > 0) {
            easing_speed = 1.025;
        } else {
            easing_speed = 1.075;
        }
    } else {
        if (average_gap_ms > 0) {
            easing_speed = speed_info->speed - 0.02;
        } else {
            easing_speed = speed_info->speed + 0.02;
        }
    }
    adjust_ms = abs(average_gap_ms) - 4; // 4ms for system scheduler jitter
    if (adjust_ms <= 2) {  // 2ms is too small, do not handle it !
        return;
    }

    easing_frames = adjust_ms * 48 / fabs(speed_info->speed - easing_speed);
    easing_frames = easing_frames * speed_info->speed;  // base on input, speed 1.0f

    gap_ease->speed = easing_speed;
    gap_ease->target_frames = easing_frames;
    gap_ease->current_frames = 0;  // base on input, speed 1.0f
    gap_ease->start = false;
    AM_LOGI("average_gap_ms %d, adjust_ms %d, easing_speed %.3f, easing_frames %d", average_gap_ms, adjust_ms, easing_speed, easing_frames);
}

/*
 * When micro speed adjustment enable, split large data into small piece, finally feed it to sonic.
 *
 * If feed small data size to sonic, sonic internal cache frames may be smaller sometimes,
 * and more suitable to switch speed.
*/
static int aml_nonms12_speed_process_split_mode(
    struct aml_stream_out *aml_out, int frame_size,
    void *in_buffer, int in_bytes,
    void **p_out_buffer, size_t *p_out_bytes,
    const audio_speed_config_t *speed_config)
{
    int input_offset = 0;
    int expect_bytes = 128 * frame_size;
    void *data_buffer = 0;
    size_t data_bytes = 0;
    int avail_bytes = 0;
    int new_buffer_size = 0;
    bool timeout_flag = false;
    int debug_enable = get_debug_value(AML_DEBUG_AUDIOHAL_SPEED);
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;
    struct timespec *gap_start_ts = &speed_info->sync_apts_gap.start_ts;
    aml_audio_speed_apts_gap_ease_t *gap_ease = &speed_info->apts_gap_ease;
    audio_speed_config_t new_speed_config;

    if (!speed_info->split_mode || frame_size <= 0) {
        return -1;
    }

    int timeout_ms = 300;
    int past_time_ms = calc_time_interval_us(gap_start_ts, &aml_out->lasttimestamp)/1000;
    if ((gap_start_ts->tv_sec || gap_start_ts->tv_nsec ) && past_time_ms >= timeout_ms) {
        AM_LOGI("past_time_ms %d, timeout_ms %d, timeout !", past_time_ms, timeout_ms);
        timeout_flag = true;
    }
    memcpy(&new_speed_config, speed_config, sizeof(*speed_config));
    speed_info->local_buf_used_bytes = 0;

    while (input_offset < in_bytes) {
        bool gap_easing_done = false;
        int latency_frame = aml_audio_speed_get_latency_frames(speed_info->speed_handle);
        int handle_bytes = in_bytes - input_offset;
        uint8_t *input_ptr = (uint8_t *)in_buffer + input_offset;

        // If sonic internal cache frames large, may affect adjustment effect
        if ((latency_frame < 192) || (timeout_flag && handle_bytes <= expect_bytes)) {
            gap_ease->start = true;
        }
        if (handle_bytes > expect_bytes) {
            handle_bytes = expect_bytes;
        }

        if (gap_ease->start) {
            int64_t diff_frames = 0;

            gap_ease->current_frames += (handle_bytes/frame_size);
            diff_frames = gap_ease->current_frames - gap_ease->target_frames;
            if (llabs(diff_frames) <= 2048 && latency_frame < 192 && latency_frame > 0) {
                gap_easing_done = true;
            } else if (diff_frames > 2048) {
                gap_easing_done = true;
            }
            new_speed_config.speed = gap_ease->speed;
        }
        if (debug_enable) {
            ALOGI("%s : last_latency_frame %d, gap_ease (target_frames %" PRId64 ", current_frames %" PRId64 ", start %d, easing_done %d)",
                __func__, latency_frame, gap_ease->target_frames, gap_ease->current_frames, gap_ease->start, gap_easing_done);
        }

        if (gap_easing_done) {
            gap_ease->speed = speed_info->speed;
            gap_ease->target_frames = 0;
            gap_ease->current_frames = 0;
            gap_ease->start = false;
            speed_info->split_mode = false;
            new_speed_config.speed = speed_info->speed;
        }

        aml_audio_speed_process_wrapper(&speed_info->speed_handle, input_ptr, handle_bytes,\
                                        &data_buffer, &data_bytes, &new_speed_config);

        if (debug_enable) {
            ALOGI("%s : speed %.3f, in_frame %d, out_frame %zu, latency_frame %d", __func__, new_speed_config.speed,
                    handle_bytes/frame_size, data_bytes/frame_size, aml_audio_speed_get_latency_frames(speed_info->speed_handle));
        }
        input_offset += handle_bytes;

        if (data_bytes > 0) {
            avail_bytes = speed_info->local_buf_size - speed_info->local_buf_used_bytes;
            if (avail_bytes < data_bytes) {
                new_buffer_size = speed_info->local_buf_size + (data_bytes - avail_bytes);
                new_buffer_size += expect_bytes * 4;

                if (speed_info->local_buf_ptr == NULL) {
                    speed_info->local_buf_ptr = aml_audio_malloc(new_buffer_size);
                } else {
                    speed_info->local_buf_ptr = aml_audio_realloc(speed_info->local_buf_ptr, new_buffer_size);
                }
                if (speed_info->local_buf_ptr == NULL) {
                    ALOGE("%s : realloc speed local_buffer failed !", __func__);
                    *p_out_buffer = NULL;
                    *p_out_bytes = 0;
                    return -1;
                }
                ALOGD("%s realloc speed local_buf_size from %d to %d", __func__, speed_info->local_buf_size, new_buffer_size);
                speed_info->local_buf_size = new_buffer_size;
            }
            memcpy((uint8_t *)speed_info->local_buf_ptr + speed_info->local_buf_used_bytes, data_buffer, data_bytes);
            speed_info->local_buf_used_bytes += data_bytes;
        }
    }

    *p_out_buffer = speed_info->local_buf_ptr;
    *p_out_bytes = speed_info->local_buf_used_bytes;
    return 0;
}


static bool aml_nonms12_stream_speed_process(struct aml_stream_out *aml_out,
    void *in_buffer, int in_bytes,
    void **p_out_buffer, size_t *p_out_bytes,
    const audio_speed_config_t *speed_config)
{
    bool ret = false;
    int frame_size = 0;
    int average_gap_ms = 0;
    const int DETECT_TIME_MS = AML_AUDIO_SPEED_DETECT_GAP_TIME_MS;
    aml_stream_speed_info_t *speed_info = NULL;
    aml_audio_speed_apts_gap_ease_t *gap_ease = NULL;
    bool dtv_stream_flag = false;

    if (aml_out == NULL || speed_config == NULL || in_buffer == NULL || in_bytes <= 0
        || p_out_buffer == NULL || p_out_bytes == NULL) {
        AM_LOGE("invalid parameters !");
        return false;
    }
    speed_info = &aml_out->speed_info;
    gap_ease = &speed_info->apts_gap_ease;
    dtv_stream_flag = is_dtv_stream_out((struct audio_stream_out *)aml_out);
    frame_size = audio_bytes_per_frame(speed_config->channels, speed_config->aformat);

    // non-ms12 : aml_do_hwsync_action is behind of speed_process
    speed_info->last_latency_frame = aml_audio_speed_get_latency_frames(speed_info->speed_handle);

    if (!dtv_stream_flag && frame_size > 0) {
        if (gap_ease->target_frames > 0) {
            if (gap_ease->start) {
                // apts gap ease processing start, reset apts gap statistics
                aml_audio_speed_reset_apts_gap(&speed_info->sync_apts_gap, DETECT_TIME_MS);
            }
        } else {
            if (aml_audio_speed_get_apts_gap_average(&speed_info->sync_apts_gap, &aml_out->lasttimestamp, &average_gap_ms)) {
                if (check_average_gap_ms(average_gap_ms)) {
                    speed_info->split_mode = true;
                    aml_nonms12_config_apts_gap_easing(aml_out, average_gap_ms);
                } else {
                    speed_info->split_mode = false;
                    if (speed_info->local_buf_ptr) {
                        aml_audio_free(speed_info->local_buf_ptr);
                        speed_info->local_buf_ptr = NULL;
                        speed_info->local_buf_size = 0;
                        speed_info->local_buf_used_bytes = 0;
                    }
                }
                aml_audio_speed_reset_apts_gap(&speed_info->sync_apts_gap, DETECT_TIME_MS);
            }
        }
    }

    if (speed_info->split_mode) {
        ret = aml_nonms12_speed_process_split_mode(aml_out, frame_size, in_buffer, in_bytes, p_out_buffer, p_out_bytes, speed_config);
    } else {
        ret = aml_audio_speed_process_wrapper(&speed_info->speed_handle, in_buffer, in_bytes, p_out_buffer, p_out_bytes, speed_config);
    }

    if (get_debug_value(AML_DEBUG_AUDIOHAL_SPEED)) {
        frame_size = audio_bytes_per_frame(speed_config->channels, speed_config->aformat);
        ALOGI("%s : speed %.3f, in_frame %d, out_frame %zu, latency_frame %d", __func__, speed_config->speed,
            in_bytes/frame_size, *p_out_bytes/frame_size, speed_info->last_latency_frame);
    }

    if (ret != 0) {
        ALOGE("aml_audio_speed_process_wrapper failed");
        speed_info->last_latency_frame = 0;
        return false;
    }
    return true;
}

