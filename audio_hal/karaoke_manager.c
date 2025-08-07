/*
 * Copyright (C) 2017 Amlogic Corporation.
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
#ifdef SUPPORT_KARAOKE

#define LOG_TAG "audio_hw_hal_kara_mngr"
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <audio_utils/channels.h>

#include "audio_data_process.h"
#include "audio_hw_utils.h"
#include "karaoke_manager.h"
#include "aml_volume_utils.h"
//#include "EffectReverb.h"
#include "aml_malloc_debug.h"
#include "audio_hw_ms12.h"
#include "audio_hw_ms12_common.h"
#include "aml_reverb.h"
#include "aml_config_data.h"
#include "amlAudioMixer.h" // for submix
#include "aml_android_utils.h"

#define USB_DEFAULT_PERIOD_SIZE      512
#define USB_DEFAULT_PERIOD_COUNT     4

#define LINEIN_DEFAULT_SAMPLE_RATE   48000
#define LINEIN_DEFAULT_CHANNEL       2
#define LINEIN_DEFAULT_FORMAT        PCM_FORMAT_S16_LE
#define LINEIN_DEFAULT_PERIOD_SIZE   1024
#define LINEIN_DEFAULT_PERIOD_COUNT  4

#define ALOOP_DEFAULT_PERIOD_SIZE   512
#define ALOOP_DEFAULT_PERIOD_COUNT  6

/* privide data for recorder by ring_buffer/echo_reference/ALOOP and HW loopback(hw provide) */
static int provide_data_for_recorder(struct kara_manager *kara,
                                    kara_record_type_t record_type,
                                    void* buffer,
                                    struct audioCfg config,
                                    size_t size)
{
    if (!kara || !buffer || 0 == size) {
        return -EINVAL;
    }

    /* Original mic data, recorder by ring buffer */
    if (KARA_RECORD_TYPE_MIC_ORIGINAL == record_type) {
        // write data only when record start
        if (kara->kara_mic_record) {
            ring_buffer_write(&kara->mic_buffer, (unsigned char *)buffer, size, UNCOVER_WRITE);
        }
        return size;
    }

    struct voice_in *in = &kara->in;
    if (!in) {
        return -EINVAL;
    }
    /* Mic data after volume process, recorder by echo_reference buffer */
    if (KARA_RECORD_TYPE_MIC_AFTER_VOLUME == record_type) {
        size_t original_size = size;
        size_t after_process_size = original_size; //size after process
        int channel_support = 2; //ench reference only support 2ch write currently
        void* ench_source_buffer = NULL;
        if (0 == config.channelCnt) {
            return -EINVAL;
        }
        if (kara->echo_reference == NULL) {
        // do nothing
        } else {
            //use conversion buffer for saving data after channel adjust
            if (channel_support != config.channelCnt) {
                after_process_size = (original_size * channel_support) / config.channelCnt;
                if (after_process_size > in->conversion_buffer_size) {
                    in->conversion_buffer_size = after_process_size;
                    in->conversion_buffer = aml_audio_realloc(in->conversion_buffer, in->conversion_buffer_size);
                    if (!in->conversion_buffer) {
                        AM_LOGE("conversion_buffer malloc is fail");
                        return -1;
                    }
                }
                /* adjust to channel supported by echo_reference */
                enum pcm_format format = aml_pcm_format_from_audio_format(config.format);
                unsigned int sample_size_in_bytes = pcm_format_to_bits(format) / 8;
                after_process_size = adjust_channels(buffer, config.channelCnt,
                                        in->conversion_buffer, channel_support,
                                        sample_size_in_bytes, original_size);
                ench_source_buffer = in->conversion_buffer;
            } else {
                ench_source_buffer = buffer;
            }
            /* save data to echo reference*/
            struct echo_reference_buffer b;
            b.raw = ench_source_buffer;
            b.frame_count = original_size / config.frame_size;
            clock_gettime(CLOCK_REALTIME, &b.time_stamp);
            b.delay_ns = 0;
            kara->echo_reference->write(kara->echo_reference, &b);
            if (get_debug_value(AML_DUMP_AUDIOHAL_OUT) || in->debug) {
                aml_dump_audio_bitstreams("/data/audio/kara_echo_reference.raw", ench_source_buffer, after_process_size);
            }
        }
        return size;
    }

    /* Mic data after reverb process */
    if (KARA_RECORD_TYPE_MIC_AFTER_REVERB == record_type) {
        // implement when need record data after reverb
    }

    /* Data after mix, recorder by ALOOP */
    if (KARA_RECORD_TYPE_MIC_AFTER_SW_MIX == record_type) {
        if (kara->karaoke_on && kara->loopback_handle) {
            pcm_write(kara->loopback_handle, (void *)buffer, size);
            if (get_debug_value(AML_DUMP_AUDIOHAL_OUT) || in->debug) {
                aml_dump_audio_bitstreams("/data/audio/kara_write_to_loopback.pcm", buffer, size);
            }
        }
        return size;
    }

    return size;
}

/* Kara input data manager */
static ssize_t mic_data_input(struct kara_manager *kara, size_t frames)
{
    if (!kara || 0 == frames) {
        return -EINVAL;
    }
    struct voice_in *in = &kara->in;
    if (!in) {
        return -EINVAL;
    }
    uint32_t in_framesize = in->cfg.frame_size;
    if (0 == in_framesize) {
        return -EINVAL;
    }
    int ret = -1;
    /* real read size/frames from alsa or third party */
    size_t num_read_buff_bytes = frames * in_framesize; // init read size
    size_t real_frames = frames; // init real_frames as sample rate is same

    /* read size need modify when in&mixout sample rate are different */
    if (kara->resample_handle && kara->resample_config.output_sr) {
        real_frames = (real_frames * kara->resample_config.input_sr) / kara->resample_config.output_sr;
        int remainder = (real_frames * kara->resample_config.input_sr) % kara->resample_config.output_sr;
        if (remainder) {
            real_frames += 1; // read more 1 frame because data may overrun
        }
        num_read_buff_bytes = real_frames * in_framesize;
    }

    /* kara->buf is for saving the original mic data */
    if (num_read_buff_bytes > kara->buf_len) {
        kara->buf = aml_audio_realloc(kara->buf, num_read_buff_bytes);
        if (!kara->buf) {
            AM_LOGE("kara->buf aml_audio_realloc is fail");
            return -1;
        }
        kara->buf_len = num_read_buff_bytes;
    }
    void *read_buff = kara->buf;
    if (!read_buff) {
        AM_LOGE("kara->buf is null");
        return -1;
    }
    /* Read mic data via different way */
    switch (kara->kara_input_type) {
        case KARA_INPUT_TYPE_USB_MIC:
            ret = proxy_read(&in->proxy, read_buff, num_read_buff_bytes);
            break;
        case KARA_INPUT_TYPE_LINEIN_MIC:
            if (!in->pcm_handle) {
                AM_LOGE("in->pcm_handle is null");
                return -1;
            }
            ret = pcm_read(in->pcm_handle, read_buff, num_read_buff_bytes);
            break;
        default:
            //add other read function here
            break;
    }

    if (in->debug) {
        AM_LOGD("input type =%d, ret = %d, real_frames = %zu, num_read_buff_bytes = %zu",
                 kara->kara_input_type, ret, real_frames, num_read_buff_bytes);
    }

    if (0 == ret) {
        if (get_debug_value(AML_DUMP_AUDIOHAL_OUT) || in->debug) {
            if (KARA_INPUT_TYPE_USB_MIC == kara->kara_input_type) {
                aml_dump_audio_bitstreams("/data/audio/kara_mic_input_original_usb.raw", read_buff, num_read_buff_bytes);
            } else if (KARA_INPUT_TYPE_LINEIN_MIC == kara->kara_input_type) {
                aml_dump_audio_bitstreams("/data/audio/kara_mic_input_original_linein.raw", read_buff, num_read_buff_bytes);
            } else {
                aml_dump_audio_bitstreams("/data/audio/kara_mic_input_original.raw", read_buff, num_read_buff_bytes);
            }
        }
        /* Provide data for recording original data */
        provide_data_for_recorder(kara, KARA_RECORD_TYPE_MIC_ORIGINAL, read_buff, in->cfg, num_read_buff_bytes);
    } else {
        real_frames = 0;
    }

    return real_frames;
}

static ssize_t mic_data_process(struct kara_manager *kara, size_t in_frames, size_t out_frames)
{
    if (!kara || !kara->buf || 0 == in_frames || 0 == out_frames) {
        return -EINVAL;
    }
    struct voice_in *in = &kara->in;
    if (!in) {
        return -EINVAL;
    }
    size_t original_size = in_frames * in->cfg.frame_size; // size of original mic data
    size_t after_process_size = original_size; // size after process, initial with no process
    enum pcm_format format = in->pcm_in_config.format;
    unsigned int sample_size_in_bytes = pcm_format_to_bits(format) / 8;

    /* Volume Process Start*/
    //AM_LOGD("original_size=%zu mute=%d gain=%f", original_size, kara->kara_mic_mute, kara->kara_mic_gain);
    if (kara->kara_mic_mute) {
        memset(kara->buf, 0, original_size); // mute mic data
    } else {
        apply_volume(DbToAmpl(kara->kara_mic_gain), kara->buf, sample_size_in_bytes, original_size);
    }
    if (get_debug_value(AML_DUMP_AUDIOHAL_OUT) || in->debug) {
        if (KARA_INPUT_TYPE_USB_MIC == kara->kara_input_type) {
            aml_dump_audio_bitstreams("/data/audio/kara_after_volume_process_usb.raw", kara->buf, original_size);
        } else if (KARA_INPUT_TYPE_LINEIN_MIC == kara->kara_input_type) {
            aml_dump_audio_bitstreams("/data/audio/kara_after_volume_process_linein.raw", kara->buf, original_size);
        } else {
            aml_dump_audio_bitstreams("/data/audio/kara_after_volume_process.raw", kara->buf, original_size);
        }
    }
    /* Provide data for recording after volume process */
    provide_data_for_recorder(kara, KARA_RECORD_TYPE_MIC_AFTER_VOLUME, kara->buf, in->cfg, original_size);
    /* Volume Process End*/

    /* Reverb Process Start*/
    if (kara->reverb_enable) {
        /* reverb only support 2 channel & 16 bit currently */
        if ( 2 == in->pcm_in_config.channels && PCM_FORMAT_S16_LE == in->pcm_in_config.format) {
            AML_Reverb_Set_Mode(kara->reverb_handle, kara->reverb_mode);
            AML_Reverb_Process(kara->reverb_handle, kara->buf, kara->buf, original_size >> 2);
        } else {
            // todo after reverb support more input config
        }
    }
    if (get_debug_value(AML_DUMP_AUDIOHAL_OUT) || in->debug) {
        aml_dump_audio_bitstreams("/data/audio/kara_after_reverb_process.pcm", kara->buf, original_size);
    }
    /* Reverb Process End */

    return in_frames;
}

static ssize_t mic_data_output(struct kara_manager *kara, void *main_buffer, size_t in_frames, size_t out_frames)
{
    if (!kara || !kara->buf || !main_buffer || 0 == out_frames) {
        return -EINVAL;
    }
    struct voice_in *in = &kara->in;
    if (!in) {
        return -EINVAL;
    }
    size_t original_size = in_frames * in->cfg.frame_size; // size of original mic data
    size_t after_process_size = original_size; // size after process, initial with no process
    size_t mixout_bytes = out_frames * kara->mixout_config.frame_size;
    void *read_buff = kara->buf;
    void *out_buff = kara->buf;
    unsigned int num_in_channels = in->cfg.channelCnt;
    unsigned int num_mixout_channels = kara->mixout_config.channelCnt;
    uint32_t in_sample_rate = in->cfg.sampleRate;
    uint32_t out_sample_rate = kara->mixout_config.sampleRate;
    if (0 == num_in_channels || 0 == num_mixout_channels
        || 0 == in_sample_rate || 0 == out_sample_rate) {
        return -EINVAL;
    }
    enum pcm_format in_format = in->pcm_in_config.format;
    unsigned int in_sample_size = pcm_format_to_bits(in_format) / 8;

    /* SW Mix Process Start */
    if (KARA_OUTPUT_TYPE_SW_MIX == kara->kara_output_type) {
        /* 1.do channel adjust */
        if (num_in_channels == num_mixout_channels) {
            //do not need channel adjust
            read_buff = kara->buf;
            out_buff = kara->buf;
        } else {
            // need channel adjust
            after_process_size = (original_size * num_mixout_channels) / num_in_channels;
            if (after_process_size <= kara->buf_len) {
                // use kara->buf for saving data after channel contract
                read_buff = kara->buf;
                out_buff = kara->buf;
            } else {
                // use conversion_buffer for saving data after channel expand
                if (after_process_size > in->conversion_buffer_size) {
                    in->conversion_buffer = aml_audio_realloc(in->conversion_buffer, after_process_size);
                    if (!in->conversion_buffer) {
                        AM_LOGE("channel adjust conversion_buffer malloc error");
                        return -1;
                    }
                    in->conversion_buffer_size = after_process_size;
                }
                read_buff = kara->buf;
                out_buff = in->conversion_buffer;
            }
            after_process_size = adjust_channels(read_buff, num_in_channels,
                                                 out_buff, num_mixout_channels,
                                                 in_sample_size, original_size);
        }

        /* 2.do resample */
        if (kara->resample_handle) {
            read_buff = out_buff; //the last out buffer is the next input buffer
            if (!aml_audio_resample_process(kara->resample_handle, read_buff, after_process_size)) {
                after_process_size = kara->resample_handle->resample_size;
                if (after_process_size <= kara->buf_len) {
                    // use kara->buf for saving data after resample contract
                    out_buff = kara->buf;
                } else {
                    // use conversion_buffer for saving data after resample expand
                    if (after_process_size > in->conversion_buffer_size) {
                        in->conversion_buffer = aml_audio_realloc(in->conversion_buffer, after_process_size);
                        if (!in->conversion_buffer) {
                            AM_LOGE("resample conversion_buffer malloc error");
                            return -1;
                        }
                        in->conversion_buffer_size = after_process_size;
                    }
                    out_buff = in->conversion_buffer;
                }
                // use ring buffer because resample handle different length each time
                ring_buffer_write(&kara->resample_buffer, (unsigned char *)kara->resample_handle->resample_buffer,
                                  kara->resample_handle->resample_size, UNCOVER_WRITE);
                if (kara->resample_wait_count <= 3) {
                    kara->resample_wait_count += 1;
                    return 0;
                }
                // copy resample data to out buffer
                enum pcm_format out_format = aml_pcm_format_from_audio_format(kara->mixout_config.format);
                int out_sample_size = pcm_format_to_bits(out_format) / 8;
                int single_bytes = mixout_bytes * in_sample_size / out_sample_size;
                if (get_buffer_read_space(&kara->resample_buffer) >= single_bytes) {
                    ring_buffer_read(&kara->resample_buffer, out_buff, single_bytes);
                    //memcpy(out_buff, kara->resample_handle->resample_buffer, kara->resample_handle->resample_size);
                } else {
                    AM_LOGE("resample ring buffer size(%d) not enough for single_bytes(%d)",
                               get_buffer_read_space(&kara->resample_buffer), single_bytes);
                }
            }
        }

        /* 3.mixer mic data with main buffer */
        read_buff = out_buff; //the last out buffer is the next input buffer
        do_mixing_specified_channel_cnt(main_buffer, read_buff, out_frames,
                                        in->cfg.format, kara->mixout_config.format,
                                        num_mixout_channels);

        /* 4.dump data after mix process when debug */
        if (get_debug_value(AML_DUMP_AUDIOHAL_OUT) || in->debug) {
            if (KARA_INPUT_TYPE_USB_MIC == kara->kara_input_type) {
                aml_dump_audio_bitstreams("/data/audio/kara_after_mix_usb.raw", main_buffer, mixout_bytes);
            } else if (KARA_INPUT_TYPE_LINEIN_MIC == kara->kara_input_type) {
                aml_dump_audio_bitstreams("/data/audio/kara_after_mix_linein.raw", main_buffer, mixout_bytes);
            } else {
                aml_dump_audio_bitstreams("/data/audio/kara_after_mix.raw", main_buffer, mixout_bytes);
            }
        }
        /* 5.Provide data for recording data after mix process */
        provide_data_for_recorder(kara, KARA_RECORD_TYPE_MIC_AFTER_SW_MIX, main_buffer, kara->mixout_config, mixout_bytes);
        /* SW Mix Process End */
    }else {
        // add other output type here
    }

    return after_process_size;
}

static int kara_mix_micphone(struct kara_manager *kara, void *buf, size_t bytes)
{
    if (!kara || !buf || 0 == bytes) {
        return -EINVAL;
    }
    if (0 == kara->mixout_config.frame_size) {
        return -EINVAL;
    }
    struct voice_in *in = &kara->in;
    size_t out_frames = bytes / kara->mixout_config.frame_size; // frames on main buffer
    ssize_t frames_ret = 0;
    pthread_mutex_lock(&kara->lock);

    /* 1.mic data input management*/
    frames_ret = mic_data_input(kara, out_frames); // return real read frames
    if (frames_ret <= 0) {
        AM_LOGE("mic_data_input fail frames_ret(%zu)", frames_ret);
        goto exit;
    }

    /* 2.mic data process management */
    frames_ret = mic_data_process(kara, frames_ret, out_frames);
    if (frames_ret <= 0) {
        AM_LOGE("mic_data_process fail frames_ret(%zu)", frames_ret);
        goto exit;
    }

    /* 3.mic data output management */
    frames_ret = mic_data_output(kara, buf, frames_ret, out_frames);
    if (frames_ret <= 0) {
        AM_LOGE("mic_data_output fail frames_ret(%zu)", frames_ret);
        goto exit;
    }

exit:
    pthread_mutex_unlock(&kara->lock);
    return frames_ret;
}

/* For userspace recording original mic buffer */
static ssize_t kara_read_mic_buffer(struct kara_manager *kara, void *buffer, size_t bytes)
{
    if (!kara || !buffer || kara->mic_buffer.size == 0 || !kara->karaoke_start) {
        return -EINVAL;
    }

    uint32_t read_bytes = 0;
    int nodata_count = 0;
    int retry_max_count = 20;
    int sleep_us = 5000;
    size_t ret = 0;
    while (read_bytes < bytes) {
        ret = ring_buffer_read(&kara->mic_buffer, (uint8_t *)buffer + read_bytes, bytes - read_bytes);
        read_bytes += ret;
        if (read_bytes == bytes) {
            return bytes;
        }
        nodata_count++;
        if (nodata_count >= retry_max_count) {
            AM_LOGW("read data timeout, need:%zu, read_bytes:%d", bytes, read_bytes);
            memset(buffer, 0, bytes);
            return bytes;
        }
        usleep(sleep_us);
    }
    return bytes;
}

/* init ALOOP handle*/
static int kara_open_aloop_handle(struct kara_manager *kara, struct audioCfg *cfg)
{
    if (!kara || !cfg) {
        AM_LOGE("Input null pointer");
        return -EINVAL;
    }
    int aloop_card = alsa_device_get_card_index_by_name("Loopback");
    if (aloop_card < 0) {
        kara->loopback_handle = NULL;
        AM_LOGI("Aloop card device not found");
        return -1;
    } else {
        unsigned int aloop_device = 0; // Aloop device 0 for pcm write
        AM_LOGI("Aloop card device exist");
        struct pcm_config pcm_mixout_config;
        memset(&pcm_mixout_config, 0, sizeof(pcm_mixout_config));
        pcm_mixout_config.channels = cfg->channelCnt;
        pcm_mixout_config.rate = cfg->sampleRate;
        pcm_mixout_config.format = aml_pcm_format_from_audio_format(cfg->format);
        pcm_mixout_config.period_size = ALOOP_DEFAULT_PERIOD_SIZE;
        pcm_mixout_config.period_count = ALOOP_DEFAULT_PERIOD_COUNT;
        pcm_mixout_config.start_threshold = pcm_mixout_config.period_size *
                                            pcm_mixout_config.period_count / 2;
        kara->loopback_handle = pcm_open(aloop_card, aloop_device, PCM_OUT, &pcm_mixout_config);
        if (!pcm_is_ready(kara->loopback_handle)) {
            AM_LOGE("cannot open loopback: %s", pcm_get_error(kara->loopback_handle));
            pcm_close (kara->loopback_handle);
            kara->loopback_handle = NULL;
        }
    }
    return 0;
}

static int kara_open_micphone(struct kara_manager *kara, struct audioCfg *cfg)
{
    //AM_LOGI("enter");
    if (!kara || !cfg) {
        AM_LOGE("Input null pointer");
        return -EINVAL;
    }
    struct voice_in *in = NULL;
    struct pcm_config proxy_config;
    alsa_device_profile *profile = NULL;
    int ret = 0;
    if (!kara->in.in_profile || !profile_is_valid(kara->in.in_profile)) {
        AM_LOGE("usb in_profile invalid");
        return -EINVAL;
    }

    pthread_mutex_lock(&kara->lock);
    if (kara->karaoke_start == true) {
        AM_LOGI("karaoke already started");
        pthread_mutex_unlock(&kara->lock);
        return 0;
    }
    int init_ret = -1;
    in = &kara->in;
    profile = in->in_profile;
    memset(&proxy_config, 0, sizeof(proxy_config));
    // set same config with adev_open_input_stream for using ring buffer
    proxy_config.channels = profile_get_default_channel_count(profile);
    proxy_config.rate = profile_get_default_sample_rate(profile);
    proxy_config.format = profile_get_default_format(profile);
    proxy_config.period_size = USB_DEFAULT_PERIOD_SIZE;
    proxy_config.period_count = USB_DEFAULT_PERIOD_COUNT;

    /* in config for channel adjust and mix */
    in->cfg.channelCnt = proxy_config.channels;
    in->cfg.channelMask = audio_channel_in_mask_from_count(in->cfg.channelCnt);
    in->cfg.sampleRate = proxy_config.rate;
    in->cfg.format = aml_audio_format_from_pcm_format(proxy_config.format);
    in->cfg.frame_size = in->cfg.channelCnt * pcm_format_to_bits(proxy_config.format) / 8;
    in->debug = 0;
    in->pcm_in_config = proxy_config;
    kara->mixout_config = *cfg;
    kara->mixout_config.channelMask = audio_channel_in_mask_from_count(kara->mixout_config.channelCnt);

#if (ANDROID_PLATFORM_SDK_VERSION > 33) || (ANDROID_PLATFORM_SDK_VERSION == 33 \
        && (ANDROID_PLATFORM_SDK_EXTENSION_VERSION >= 5))
    ret = proxy_prepare(&in->proxy, profile, &proxy_config, false);
#else
    ret = proxy_prepare(&in->proxy, profile, &proxy_config);
#endif

    if (ret < 0) {
        AM_LOGE("proxy prepare fail");
        goto err;
    }

    /*re config period size by vendor*/
    unsigned int vendor_period_size = aml_usb_reconfig_period_size(in->proxy.alsa_config.period_size,
                                                                   in->proxy.alsa_config.rate);
    in->proxy.alsa_config.period_size = vendor_period_size;
    in->proxy.alsa_config.period_count *= 2; // default period_count from proxy is 2
    AM_LOGI("usb vendor_period_size = %d period_count = %d",
             in->proxy.alsa_config.period_size, in->proxy.alsa_config.period_count);

    AM_LOGI("proxy_prepare configs: channels %d format %d rate %d",
            proxy_config.channels, proxy_config.format, proxy_config.rate);
    AM_LOGI("in_configs: channels = %d, format = %d, rate = %d, frame_size = %d",
            in->cfg.channelCnt, in->cfg.format, in->cfg.sampleRate, in->cfg.frame_size);
    AM_LOGI("mixout_configs: channels %d, format %d, rate %d, frame_size %d",
            cfg->channelCnt, cfg->format, cfg->sampleRate, cfg->frame_size);

    ret = proxy_open(&in->proxy);
    if (ret < 0) {
        AM_LOGE("proxy open fail");
        goto err;
    }

    in->conversion_buffer = NULL;
    in->conversion_buffer_size = 0;
    kara->buf = NULL;
    kara->buf_len = 0;
    in->pcm_handle = NULL;

    /* init resample */
    if (in->cfg.sampleRate != kara->mixout_config.sampleRate) {
        kara->resample_config.aformat   = in->cfg.format;
        kara->resample_config.channels  = kara->mixout_config.channelCnt; // do channel adjust before resample
        kara->resample_config.input_sr  = in->cfg.sampleRate;
        kara->resample_config.output_sr = kara->mixout_config.sampleRate;
        ALOGI("init resampler from in-samplerate(%d) to out-samplerate(%d) channels(%d) format(%d)\n",
                kara->resample_config.input_sr, kara->resample_config.output_sr,
                kara->resample_config.channels, kara->resample_config.aformat);
        ret = aml_audio_resample_init((aml_audio_resample_t **)&kara->resample_handle, AML_AUDIO_SIMPLE_RESAMPLE, &kara->resample_config);
        if (ret < 0) {
            ALOGE("karaoke resample init error\n");
            kara->resample_handle = NULL;
        }
        /* init resample buffer for saving data after resample */
        init_ret = ring_buffer_init(&kara->resample_buffer, USB_DEFAULT_PERIOD_SIZE * 32);
        if (init_ret < 0) {
            AM_LOGE("malloc resample_buffer error");
        }
    } else {
        kara->resample_handle = NULL;
    }
    kara->resample_wait_count = 0;

    /* init ring buffer for save original mic data */
    init_ret = ring_buffer_init(&kara->mic_buffer, USB_DEFAULT_PERIOD_SIZE * 32);
    if (init_ret < 0) {
        AM_LOGE("malloc mic buffer error");
    }

    /* init ALOOP handle for recording */
    kara_open_aloop_handle(kara, cfg);

    kara->karaoke_start = true;
    pthread_mutex_unlock(&kara->lock);
    AM_LOGI("success and exit");
    return 0;

err:
    AM_LOGI("fail and exit");
    kara->karaoke_start = false;
    pthread_mutex_unlock(&kara->lock);
    return ret;
}

static int kara_open_linein_micphone(struct kara_manager *kara, struct audioCfg *cfg)
{
    //AM_LOGI("Enter");
    if (!kara || !cfg) {
        AM_LOGE("Input null pointer");
        return -EINVAL;
    }
    pthread_mutex_lock(&kara->lock);
    if (true == kara->karaoke_start) {
        AM_LOGI("%s() linein karaoke is already opened!", __func__);
        pthread_mutex_unlock(&kara->lock);
        return 0;
    }

    int init_ret = 0;
    struct voice_in *in = NULL;
    struct pcm *pcmIn = NULL;
    struct pcm_config pcm_linein_config;
    memset(&pcm_linein_config, 0, sizeof(pcm_linein_config));
    /*the record parameter depends on pcm in config */
    pcm_linein_config.channels = LINEIN_DEFAULT_CHANNEL;
    pcm_linein_config.rate = LINEIN_DEFAULT_SAMPLE_RATE;
    pcm_linein_config.format = LINEIN_DEFAULT_FORMAT;
    pcm_linein_config.period_size = LINEIN_DEFAULT_PERIOD_SIZE;
    pcm_linein_config.period_count = LINEIN_DEFAULT_PERIOD_COUNT;
    AM_LOGD("pcm_linein_config: channels %d, format %d, rate %d",
            pcm_linein_config.channels, pcm_linein_config.format, pcm_linein_config.rate);

    in = &kara->in;
    /* in config for channel adjust and mix */
    in->cfg.channelCnt = pcm_linein_config.channels;
    in->cfg.channelMask = AUDIO_CHANNEL_IN_STEREO;
    in->cfg.sampleRate = pcm_linein_config.rate;
    in->cfg.format = aml_audio_format_from_pcm_format(pcm_linein_config.format);
    in->cfg.frame_size = in->cfg.channelCnt * pcm_format_to_bits(pcm_linein_config.format) / 8;
    in->debug = 0;
    in->pcm_in_config = pcm_linein_config;
    kara->mixout_config = *cfg;
    kara->mixout_config.channelMask = audio_channel_in_mask_from_count(kara->mixout_config.channelCnt);

    /* open alsa capture handle */
    int card = alsa_device_get_card_index();
    int device = alsa_device_update_pcm_index(PORT_I2S, CAPTURE);
    pcmIn = pcm_open(card, device, PCM_IN, &pcm_linein_config);
    if (!pcm_is_ready(pcmIn)) {
        AM_LOGE("pcm_is_ready error!");
        if (pcmIn != NULL) {
            pcm_close(pcmIn);
            pcmIn = NULL;
        }
        init_ret = -1;
        goto err;
    }

    in->pcm_handle = pcmIn;
    in->conversion_buffer = NULL;
    in->conversion_buffer_size = 0;
    kara->buf = NULL;
    kara->buf_len = 0;

    /* init resample */
    if (in->cfg.sampleRate != kara->mixout_config.sampleRate) {
        kara->resample_config.aformat   = in->cfg.format;
        kara->resample_config.channels  = kara->mixout_config.channelCnt; // do channel adjust before resample
        kara->resample_config.input_sr  = in->cfg.sampleRate;
        kara->resample_config.output_sr = kara->mixout_config.sampleRate;
        ALOGI("init resampler from in-samplerate(%d) to out-samplerate(%d) channels(%d) format(%d)\n",
                kara->resample_config.input_sr, kara->resample_config.output_sr,
                kara->resample_config.channels, kara->resample_config.aformat);
        init_ret = aml_audio_resample_init((aml_audio_resample_t **)&kara->resample_handle, AML_AUDIO_SIMPLE_RESAMPLE, &kara->resample_config);
        if (init_ret < 0) {
            ALOGE("karaoke resample init error\n");
            kara->resample_handle = NULL;
        }
        /* init resample buffer for saving data after resample */
        init_ret = ring_buffer_init(&kara->resample_buffer, LINEIN_DEFAULT_PERIOD_SIZE * 32);
        if (init_ret < 0) {
            AM_LOGE("malloc resample_buffer error");
        }
    } else {
        kara->resample_handle = NULL;
    }
    kara->resample_wait_count = 0;

    init_ret = ring_buffer_init(&kara->mic_buffer, LINEIN_DEFAULT_PERIOD_SIZE * 32);
    if (init_ret < 0) {
        AM_LOGE("malloc mic buffer error");
    }

    AM_LOGI("in_configs: channels = %d, format = %d, rate = %d, frame_size = %d",
            in->cfg.channelCnt, in->cfg.format, in->cfg.sampleRate, in->cfg.frame_size);
    AM_LOGI("mixout_configs: channels = %d, format = %d, rate = %d, frame_size = %d",
            cfg->channelCnt, cfg->format, cfg->sampleRate, cfg->frame_size);

    /* init ALOOP handle for recording */
    kara_open_aloop_handle(kara, cfg);

    kara->karaoke_start = true;
    pthread_mutex_unlock(&kara->lock);
    AM_LOGI("success and exit");
    return 0;

err:
    AM_LOGI("fail and exit");
    kara->karaoke_start = false;
    pthread_mutex_unlock(&kara->lock);
    return init_ret;
}

static int kara_close_micphone(struct kara_manager *kara)
{
    if (!kara) {
        AM_LOGE("Input null pointer");
        return -EINVAL;
    }

    struct voice_in *in = &kara->in;
    AM_LOGI("enter");
    pthread_mutex_lock(&kara->lock);
    if (kara->karaoke_start == false) {
        AM_LOGI("karaoke already closed");
        pthread_mutex_unlock(&kara->lock);
        return 0;
    }

    /* close mic handle*/
    if (KARA_INPUT_TYPE_USB_MIC == kara->kara_input_type) {
        proxy_close(&in->proxy);
    } else if (KARA_INPUT_TYPE_LINEIN_MIC == kara->kara_input_type) {
        if (in->pcm_handle != NULL) {
            pcm_close(in->pcm_handle);
            in->pcm_handle == NULL;
        }
    } else {
        // add close function for other input here
    }

    aml_audio_free(in->conversion_buffer);
    in->conversion_buffer = NULL;
    in->conversion_buffer_size = 0;
    aml_audio_free(kara->buf);
    kara->buf = NULL;
    kara->buf_len = 0;
    ring_buffer_release(&kara->mic_buffer);
    kara->karaoke_start = false;
    /* close resample handle */
    if (kara->resample_handle) {
        aml_audio_resample_close(kara->resample_handle);
        kara->resample_handle = NULL;
        ring_buffer_release(&kara->resample_buffer);
        kara->resample_wait_count = 0;
    }
    kara->resample_wait_count = 0;
    /* close loopback handle */
    if (kara->loopback_handle) {
        pcm_close(kara->loopback_handle);
        kara->loopback_handle = NULL;
    }

    pthread_mutex_unlock(&kara->lock);
    AM_LOGI("exit");
    return 0;
}

static void add_echo_reference(struct kara_manager *kara,
                               struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&kara->lock);
    kara->echo_reference = reference;
    pthread_mutex_unlock(&kara->lock);
}

static void remove_echo_reference(struct kara_manager *kara,
                                  struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&kara->lock);
    if (kara->echo_reference == reference) {
        /* stop writing to echo reference */
        reference->write(reference, NULL);
        kara->echo_reference = NULL;
    }
    pthread_mutex_unlock(&kara->lock);
}

void put_echo_reference(struct kara_manager *kara,
                          struct echo_reference_itfe *reference)
{
    /*coverity[missing_lock]*/
    if (kara->echo_reference != NULL &&
            reference == kara->echo_reference) {
        remove_echo_reference(kara, reference);
        aml_release_echo_reference(reference);
        pthread_mutex_lock(&kara->lock);
        kara->echo_reference = NULL;
        pthread_mutex_unlock(&kara->lock);
    }
}

struct echo_reference_itfe *get_echo_reference(struct kara_manager *kara,
        audio_format_t format,
        uint32_t channel_count,
        uint32_t sampling_rate)
{
    struct echo_reference_itfe *echo = NULL;
    /*coverity[missing_lock]*/
    put_echo_reference(kara, kara->echo_reference);
    if (kara->karaoke_start) {
        uint32_t wr_channel_count = 2; // echo_reference support 2ch only
        uint32_t wr_sampling_rate = kara->in.cfg.sampleRate;
        audio_format_t wr_format = kara->in.cfg.format;
        AM_LOGI("rd channel %d rate %d format= %d, wr channel %d rate %d format= %d",
                 channel_count, sampling_rate, format,
                 wr_channel_count, wr_sampling_rate, wr_format);

        int status = aml_create_echo_reference(format,
                channel_count,
                sampling_rate,
                wr_format,
                wr_channel_count,
                wr_sampling_rate,
                &echo);
        if (status == 0) {
            add_echo_reference(kara, echo);
            AM_LOGI("success");
        }
    }

    return echo;
}

int karaoke_check_mix_output(struct kara_manager *kara, void *buffer, size_t bytes)
{
    if (!kara || !buffer || 0 == bytes) {
        //AM_LOGE("parameter invalid");
        return -EINVAL;
    }
    int ret = 0;
//    AM_LOGI("karaoke_input_type=%d, karaoke_on=%d, karaoke_enable=%d, karaoke_start=%d",
//             kara->kara_input_type, kara->karaoke_on, kara->karaoke_enable, kara->karaoke_start);
    if (kara->karaoke_on && kara->karaoke_enable) {
        if (!kara->karaoke_start && kara->open) {
            ret = kara->open(kara, &kara->mixout_config);
            if (ret < 0) {
                AM_LOGE("karaoke open failed: %d", ret);
            }
        } else if (kara->karaoke_start && kara->mix) {
            kara->mix(kara, buffer, bytes);
        }
    } else if (kara->karaoke_start && kara->close) {
        kara->close(kara);
    }

    return 0;
}

int karaoke_get_audioCfg_from_ms12_info(struct audioCfg *cfg, struct aml_ms12_dec_info *ms12_info)
{
    int ret = 0;
    if (!cfg || !ms12_info) {
        AM_LOGE("parameter invalid");
        ret = -EINVAL;
        return ret;
    }
    cfg->format = ms12_info->data_type;
    cfg->channelCnt = ms12_info->output_ch;
    cfg->sampleRate = ms12_info->output_sr;
    cfg->frame_size = cfg->channelCnt *
                      pcm_format_to_bits(aml_pcm_format_from_audio_format(cfg->format)) / 8;

    AM_LOGV("format=%d, channel=%d, sampleRate=%d", cfg->format, cfg->channelCnt, cfg->sampleRate);
    return ret;
}

int karaoke_get_audioCfg_from_pcm_config(struct audioCfg *cfg, struct pcm_config *pcm_cfg)
{
    int ret = 0;
    if (!cfg || !pcm_cfg) {
        AM_LOGE("parameter invalid");
        ret = -EINVAL;
        return ret;
    }
    cfg->channelCnt = pcm_cfg->channels;
    cfg->sampleRate = pcm_cfg->rate;
    cfg->format = aml_audio_format_from_pcm_format(pcm_cfg->format);
    cfg->frame_size = cfg->channelCnt * pcm_format_to_bits(pcm_cfg->format) / 8;

    AM_LOGV("format=%d, channel=%d, sampleRate=%d, frame_size=%d",
            cfg->format, cfg->channelCnt, cfg->sampleRate, cfg->frame_size);
    return ret;
}

/* karaoke init*/
int karaoke_init(struct kara_manager *kara,
                      alsa_device_profile *profile,
                      kara_input_type_t kara_input_type,
                      kara_output_type_t kara_output_type)
{
    AM_LOGI("enter, kara_input_type=%d, kara_output_type=%d", kara_input_type, kara_output_type);
    if (!kara) {
        return -EINVAL;
    }
    int ret = -1;
    pthread_mutex_init(&kara->lock, NULL);
    kara->kara_output_type = kara_output_type;

    /* distinguish different input open by kara_input_type*/
    kara->kara_input_type = kara_input_type;
    switch (kara->kara_input_type) {
        case KARA_INPUT_TYPE_USB_MIC:
            kara->open = kara_open_micphone;
            kara->in.in_profile = profile;
            break;
        case KARA_INPUT_TYPE_LINEIN_MIC:
            kara->open = kara_open_linein_micphone;
            kara->in.in_profile = NULL;
            break;
        default:
            //add open function for other input type here
            kara->open = NULL;
            return -EINVAL;
    }
    kara->read = kara_read_mic_buffer;
    kara->mix = kara_mix_micphone;
    kara->close = kara_close_micphone;

    /* init reverb handle */
    if (!kara->reverb_handle) {
        ret = AML_Reverb_Init(&kara->reverb_handle);
        if (ret < 0) {
            AM_LOGE("%s() int Reverb Error!", __func__);
            return -EINVAL;
        }
    }

    AM_LOGI("exit");
    return 0;
}

/* karaoke close mic and free resource */
int karaoke_close(struct kara_manager *kara)
{
    AM_LOGI("enter");
    int ret = 0;
    if (!kara || !kara->close) {
        ret = -ENOSYS;
        return ret;
    }

    if (kara->karaoke_start) {
        kara->close(kara);
    }
    AM_LOGI("exit");
    return ret;
}

/* query karaoke input type */
kara_input_type_t karaoke_get_input_type(struct kara_manager *kara)
{
    if (!kara) {
        return KARA_INPUT_TYPE_NONE;
    }
    AM_LOGI("type = %d", kara->kara_input_type);
    return kara->kara_input_type;
}

/* set karaoke input type */
int karaoke_set_input_type(struct kara_manager *kara, int value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    if (value < KARA_INPUT_TYPE_NONE || value > KARA_INPUT_TYPE_MAX) {
        return -EINVAL;
    }
    kara->kara_input_type = value;
    return 0;
}

/* query karaoke output type */
kara_output_type_t karaoke_get_output_type(struct kara_manager *kara)
{
    if (!kara) {
        return KARA_OUTPUT_TYPE_NONE;
    }
    AM_LOGI("type = %d", kara->kara_output_type);
    return kara->kara_output_type;
}

/* set karaoke output type */
int karaoke_set_output_type(struct kara_manager *kara, int value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    if (value < KARA_OUTPUT_TYPE_NONE || value > KARA_OUTPUT_TYPE_MAX) {
        return -EINVAL;
    }
    kara->kara_output_type = value;
    return 0;
}

/* query karaoke is on or not*/
bool karaoke_get_on(struct kara_manager *kara)
{
    if (!kara) {
        return false;
    }
    //AM_LOGI("value = %d", kara->karaoke_on);
    return kara->karaoke_on;
}

/* set karaoke on true or false*/
int karaoke_set_on(struct kara_manager *kara, bool value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    kara->karaoke_on = value;
    return 0;
}

/* query karaoke is enabled or not*/
bool karaoke_get_enable(struct kara_manager *kara)
{
    if (!kara) {
        return false;
    }
    //AM_LOGI("value = %d", kara->karaoke_enable);
    return kara->karaoke_enable;
}

/* set karaoke enable true or false*/
int karaoke_set_enable(struct kara_manager *kara, bool value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    kara->karaoke_enable = value;
    return 0;
}

/* query karaoke is started or not*/
bool karaoke_get_start(struct kara_manager *kara)
{
    if (!kara) {
        return false;
    }
    //AM_LOGI("value = %d", kara->karaoke_start);
    return kara->karaoke_start;
}

/* set karaoke start true or false*/
int karaoke_set_start(struct kara_manager *kara, bool value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    kara->karaoke_start = value;
    return 0;
}

/* query karaoke record start or not*/
bool karaoke_get_mic_record(struct kara_manager *kara)
{
    if (!kara) {
        return false;
    }
    //AM_LOGI("value = %d", kara->kara_mic_record);
    return kara->kara_mic_record;
}

/* set karaoke mic record start true or false*/
int karaoke_set_mic_record(struct kara_manager *kara, bool value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    kara->kara_mic_record = value;
    return 0;
}

/* query karaoke mic mute true or false*/
bool karaoke_get_mic_mute(struct kara_manager *kara)
{
    if (!kara) {
        return false;
    }
    AM_LOGI("value = %d", kara->kara_mic_mute);
    return kara->kara_mic_mute;
}

/* set karaoke mic mute true or false*/
int karaoke_set_mic_mute(struct kara_manager *kara, bool value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    kara->kara_mic_mute = value;
    return 0;
}

/* query karaoke mic volume enable true or false*/
bool karaoke_get_volume_enable(struct kara_manager *kara)
{
    if (!kara) {
        return false;
    }
    AM_LOGI("value = %d", kara->kara_mic_volume_enable);
    return kara->kara_mic_volume_enable;
}

/* set karaoke mic volume enable true or false*/
int karaoke_set_volume_enable(struct kara_manager *kara, bool value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    kara->kara_mic_volume_enable = value;
    return 0;
}

/* query karaoke mic volume gain with DB*/
float karaoke_get_mic_gain(struct kara_manager *kara)
{
    if (!kara) {
        return false;
    }
    AM_LOGI("value = %f", kara->kara_mic_gain);
    return kara->kara_mic_gain;
}

/* set karaoke mic gain with DB*/
int karaoke_set_mic_gain(struct kara_manager *kara, float value)
{
    AM_LOGI("value = %f", value);
    if (!kara) {
        return -EINVAL;
    }
    kara->kara_mic_gain = value;
    return 0;
}

/* query karaoke mic reverb enable true or false*/
bool karaoke_get_reverb_enable(struct kara_manager *kara)
{
    if (!kara) {
        return false;
    }
    AM_LOGI("value = %d", kara->reverb_enable);
    return kara->reverb_enable;
}

/* set karaoke on true or false*/
int karaoke_set_reverb_enable(struct kara_manager *kara, bool value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    kara->reverb_enable = value;
    return 0;
}

/* query karaoke reverb mode*/
float karaoke_get_reverb_mode(struct kara_manager *kara)
{
    if (!kara) {
        return false;
    }
    AM_LOGI("value = %d", kara->reverb_mode);
    return kara->reverb_mode;
}

/* set karaoke reverb mode */
int karaoke_set_reverb_mode(struct kara_manager *kara, int value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    kara->reverb_mode = value;
    return 0;
}

/* query karaoke debug value */
bool karaoke_get_debug(struct kara_manager *kara)
{
    if (!kara) {
        return false;
    }
    AM_LOGI("value = %d", kara->in.debug);
    return kara->in.debug;
}

/* set karaoke debug value */
int karaoke_set_debug(struct kara_manager *kara, bool value)
{
    AM_LOGI("value = %d", value);
    if (!kara) {
        return -EINVAL;
    }
    kara->in.debug = value;
    return 0;
}

/* get audio config for recording such as adev_open_input_stream */
int karaoke_get_config_by_record_type(struct kara_manager *kara,
                                                    kara_record_type_t record_type,
                                                    struct audio_config *config)
{
    AM_LOGI("record_type = %d", record_type);
    if (!kara) {
        return -EINVAL;
    }

    switch (record_type) {
        case KARA_RECORD_TYPE_MIC_ORIGINAL:
            config->sample_rate = kara->in.cfg.sampleRate;
            config->format = kara->in.cfg.format;
            config->channel_mask = kara->in.cfg.channelMask;
            break;
        case KARA_RECORD_TYPE_MIC_AFTER_VOLUME:
        case KARA_RECORD_TYPE_MIC_AFTER_REVERB:
            config->sample_rate = kara->in.cfg.sampleRate;
            config->format = kara->in.cfg.format;
            //default 2 when using echo_reference
            config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
            break;
        case KARA_RECORD_TYPE_MIC_AFTER_SW_MIX:
            config->sample_rate = kara->mixout_config.sampleRate;
            config->format = kara->mixout_config.format;
            config->channel_mask = kara->mixout_config.channelMask;
            break;
        default:
            //add other config here
            AM_LOGW("record_type do not support");
            return -1;
    }

    AM_LOGI("config->sample_rate(%d), config->format(%d), config->channel_mask(%x)",
               config->sample_rate, config->format, config->channel_mask);
    return 0;
}

/* ----------put the functions related to project below --------------------*/

/* get config from json */
void karaoke_get_project_config(void *audio_config)
{
    //AM_LOGI("enter\n");
    if (!audio_config) {
        AM_LOGE("audio_config is null \n");
        return;
    }
    struct audio_board_config *config = (struct audio_board_config *)audio_config;
    /* usb karaoke */
    config->usb_kara_config.feature_enable = !!aml_get_jason_int_value("Usb_Karaoke_Enable", 0);
    config->usb_kara_config.default_input_type = aml_get_jason_int_value("Usb_Karaoke_Input_Type", 0);
    config->usb_kara_config.default_output_type = aml_get_jason_int_value("Usb_Karaoke_Output_Type", 0);
    config->usb_kara_config.default_mic_volume_enable = !!aml_get_jason_int_value("Usb_Karaoke_Mic_Volume_Enable", 1);
    config->usb_kara_config.default_mic_volume_gain = (float)aml_get_jason_int_value("Usb_Karaoke_Mic_Volume_Gain", 0);
    config->usb_kara_config.default_reverb_enable = !!aml_get_jason_int_value("Usb_Karaoke_Reverb_Enable", 1);
    config->usb_kara_config.default_reverb_mode = aml_get_jason_int_value("Usb_Karaoke_Reverb_Mode", 0);
    /* linein karaoke */
    config->linein_kara_config.feature_enable = !!aml_get_jason_int_value("Linein_Karaoke_Enable", 0);
    config->linein_kara_config.default_input_type = aml_get_jason_int_value("Linein_Karaoke_Input_Type", 1);
    config->linein_kara_config.default_output_type = aml_get_jason_int_value("Linein_Karaoke_Output_Type", 0);
    config->linein_kara_config.default_mic_volume_enable = !!aml_get_jason_int_value("Linein_Karaoke_Mic_Volume_Enable", 1);
    config->linein_kara_config.default_mic_volume_gain = (float)aml_get_jason_int_value("Linein_Karaoke_Mic_Volume_Gain", 0);
    config->linein_kara_config.default_reverb_enable = !!aml_get_jason_int_value("Linein_Karaoke_Reverb_Enable", 1);
    config->linein_kara_config.default_reverb_mode = aml_get_jason_int_value("Linein_Karaoke_Reverb_Mode", 0);

    AM_LOGD("usb karaoke feature enable(%d), linein karaoke feature enable(%d)",
             config->usb_kara_config.feature_enable, config->linein_kara_config.feature_enable);
}

/* config set to usb&linein karaoke and do other init */
int karaoke_project_init(void *aml_adev)
{
    //AM_LOGI("enter");
    if (!aml_adev) {
        AM_LOGE("aml_adev is null \n");
        return -1;
    }
    int ret = -1;
    struct aml_audio_device *adev = (struct aml_audio_device *)aml_adev;

    /* usb karaoke init from json config */
    ret = karaoke_set_input_type(&adev->usb_audio.karaoke, adev->board_config.usb_kara_config.default_input_type);
    if (ret) {
        karaoke_set_input_type(&adev->usb_audio.karaoke, KARA_INPUT_TYPE_USB_MIC);
    }
    ret = karaoke_set_output_type(&adev->usb_audio.karaoke, adev->board_config.usb_kara_config.default_output_type);
    if (ret) {
        karaoke_set_output_type(&adev->usb_audio.karaoke, KARA_OUTPUT_TYPE_SW_MIX);
    }
    ret = karaoke_set_volume_enable(&adev->usb_audio.karaoke, adev->board_config.usb_kara_config.default_mic_volume_enable);
    if (ret) {
        karaoke_set_volume_enable(&adev->usb_audio.karaoke, true);
    }
    ret = karaoke_set_mic_gain(&adev->usb_audio.karaoke, adev->board_config.usb_kara_config.default_mic_volume_gain);
    if (ret) {
        karaoke_set_mic_gain(&adev->usb_audio.karaoke, 0); // 0db
    }
    ret = karaoke_set_reverb_enable(&adev->usb_audio.karaoke, adev->board_config.usb_kara_config.default_reverb_enable);
    if (ret) {
        karaoke_set_reverb_enable(&adev->usb_audio.karaoke, false);
    }
    ret = karaoke_set_reverb_mode(&adev->usb_audio.karaoke, adev->board_config.usb_kara_config.default_reverb_mode);
    if (ret) {
        karaoke_set_reverb_mode(&adev->usb_audio.karaoke, 1);
    }
    AM_LOGD("usb karaoke: input_type(%d) output_type(%d) volume_enable(%d) volume_gain(%f) reverb_enable(%d) reverb_mode(%d)",
             adev->usb_audio.karaoke.kara_input_type, adev->usb_audio.karaoke.kara_output_type,
             adev->usb_audio.karaoke.kara_mic_volume_enable, adev->usb_audio.karaoke.kara_mic_gain,
             adev->usb_audio.karaoke.reverb_enable, adev->usb_audio.karaoke.reverb_mode);

    /* linein karaoke init from json config */
    ret = karaoke_set_input_type(&adev->linein_karaoke, adev->board_config.linein_kara_config.default_input_type);
    if (ret) {
        karaoke_set_input_type(&adev->linein_karaoke, KARA_INPUT_TYPE_LINEIN_MIC);
    }
    ret = karaoke_set_output_type(&adev->linein_karaoke, adev->board_config.linein_kara_config.default_output_type);
    if (ret) {
        karaoke_set_output_type(&adev->linein_karaoke, KARA_OUTPUT_TYPE_SW_MIX);
    }
    ret = karaoke_set_volume_enable(&adev->linein_karaoke, adev->board_config.linein_kara_config.default_mic_volume_enable);
    if (ret) {
        karaoke_set_volume_enable(&adev->linein_karaoke, true);
    }
    ret = karaoke_set_mic_gain(&adev->linein_karaoke, adev->board_config.linein_kara_config.default_mic_volume_gain);
    if (ret) {
        karaoke_set_mic_gain(&adev->linein_karaoke, 0); // 0db
    }
    ret = karaoke_set_reverb_enable(&adev->linein_karaoke, adev->board_config.linein_kara_config.default_reverb_enable);
    if (ret) {
        karaoke_set_reverb_enable(&adev->linein_karaoke, false);
    }
    ret = karaoke_set_reverb_mode(&adev->linein_karaoke, adev->board_config.linein_kara_config.default_reverb_mode);
    if (ret) {
        karaoke_set_reverb_mode(&adev->linein_karaoke, 1);
    }
    AM_LOGD("linein karaoke: input_type(%d) output_type(%d) volume_enable(%d) volume_gain(%f) reverb_enable(%d) reverb_mode(%d)",
             adev->linein_karaoke.kara_input_type, adev->linein_karaoke.kara_output_type,
             adev->linein_karaoke.kara_mic_volume_enable, adev->linein_karaoke.kara_mic_gain,
             adev->linein_karaoke.reverb_enable, adev->linein_karaoke.reverb_mode);

    /* default init linein karaoke */
    ret = karaoke_init(&adev->linein_karaoke, NULL,
                       karaoke_get_input_type(&adev->linein_karaoke),
                       karaoke_get_output_type(&adev->linein_karaoke));
    if (ret) {
        AM_LOGE("linein karaoke init fail");
        return ret;
    }
    /* default set linein mic enable */
    karaoke_set_enable(&adev->linein_karaoke, true);

    return ret;
}

/* set karaoke parameters */
int karaoke_set_parameters(struct audio_hw_device *dev, char *param_value)
{
    if (!dev || !param_value) {
        AM_LOGE("input parameter is null \n");
        return -1;
    }
    int val = 0;
    bool valbool = false;
    bool support = false;
    char *param = NULL;
    struct kara_manager *kara = NULL;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct amlAudioMixer *audio_mixer = adev->mixerData;

    /* karaoke name */
    param = strstr(param_value, "usb");
    if (param) {
        param += 4;
        kara = &adev->usb_audio.karaoke;
        support = true;
    }
    if (!support) {
        param = strstr(param_value, "linein");
        if (param) {
            param += 7;
            kara = &adev->linein_karaoke;
            support = true;
        }
    }
    if (!support) {
        AM_LOGE("unsupported karaoke name\n");
        return -1;
    }
    /* karaoke on/off switch */
    param = strstr(param_value, "switch");
    if (param) {
        /* check feature config on project json file first */
        if (kara == &adev->usb_audio.karaoke) {
            if (!adev->board_config.usb_kara_config.feature_enable) {
                AM_LOGW("project do not define Usb_Karaoke_Enable:1");
                return 0;
            }
        } else if (kara == &adev->linein_karaoke) {
            if (!adev->board_config.linein_kara_config.feature_enable) {
                AM_LOGW("project do not define Linein_Karaoke_Enable:1");
                return 0;
            }
        }
        param += 7;
        sscanf(param, "%d", &val);
        valbool = !!val;
        karaoke_set_on(kara, valbool);

        if (valbool) {
            /* karaoke depends on continues output */
            if (adev->useAudioMixer) {
                aml_audiohal_sch_state_2_submix(audio_mixer, SUBMIX_SCHEDULER_RUNNING);
            }

            if (ms12->ms12_scheduler_state != MS12_SCHEDULER_RUNNING) {
                aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_RUNNING);
            }
        }
        return 0;
    }
    /* karaoke debug */
    param = strstr(param_value, "debug");
    if (param) {
        param += 6;
        sscanf(param, "%d", &val);
        valbool = !!val;
        karaoke_set_debug(kara, valbool);
        return 0;
    }
    /* karaoke input type */
    param = strstr(param_value, "input_type");
    if (param) {
        param += 11;
        sscanf(param, "%d", &val);
        karaoke_set_input_type(kara, val);
        return 0;
    }
    /* karaoke output type */
    param = strstr(param_value, "output_type");
    if (param) {
        param += 12;
        sscanf(param, "%d", &val);
        karaoke_set_output_type(kara, val);
        return 0;
    }
    /* karaoke record flag when using ring buffer */
    param = strstr(param_value, "record");
    if (param) {
        param += 7;
        sscanf(param, "%d", &val);
        valbool = !!val;
        karaoke_set_mic_record(kara, valbool);
        return 0;
    }
    /* karaoke mic mute */
    param = strstr(param_value, "mic_mute");
    if (param) {
        param += 9;
        sscanf(param, "%d", &val);
        valbool = !!val;
        karaoke_set_mic_mute(kara, valbool);
        return 0;
    }
    /* karaoke mic volume enable */
    param = strstr(param_value, "mic_volume_enable");
    if (param) {
        param += 18;
        sscanf(param, "%d", &val);
        valbool = !!val;
        karaoke_set_volume_enable(kara, valbool);
        return 0;
    }
    /* karaoke mic volume gain value */
    param = strstr(param_value, "mic_volume_gain");
    if (param) {
        param += 16;
        sscanf(param, "%d", &val);
        karaoke_set_mic_gain(kara, val);
        return 0;
    }
    /* karaoke mic reverb enable */
    param = strstr(param_value, "reverb_enable");
    if (param) {
        param += 14;
        sscanf(param, "%d", &val);
        valbool = !!val;
        karaoke_set_reverb_enable(kara, valbool);
        return 0;
    }
    /* karaoke mic reverb mode value */
    param = strstr(param_value, "reverb_mode");
    if (param) {
        param += 12;
        sscanf(param, "%d", &val);
        karaoke_set_reverb_mode(kara, val);
        return 0;
    }

    AM_LOGE(" parameter is illegal");
    return -1;
}

#endif /* SUPPORT_KARAOKE */

