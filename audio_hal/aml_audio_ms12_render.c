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

#include <inttypes.h>
#include "audio_hw.h"
#include "audio_hw_utils.h"
#ifdef ENABLE_DVB_PATCH
#include "dtv_patch.h"
#endif
#include "dolby_lib_api.h"
#include "aml_volume_utils.h"
#include "audio_hw_ms12.h"
#include "aml_audio_timer.h"
#include "alsa_config_parameters.h"
#include <aml_android_utils.h>
#include "audio_hw_ms12_common.h"
#include "aml_audio_ms12_sync.h"
#include "audio_hwsync_wrap.h"
#include "aml_audio_output.h"
#include "tv_private_object.h"
#include "audio_hw_resource_mgr.h"
#include <cutils/properties.h>
#include <fcntl.h>
#include "audio_data_process.h"
#define MS12_MAIN_WRITE_LOOP_THRESHOLD                  (2000)
#define AUDIO_IEC61937_FRAME_SIZE 4
#define MS12_TRUNK_SIZE                                 (1024)
#define SECOND_2_PTS (90000) // 1s = 90000 (pts)

int aml_audio_ms12_process_wrapper(struct audio_stream_out *stream, const void *write_buf, size_t write_bytes)

{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    int return_bytes = write_bytes;
    int ret = 0;
    int total_write = 0;
    void *buffer = (void *)write_buf;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    int write_retry =0;
    size_t used_size = 0;
    audio_data_info_t data_info = { 0 };
    int ms12_write_failed = 0;
    int consume_size = 0,ms12_threshold_size = 256;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    audio_format_t output_format = get_output_format (stream);
    bool is_dolby_truehd = (aml_out->hal_internal_format == AUDIO_FORMAT_DOLBY_TRUEHD);

    if (adev->debug_flag) {
        ALOGD("%s:%d hal_format:%#x, output_format:0x%x, sink_format:0x%x do_easing %d",
            __func__, __LINE__, aml_out->hal_format, output_format, adev->sink_format,ms12->do_easing);
    }

    if (is_tv_stream_out(aml_out)) {
        if (aml_out->is_tv_src_stream && (patch->need_do_avsync || !patch->input_signal_stable)) {
            if (!ms12->is_muted) {
                //set_ms12_main_audio_mute(ms12, true, 0);
            }
        } else {
            tv_do_ease(aml_out, buffer, write_bytes);
        }
    }

    if (is_bypass_dolbyms12(stream)) {
        if (adev->debug_flag) {
            ALOGI("%s passthrough dolbyms12, format %#x\n", __func__, aml_out->hal_format);
        }

        data_info.audio_format = aml_out->hal_internal_format;
        data_info.channel_mask = aml_out->hal_channel_mask;
        ret = aml_audio_pcm_output((struct audio_stream_out *)aml_out, write_buf, write_bytes, &data_info);

    } else {

        if (is_tv_stream_out(aml_out) || is_dolby_truehd) {
            /*when it is non continuous mode, we bypass data here*/
            dolby_ms12_bypass_process(stream, buffer, write_bytes);
            /* This code is the True Passthrough(MS12) method. */
            //if (is_ms12_passthrough(stream)) {
            //    ALOGD("%s only passthrough, do not goto dolbyms12!\n", __func__);
            //    return return_bytes;
            //}
        }
        /*begin to write, clear the total write*/
        total_write = 0;
re_write:
        if (adev->debug_flag) {
            ALOGI("%s dolby_ms12_main_process before write_bytes %zu!\n", __func__, write_bytes);
        }
#ifdef ENABLE_DVB_PATCH
        bool dtv_stream_flag = is_dtv_stream_out(stream);
        if (dtv_stream_flag && is_ms12_passthrough(stream)) {
            aml_dtvsync_t *aml_dtvsync = (aml_dtvsync_t *)aml_out->hwsync->mediasync;
            struct dtvsync_audio_policy *async_policy = NULL;
            // JIRA:SWPL-202326
            // Description:enter dtv under bypass, ddp stream always mute on ARC.
            // Solution:Because switch from AUTO to passthrough, DTV output to the AVR is normal.
            // So, when enter DTV under passthrough directly,
            // it should check is_ms12_main_decoder is true,
            // make sure that the ms12_dec handle is built and is_focus is true,
            // then dolby_ms12_bypass_process() can be triggered noramlly.
            if (aml_dtvsync != NULL && aml_out->is_ms12_main_decoder) {
                async_policy = &(aml_dtvsync->apolicy);
                if (async_policy->audiopolicy == DTVSYNC_AUDIO_DROP_PCM) {
                   ALOGI("%s %s", __func__, "DROP_PCM");
                   return return_bytes;
                }
            }
        }
#endif
        used_size = 0;
        ret = dolby_ms12_main_process(stream, (char*)write_buf + total_write, write_bytes, &used_size);
        if (ret == 0) {
            if (adev->debug_flag) {
                ALOGI("%s dolby_ms12_main_process return %d, return used_size %zu!\n", __FUNCTION__, ret, used_size);
            }
            if (used_size < write_bytes && write_retry < MS12_MAIN_WRITE_LOOP_THRESHOLD) {
                if (adev->debug_flag) {
                    ALOGI("%s dolby_ms12_main_process used  %zu,write total %zu,left %zu\n", __FUNCTION__, used_size, write_bytes, write_bytes - used_size);
                }
                total_write += used_size;
                write_bytes -= used_size;
                /*if ms12 doesn't consume any data, we need sleep*/
                if (used_size == 0) {
                    aml_audio_sleep(1000);
                }
                if (adev->debug_flag >= 2) {
                    ALOGI("%s sleep 1ms\n", __FUNCTION__);
                }
                write_retry++;
                if (adev->ms12.dolby_ms12_enable) {
                    goto re_write;
                }
            }
            if (write_retry >= MS12_MAIN_WRITE_LOOP_THRESHOLD) {
                ALOGE("%s main write retry time output,left %zu", __func__, write_bytes);
                //bytes -= write_bytes;
                ms12_write_failed = 1;
            }
        } else {
            ALOGE("%s dolby_ms12_main_process failed %d", __func__, ret);
        }
    }

    return return_bytes;

}

static void aml_audio_ms12_init_pts_param(struct dolby_ms12_desc *ms12, uint64_t first_pts)
{
    if (!ms12)
        return;
    ms12->first_in_frame_pts = first_pts;
    ms12->last_synced_frame_pts = -1;
    ms12->out_synced_frame_count = 0;
    ALOGI("first_in_frame_pts  %" PRIu64 " ms" , ms12->first_in_frame_pts / 90);
}

static int aml_audio_ms12_process(struct audio_stream_out *stream, const void *write_buf, size_t write_bytes) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    int return_bytes = write_bytes;
    bool need_separate_frame  = false;
    int ret = 0;

    /*
     * case 1 for local ddp 44.1khz, now the input size is too big,
     * the passthrough output will be blocked by the decoded pcm output,
     * so we need feed it with small trunk to fix this issue
     *
     *
     * case 2 SWPL-66107
     * for dtv passthrough case, sometimes it has big input size,
     * we need separate it to small trunk
     *
     * todo, we need add a parser for such case
     */
    if (!adev->continuous_audio_mode && !is_tv_stream_out(aml_out) && !audio_is_linear_pcm(aml_out->hal_format)) {
        need_separate_frame = true;
    } else if (is_dtv_stream_out(stream) &&
            (AML_DIGITAL_AUDIO_MODE_BYPASS == adev->digital_audio_mode)) {
        need_separate_frame = true;
    }
    if (need_separate_frame) {
        size_t left_bytes = write_bytes;
        size_t used_bytes = 0;
        int process_size = 0;
        /*
         * Reason:
         * After enable the amlogic_truehd encoded by the dolby mat encoder, passthrough the Dolby MS12 pipeline.
         * Found the process_bytes(MS12_TRUNK_SIZE 1024Bytes) can lead the alsa underrun.
         *
         * Solution:
         * After send all the truehd to ms12, sound is smooth. If dolby truehd occur underrun in passthrough mode,
         * please take care of the value of process_size(aml_audio_ms12_render: bytes).
         *
         * Issue:
         * SWPL-60957: passthrough TrueHD format in Movie player.
         */
        int process_bytes = (aml_out->hal_format == AUDIO_FORMAT_DOLBY_TRUEHD) ? (write_bytes / 2) : MS12_TRUNK_SIZE;
        while (1) {
            process_size = left_bytes > process_bytes ? process_bytes : left_bytes;
            ret = aml_audio_ms12_process_wrapper(stream, (char *)write_buf + used_bytes, process_size);
            if (ret <= 0) {
                break;
            }
            used_bytes += process_size;
            left_bytes -= process_size;
            if (left_bytes <= 0) {
                break;
            }
        }
    } else {
        aml_audio_ms12_process_wrapper(stream, write_buf, write_bytes);
    }
    return return_bytes;
}

int aml_audio_ms12_render(struct audio_stream_out *stream, void *abuffer)
{
#ifndef AUDIO_HAL_DISABLE_MS12
    aml_audio_buffer_t *inBuffer = (aml_audio_buffer_t *)abuffer;
    const void *buffer = inBuffer->pData;
    size_t bytes = inBuffer->size;

    int ret = -1;
    int dec_used_size = 0;
    int used_size = 0;
    int left_bytes = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    int return_bytes = bytes;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    int out_frames = 0;
    int ms12_delayms = 0;
    int alsa_latency = 0;
    int force_setting_delayms = 0;
    bool bypass_aml_dec = false;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    bool dtv_stream_flag = is_dtv_stream_out(stream);

    /*
     * define the bypass_aml_dec by audio format
     * 1. AC3/E-AC3/E-AC3_JOC/AC4/TrueHD/MAT
     * 2. multi-pcm(5.1 or 7.1 but not stereo)
     * 1+2, can go through ms12 processing
     */
    if (is_dolby_ms12_support_compression_format(aml_out->hal_internal_format)
        || is_multi_channel_48k_pcm(stream)) {
        bypass_aml_dec = true;
    }

    if (aml_out->hw_sync_mode) {
         audio_hwsync_t *hw_sync = aml_out->hwsync;
         uint64_t apts64 = 0;
         uint64_t  cur_pts = inBuffer->apts;
         bool amaster_mode = true;
         if (hw_sync->wait_video_done == false && hw_sync->use_mediasync) {
             apts64 = cur_pts & ULLONG_MAX;
             aml_out->is_waiting_video = true;
             aml_hwsync_wait_video_start(hw_sync);
             aml_hwsync_wait_video_drop(hw_sync, apts64);
             aml_out->is_waiting_video = false;
             hw_sync->wait_video_done = true;
             aml_out->trace_last_write_time_ms = 0;
         } else {
             aml_hwsync_wrap_is_amaster(hw_sync, &amaster_mode);
             if (!amaster_mode) {
                 aml_out->restore_vmaster = true;
                 aml_hwsync_wrap_set_amaster(hw_sync, true);
             }
         }
         aml_audio_hwsync_checkin_apts(aml_out->hwsync, aml_out->hwsync->payload_offset, cur_pts);
         aml_out->hwsync->payload_offset += bytes;
    }

    if (bypass_aml_dec) {
#ifdef ENABLE_DVB_PATCH
        if (dtv_stream_flag) {
           dtv_audio_sync_ms12_raw_check_in(stream, abuffer);
        }
#endif
        /* audio data/apts, then we send the audio data*/
        aml_audio_ms12_process(stream, buffer, bytes);
    } else {
        if (aml_out->aml_dec == NULL) {
            config_output(stream, true);
        }
        aml_dec_t *aml_dec = aml_out->aml_dec;

        if (aml_dec) {
            dec_data_info_t * dec_pcm_data = &aml_dec->dec_pcm_data;
            dec_data_info_t * dec_raw_data = &aml_dec->dec_raw_data;
            dec_data_info_t * raw_in_data  = &aml_dec->raw_in_data;
            left_bytes = bytes;
            do {
                if (adev->debug_flag)
                    ALOGD("left_bytes %d dec_used_size %d", left_bytes, dec_used_size);
                pthread_mutex_lock(&aml_out->dec_MutexLock);
                ret = aml_decoder_process(aml_dec, (unsigned char *)buffer + dec_used_size, left_bytes, &used_size);
                pthread_mutex_unlock(&aml_out->dec_MutexLock);

                if (ret < 0) {
                    ALOGV("aml_decoder_process error");
                    return return_bytes;
                }
                left_bytes -= used_size;
                dec_used_size += used_size;
                ALOGV("%s() ret =%d pcm len =%d raw len=%d", __func__, ret, dec_pcm_data->data_len, dec_raw_data->data_len);
                // write pcm data
                if (dec_pcm_data->data_ch != aml_out->hal_ch) {
                    ALOGI("[%s:%d] open stream channel != decoder config channel dec_pcm_data->data_ch %d aml_out->hal_ch=%d",__FUNCTION__,__LINE__,dec_pcm_data->data_ch,aml_out->hal_ch);
                    ms12->config_channel_mask = audio_channel_out_mask_from_count(dec_pcm_data->data_ch);
                    aml_out->hal_ch = dec_pcm_data->data_ch;
                    aml_out->hal_channel_mask = ms12->config_channel_mask;
                }
                if (dec_pcm_data->data_len > 0) {
                    void  *dec_data = (void *)dec_pcm_data->buf;

                    if (dec_pcm_data->data_sr > 0) {
                        aml_out->config.rate = dec_pcm_data->data_sr;
                        ms12->config_sample_rate = dec_pcm_data->data_sr;
                    }

                    int input_sr = dec_pcm_data->data_sr;
                    int output_sr = OUTPUT_ALSA_SAMPLERATE;
                    if (dec_pcm_data->data_sr != OUTPUT_ALSA_SAMPLERATE) {
                        audio_resample_config_t cfg = {
                            .aformat = AUDIO_FORMAT_PCM_16_BIT,
                            .channels = dec_pcm_data->data_ch,
                            .input_sr = input_sr,
                            .output_sr = output_sr,
                        };
                        ret = aml_audio_resample_process_ex(&aml_out->resample_handle, &cfg, dec_data, dec_pcm_data->data_len);
                        if (ret != 0) {
                            AM_LOGE("aml_audio_resample_process_ex fail ret=%d", ret);
                        } else {
                            dec_data = aml_out->resample_handle->resample_buffer;
                            dec_pcm_data->data_len = aml_out->resample_handle->resample_size;
                        }
                        ms12->config_sample_rate = OUTPUT_ALSA_SAMPLERATE;
                    }
#ifdef ENABLE_DVB_PATCH
                    if (dtv_stream_flag) {
                        dtv_audio_sync_nonms12_pts_update(stream, dec_pcm_data, raw_in_data->data_len);
                    }
#endif
                    /* audio data/apts, then we send the audio data*/

                    /* In DTV case for AAC/HEAAC/MPEG-L1~L3 format data, every pcm sample will do pre attenuation */
                    /* monitor the HDMI/SPDIF output with DVB */
                    // Ref_Level_997Hz_23dBFS_200_MP1L2_DVB_h264_25fps.trp
                    //         Audio Mode(NONE), PCM output, -23dB
                    //         Audio Mode(AUTO), DD  output, -31dB
                    // Loudness_Consistency_-23dB_ddp_DVB_h264_25fps.trp
                    //         Audio Mode(NONE), PCM output, -23dB
                    //         Audio Mode(AUTO), DD  output, -31dB
                    // The AUDIO_FORMAT_AAC/AUDIO_FORMAT_AAC_LATM/AUDIO_FORMAT_MP2/AUDIO_FORMAT_MP3 Data will be decoded by this flow.
                    bool is_mpeg_es_with_bitstream_out = (aml_out->hal_internal_format == AUDIO_FORMAT_MP2 || aml_out->hal_internal_format == AUDIO_FORMAT_MP3);
                    is_mpeg_es_with_bitstream_out = is_mpeg_es_with_bitstream_out && (adev->sink_format > AUDIO_FORMAT_PCM_16_BIT);
                    if (aml_out->hal_internal_format == AUDIO_FORMAT_AAC ||
                        aml_out->hal_internal_format == AUDIO_FORMAT_AAC_LATM ||
                        is_mpeg_es_with_bitstream_out) {
                        pcm_data_do_pre_attenuation(
                            dec_data
                            , dec_pcm_data->data_len
                            , adev->ms12.dolby_ms12_enable
                            , dtv_stream_flag
                            , (adev->ms12.stereo_drc.mode == DOLBY_DRC_RF_MODE)
                            , adev->ms12.system_sound_target
                            , audio_bytes_per_sample(dec_pcm_data->data_format) //decoded pcm's bps
                            );
                    }
                    aml_audio_ms12_process_wrapper(stream, dec_data, dec_pcm_data->data_len);
                }
            } while ((left_bytes > 0) || aml_dec->fragment_left_size);
        }
    }

    return return_bytes;

#else
    (void)(stream);
    (void)(buffer);
    return bytes;
#endif
}




