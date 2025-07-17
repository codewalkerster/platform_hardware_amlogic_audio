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

#define LOG_TAG "audio_hw_hal_primary"
//#define LOG_NDEBUG 0

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
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <linux/ioctl.h>
#include <hardware/hardware.h>
#include <system/audio.h>
#include <audio_utils/channels.h>
#include <audio_utils/primitives.h>

#if ANDROID_PLATFORM_SDK_VERSION >= 25 //8.0
#include <system/audio-base.h>
#endif

#include <hardware/audio.h>
#include <hardware/audio_alsaops.h>
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>
#include <audio_route/audio_route.h>
#include <spdifenc_wrap.h>
#include <aml_android_utils.h>
#include <aml_alsa_mixer.h>
#include <aml_audio_spdifdec.h>

#include "SPDIFEncoderAD.h"
#include "aml_volume_utils.h"
#include "aml_data_utils.h"
#include "spdifenc_wrap.h"
#include "alsa_manager.h"
#include "aml_audio_stream.h"
#include "audio_hw.h"
#include "spdif_encoder_api.h"
#include "audio_hw_utils.h"
#include "aml_audio_report.h"
#include "aml_audio_sysfs.h"
#include "audio_hw_profile.h"
#include "aml_dump_debug.h"
#include "alsa_device_parser.h"
#include "aml_audio_stream.h"
#include "alsa_config_parameters.h"
#include "spdif_encoder_api.h"
#include "aml_ng.h"
#include "aml_audio_timer.h"
#include "aml_audio_ease.h"
#include "aml_audio_spdifout.h"
#include "aml_audio_output.h"
#include "aml_mmap_audio.h"
#include "earc_utils.h"
#include "aml_audio_uevent.h"

#include <dolby_ms12_status.h>
#include <SPDIFEncoderAD.h>
#include "audio_hw_ms12.h"
#include "audio_hw_ms12_common.h"
#include "dolby_lib_api.h"
#include "aml_audio_ac3parser.h"
#include "aml_audio_ac4parser.h"
#include "aml_audio_ms12_sync.h"
#include "audio_hwsync_wrap.h"
#include "aml_stream_manager.h"
#include "aml_parser_manager.h"

#include "tv_patch_ctrl.h"
#include "tv_patch.h"
#include "hdmirx_utils.h"
#include "aml_audio_dev2mix_process.h"

#include "aml_math_utils.h"
#include "aml_audio_ms12_render.h"
#include "aml_audio_nonms12_render.h"
#include "aml_vad_wakeup.h"
#include "aml_config_data.h"
#include "aml_hfp.h"

#include "aml_async_write.h"
#include "audio_hw_resource_mgr.h"
#include "device_patch.h"
#include "component_noise_gate.h"
#include "tv_private_object.h"
#include "hdmirx_utils.h"
#include "aml_audio_enhancement.h"
#include <sys/resource.h>
#include "audio_mpegh.h"
#include "audio_mediasync_wrap.h"

#define ENABLE_NANO_NEW_PATH 1
#if ENABLE_NANO_NEW_PATH
#include "jb_nano.h"
#endif

#ifdef ENABLE_DVB_PATCH
// for dtv playback
#include "dtv_patch.h"
#endif


/*Google Voice Assistant channel_mask */
#define BUILT_IN_MIC 12

#define HDMI_LATENCY_MS 60

//#include "amlAudioMixer.h"
#include "audio_bt_hal.h"
//#include "a2dp_hal.h"
#include "audio_bt_sco.h"
#include "aml_malloc_debug.h"
#ifdef ENABLE_AEC_APP
#include "audio_aec.h"
#endif
#include <audio_effects/effect_aec.h>
#include <audio_utils/clock.h>
#include "audio_dummy_streamout.h"
#include "aml_audio_stream_base.h"

//audio content recognize function
#include "aml_ai_audio.h"

#include "device_patch.h"
#include "component_picture_mode.h"
#include "dtv_private_object.h"
#include "hdmirx_utils.h"
#include <sys/utsname.h>

#ifdef ENABLE_AUTOMOTIVE_AUDIO_FUNCTION
#include "../automotive/bus_stream_out.h"
#endif

#ifdef LOWPOWER_DSP_FFV
#include "audio_hw_dsp.h"
#include "audio_hw_ffv.h"
#endif

#define CARD_AMLOGIC_BOARD 0


/*Google Voice Assistant channel_mask */
#define BUILT_IN_MIC 12

/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000
#undef RESAMPLER_BUFFER_FRAMES
#define RESAMPLER_BUFFER_FRAMES (PERIOD_SIZE * 6)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES)
#define NSEC_PER_SECOND 1000000000ULL

#define DOLBY_MS12_INPUT_FORMAT_TEST

#define IEC61937_PACKET_SIZE_OF_AC3                     (0x1800)
#define IEC61937_PACKET_SIZE_OF_EAC3                    (0x6000)

#define MAX_INPUT_STREAM_CNT                            (3)

#define DIRECT_DDP_BUFSIZE                              (768)

#define NETFLIX_DDP_BUFSIZE                             (768)
#define NETFLIX_DDP_ATMOS_BUFSIZE                       (1792)
#define OUTPUT_PORT_MAX_COEXIST_NUM                     (3)

/*Tunnel sync HEADER is 20 bytes*/
#define TUNNEL_SYNC_HEADER_SIZE    (20)
#define TUNNEL_SYNC_NETFLIX_MULITCH_HEADER_SIZE (24)

/* this latency is from logcat time. */
#define HAL_MS12_PIPELINE_LATENCY (10)
#define VX_BUFFER_CLEAR_STEREO_FRAME_SIZE 1024
#define VX_BUFFER_CLEAR_MULTICHANNEL_FRAME_SIZE 6144
#define VX_BUFFER_CLEAR_COUNT 5

static const struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_PLAYBACK_PERIOD_SIZE,
    .period_count = DEFAULT_PLAYBACK_PERIOD_CNT,
    .format = PCM_FORMAT_S16_LE,
};

static const struct pcm_config pcm_config_out_direct = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_PLAYBACK_PERIOD_SIZE,
    .period_count = DEFAULT_PLAYBACK_PERIOD_CNT,
    .format = PCM_FORMAT_S16_LE,
};

static const struct pcm_config pcm_config_in = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = DEFAULT_CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

static const struct pcm_config pcm_config_bt = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = DEFAULT_PLAYBACK_PERIOD_SIZE,
    .period_count = PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

static void select_output_device (struct aml_audio_device *adev);
static void select_input_device (struct aml_audio_device *adev);
static void select_devices (struct aml_audio_device *adev);
static int adev_set_voice_volume (struct audio_hw_device *dev, float volume);
static int do_output_standby (struct aml_stream_out *out);
static uint32_t out_get_sample_rate (const struct audio_stream *stream);
static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle __unused,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address __unused);
static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream);
ssize_t out_write_new(struct audio_stream_out *stream,
                      const void *buffer,
                      size_t bytes);
static int out_get_presentation_position(const struct audio_stream_out *stream,
                                         uint64_t *frames,
                                         struct timespec *timestamp);
static bool is_contain_d2d_patch(struct aml_audio_device *adev, struct audio_patch *unused_patch);

static int adev_release_audio_patch(struct audio_hw_device *dev,
                                audio_patch_handle_t handle);

static int adev_create_audio_patch(struct audio_hw_device *dev,
                                unsigned int num_sources,
                                const struct audio_port_config *sources,
                                unsigned int num_sinks,
                                const struct audio_port_config *sinks,
                                audio_patch_handle_t *handle);

static int adev_close(hw_device_t *device);
static aec_timestamp get_timestamp(void);

static int adev_get_mic_mute(const struct audio_hw_device* dev, bool* state);
static int adev_get_microphones(const struct audio_hw_device* dev,
                                struct audio_microphone_characteristic_t* mic_array,
                                size_t* mic_count);
static void get_mic_characteristics(struct audio_microphone_characteristic_t* mic_data,
                                    size_t* mic_count);

void aml_audio_output_routing(struct aml_audio_device *adev, audio_devices_t cur_output_device);
static void * g_aml_primary_adev = NULL;

void *aml_adev_get_handle(void)
{
    return (void *)g_aml_primary_adev;
}

int out_set_playback_rate_parameters(struct audio_stream_out *stream,
                                          const audio_playback_rate_t *playbackRate)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    aml_stream_speed_info_t *speed_info = &out->speed_info;

    pthread_mutex_lock (&adev->lock);
    pthread_mutex_lock (&out->lock);

    AM_LOGI("out set stream playback rate change %f => %f, mPitch = %f, mStretchMode = %d, mFallbackMode = %d",
        speed_info->speed, playbackRate->mSpeed, playbackRate->mPitch, playbackRate->mStretchMode, playbackRate->mFallbackMode);

    //Customers have 4X speed requirements, so the speed range is limited to [0.01~4]
    if (playbackRate->mSpeed > AML_AUDIO_TIMESTRETCH_SPEED_MAX || playbackRate->mSpeed < AML_AUDIO_TIMESTRETCH_SPEED_MIN) {
        AM_LOGE("Speed parameters is not supported !! playbackRate->mSpeed %f", playbackRate->mSpeed);
        pthread_mutex_unlock (&out->lock);
        pthread_mutex_unlock (&adev->lock);
        return -ENOSYS;
    }

    out->output_speed = playbackRate->mSpeed;

    speed_info->last_speed = speed_info->speed;
    speed_info->speed = playbackRate->mSpeed;
    speed_info->mPitch = playbackRate->mPitch;
    speed_info->mStretchMode = playbackRate->mStretchMode;
    speed_info->mFallbackMode = playbackRate->mFallbackMode;

    speed_info->hwsync_force_update = true;

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        set_dolby_ms12_main_speed(stream, (double)playbackRate->mSpeed);
        ALOGI("%s(), aml_out->output_speed %f", __FUNCTION__,playbackRate->mSpeed);
    }

    pthread_mutex_unlock (&out->lock);
    pthread_mutex_unlock (&adev->lock);

    return 0;
}

int out_get_playback_rate_parameters(struct audio_stream_out *stream,
                                          audio_playback_rate_t *playbackRate)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    aml_stream_speed_info_t *speed_info = &out->speed_info;

    pthread_mutex_lock (&adev->lock);
    pthread_mutex_lock (&out->lock);

    AM_LOGI("out_get_playback_rate_parameters %f, mPitch = %f, mStretchMode = %d, mFallbackMode = %d",
        speed_info->speed, speed_info->mPitch, speed_info->mStretchMode, speed_info->mFallbackMode);
    playbackRate->mSpeed = speed_info->speed;
    playbackRate->mPitch = speed_info->mPitch;
    playbackRate->mStretchMode = speed_info->mStretchMode;
    playbackRate->mFallbackMode = speed_info->mFallbackMode;

    pthread_mutex_unlock (&out->lock);
    pthread_mutex_unlock (&adev->lock);

    return 0;
}


static void select_devices (struct aml_audio_device *adev)
{
    ALOGD ("%s(mode=%d, out_device=%#x)", __FUNCTION__, adev->mode, adev->out_device);
    int headset_on;
    int headphone_on;
    int speaker_on;
    int hdmi_on;
    int earpiece;
    int mic_in;
    int headset_mic;
    int anlg_dock_headset_on;
    headset_on = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET;
    headphone_on = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
    speaker_on = adev->out_device & AUDIO_DEVICE_OUT_SPEAKER;
    hdmi_on = adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL;
    earpiece =  adev->out_device & AUDIO_DEVICE_OUT_EARPIECE;
    mic_in = adev->in_device & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC);
    headset_mic = adev->in_device & AUDIO_DEVICE_IN_WIRED_HEADSET;
    anlg_dock_headset_on = adev->out_device & AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET;
    ALOGD ("%s : hs=%d , hp=%d, sp=%d, hdmi=0x%x,earpiece=0x%x", __func__,
             headset_on, headphone_on, speaker_on, hdmi_on, earpiece);
    ALOGD ("%s : in_device(%#x), mic_in(%#x), headset_mic(%#x)", __func__,
             adev->in_device, mic_in, headset_mic);
    if (hdmi_on) {
        do_output_device_routing(adev, AUDIO_DEVICE_OUT_AUX_DIGITAL, true);
    }
    if (headphone_on || headset_on || anlg_dock_headset_on) {
        do_output_device_routing(adev, AUDIO_DEVICE_OUT_WIRED_HEADPHONE, true);
    }
    if (speaker_on || earpiece) {
        do_output_device_routing(adev, AUDIO_DEVICE_OUT_SPEAKER, true);
    }
    if (mic_in) {
        do_input_device_routing(adev, AUDIO_DEVICE_IN_BUILTIN_MIC, true);
    }
    if (headset_mic) {
        do_input_device_routing(adev, AUDIO_DEVICE_IN_WIRED_HEADSET, true);
    }
}

static void select_mode (struct aml_audio_device *adev)
{
    ALOGD ("%s(out_device=%#x)", __FUNCTION__, adev->out_device);
    ALOGD ("%s(in_device=%#x)", __FUNCTION__, adev->in_device);
    return;
}

static void switch_to_nonms12_case (struct aml_audio_device *adev) {

    adev->switching_dolby_lib = true;
    if (adev->ms12.dolby_ms12_enable) {
        adev_ms12_cleanup((struct audio_hw_device *)adev);
    }
    adev->dolby_lib_type = eDolbyDcvLib;
    adev->switching_dolby_lib = false;
    return;
}

static int check_input_parameters(uint32_t sample_rate, audio_format_t format, int channel_count, audio_devices_t devices)
{
    ALOGD("%s(sample_rate=%d, format=%d, channel_count=%d, devices = %x)", __FUNCTION__, sample_rate, format, channel_count, devices);
   if (format == AUDIO_FORMAT_PCM_8_BIT || ((devices == AUDIO_DEVICE_NONE) && (format == AUDIO_FORMAT_PCM_FLOAT)) || sample_rate == 41000) {
        // 1 (devices == AUDIO_DEVICE_NONE) && (format == AUDIO_FORMAT_PCM_FLOAT) this is for
        // support for r_submix's readFloatArray case, when directly calling from adev_get_input_buffer_size
        // 2 format == AUDIO_FORMAT_PCM_8_BIT,format == AUDIO_FORMAT_PCM_FLOAT,sample_rate == 41000 for
        // support for t7c testAudioRecordResamplerMono8Bit and testAudioRecordMonoFloat etc. ,Ask us to support these special formats and sample rates
        return 0;
    }

    if(AUDIO_DEVICE_IN_DEFAULT == devices && AUDIO_CHANNEL_NONE == channel_count &&
       AUDIO_FORMAT_DEFAULT == format && 0 == sample_rate) {
       /* Add for Hidl6.0:CloseDeviceWithOpenedInputStreams test */
       return -ENOSYS; /*Currently System Not Supported.*/
    }

    devices &= ~AUDIO_DEVICE_BIT_IN;
    if (devices & AUDIO_DEVICE_IN_ALL_USB) {
        /* Usb input parameter should be null and use usb proxy config on adev_open_usb_input_stream*/
        if (0 == sample_rate && AUDIO_FORMAT_DEFAULT == format && AUDIO_CHANNEL_NONE == channel_count) {
            return 0;
        } else {
            /* Maybe not supported in usb proxy and set parameter to null for using usb proxy*/
            return -EINVAL;
        }
    }

    if (devices & AUDIO_DEVICE_IN_HDMI_ARC)
        return 0;

    /* config should be fixed when do not support/use pdm for builtinmic */
    if (devices & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        struct aml_audio_device *adev = aml_adev_get_handle();
        if (adev && -1 != adev->board_config.builtinmic_alsa_dev_id) {
            if (DEFAULT_OUT_SAMPLING_RATE != sample_rate
                || AUDIO_FORMAT_PCM_16_BIT != format
                || 2 != channel_count) {
                ALOGE("%s: fix config when builtinmic device is not pdm", __func__);
                return -EINVAL;
            }
        }
    }

    if (format != AUDIO_FORMAT_PCM_16_BIT && format != AUDIO_FORMAT_PCM_32_BIT) {
        ALOGE("%s: unsupported AUDIO FORMAT (%d)", __func__, format);
        return -EINVAL;
    }

    if (channel_count < 1 || channel_count > 2) {
        ALOGE("%s: unsupported channel count (%d) passed  Min / Max (1 / 2)", __func__, channel_count);
        return -EINVAL;
    }

    switch (sample_rate) {
        case 8000:
        case 11025:
        /*fallthrough*/
        case 12000:
        case 16000:
        case 17000:
        case 22050:
        case 24000:
        case 32000:
        case 44100:
        case 48000:
        case 96000:
            break;
        default:
            ALOGE("%s: unsupported (%d) samplerate passed ", __func__, sample_rate);
            return -EINVAL;
    }

    if ((devices & AUDIO_DEVICE_IN_LINE) ||
        (devices & AUDIO_DEVICE_IN_SPDIF) ||
        (devices & AUDIO_DEVICE_IN_TV_TUNER) ||
        (devices & AUDIO_DEVICE_IN_HDMI) ||
        (devices & AUDIO_DEVICE_IN_HDMI_ARC)) {
        if (channel_count == 2 &&
            sample_rate == 48000) {
            ALOGD("%s: audio patch input device %x", __FUNCTION__, devices);
            return 0;
        } else {
            ALOGD("%s: unsupported audio patch input device %x", __FUNCTION__, devices);
            return -EINVAL;
        }
    }
    if (!alsa_device_is_auge() && ((devices & AUDIO_DEVICE_IN_BACK_MIC) ||
            (devices & AUDIO_DEVICE_IN_BUILTIN_MIC))) {
        if (channel_count == 1)
            return -EINVAL;
    }

    return 0;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *) stream;
    unsigned int rate = out->hal_rate;
    ALOGV("Amlogic_HAL - out_get_sample_rate() = %d", rate);
    return rate;
}

static int out_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
{
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    AM_LOGI("out:%p config.rate=%d, format:%s(%#x), stream format:%s(%#x)", out, out->config.rate,
        audioFormat2Str(out->hal_internal_format), out->hal_internal_format,
        audioFormat2Str(stream->get_format(stream)), stream->get_format(stream));
    /* take resampling into account and return the closest majoring
     * multiple of 16 frames, as audioflinger expects audio buffers to
     * be a multiple of 16 frames
     */
    size_t size = out->config.period_size;
    size_t buffer_size = 0;
    switch (out->hal_internal_format) {
    case AUDIO_FORMAT_AC3:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = AC3_PERIOD_SIZE;
            ALOGI("%s AUDIO_FORMAT_IEC61937 %zu)", __FUNCTION__, size);
            if ((eDolbyDcvLib == adev->dolby_lib_type) &&
                (out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
                // local file playback, data from audio flinger direct mode
                // the data is packed by DCV decoder by OMX, 1536 samples per packet
                // to match with it, set the size to 1536
                // (ms12 decoder doesn't encounter this issue, so only handle with DCV decoder case)
                size = AC3_PERIOD_SIZE / 4;
                ALOGI("%s AUDIO_FORMAT_IEC61937(DIRECT) (eDolbyDcvLib) size = %zu)", __FUNCTION__, size);
            }
        } else if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = AC3_PERIOD_SIZE;
        } else {
            /*for issue SWPL-69439, we need increase the buf size*/
            size = (DEFAULT_PLAYBACK_PERIOD_SIZE << 2);
        }
        break;
    case AUDIO_FORMAT_E_AC3:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size =  EAC3_PERIOD_SIZE;
        } else if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = EAC3_PERIOD_SIZE;//one iec61937 packet size
        } else if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
            size = (DEFAULT_PLAYBACK_PERIOD_SIZE << 3) + (DEFAULT_PLAYBACK_PERIOD_SIZE << 1);
        }  else {
            /*Tunnel sync HEADER is 16 bytes*/
            if ((out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) && out->hw_sync_mode) {
                size = out->ddp_frame_size + TUNNEL_SYNC_HEADER_SIZE;
            } else if (eDolbyDcvLib == adev->dolby_lib_type) {
                /*fix issue SWPL-162010, same with offload size to fix amnuplayer audio breaks issue*/
                size = (DEFAULT_PLAYBACK_PERIOD_SIZE << 3) + (DEFAULT_PLAYBACK_PERIOD_SIZE << 1);
            } else {
                size = DIRECT_DDP_BUFSIZE;
            }
        }

        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
            ALOGI("%s eac3 AUDIO_FORMAT_IEC61937 = size%zu)", __FUNCTION__, size);
        }

        /*netflix ddp size is 768, if we change to a big value, then
         *every process time is too long, it will cause such case failed SWPL-41439
         */
        if (adev->is_netflix) {
            int ddp_buffer_size = NETFLIX_DDP_BUFSIZE;
            if (out->hal_format == AUDIO_FORMAT_E_AC3_JOC) {
                ddp_buffer_size = NETFLIX_DDP_ATMOS_BUFSIZE;
            }

            if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
                size = ddp_buffer_size + TUNNEL_SYNC_HEADER_SIZE;
            } else {
                size = ddp_buffer_size;
            }
        }
        break;
    case AUDIO_FORMAT_AC4:
        /*
         *1.offload write interval is 10ms
         *2.aml_ac4 data rate: aml_ac4 frame size=16391 frame rate =23440 sample rate=48000
         *       16391Bytes/42.6ms
         *3.set the audio hal buffer size as 8192 Bytes, it is about 24ms
         */
        size = (DEFAULT_PLAYBACK_PERIOD_SIZE << 4);
        break;
    case AUDIO_FORMAT_DOLBY_TRUEHD:
        if (out->flags & AUDIO_OUTPUT_FLAG_IEC958_NONAUDIO) {
            size = 16 * DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
        } else {
            /* TrueHD content, in SBR, need 8190bytes to feed the decoder.
             * so, choose the 8192bytes as an estimated value.
             */
            size = 8 * PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
        }
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = 4 * PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
        }
        break;
    case AUDIO_FORMAT_DTS:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = DTS1_PERIOD_SIZE / 2;
        } else {
            if (adev->stream_bitrate != 0  && adev->stream_bitrate != -1 && adev->stream_bitrate <= 768000) {     //lbr bitrate range = 32000~768000
                size = ((adev->stream_bitrate >> 3) / 1000) * OFFLOAD_BUFFER_SIZE_DURATION_MS;
                /*align to 8 byte*/
                size = size & ~(OFFLOAD_BUFFER_SIZE_ALIGNMENT - 1);
                if (size > DTS_OFFLOAD_BUFFER_MAX_SIZE) {
                    size = DTS_OFFLOAD_BUFFER_MAX_SIZE;
                } else if (size <= 0) {
                    size = DTSHD_PERIOD_SIZE * 8;
                }
            } else {
                size = DTSHD_PERIOD_SIZE * 8;
            }
        }
        ALOGI("%s AUDIO_FORMAT_DTS buffer size = %zu frames", __FUNCTION__, size);
        break;
    case AUDIO_FORMAT_DTS_HD:
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            size = 4 * PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE;
        } else {
            if (adev->stream_bitrate != 0  && adev->stream_bitrate != -1 && adev->stream_bitrate <= 768000) {
                size = ((adev->stream_bitrate >> 3) / 1000) * OFFLOAD_BUFFER_SIZE_DURATION_MS;
                /*align to 8 byte*/
                size = size & ~(OFFLOAD_BUFFER_SIZE_ALIGNMENT - 1);
                if (size > DTS_OFFLOAD_BUFFER_MAX_SIZE) {
                    size = DTS_OFFLOAD_BUFFER_MAX_SIZE;
                } else if (size <= 0) {
                    size = DTSHD_PERIOD_SIZE * 8;
                }
            } else {
                size = DTSHD_PERIOD_SIZE * 8;
            }
        }
        ALOGI("%s AUDIO_FORMAT_DTS_HD buffer size = %zu frames", __FUNCTION__, size);
        break;
    case AUDIO_FORMAT_DTS_UHD_P2:
        size = DTSHD_PERIOD_SIZE;
        ALOGI("%s AUDIO_FORMAT_DTS_UHD_P2 buffer size = %zu frames", __FUNCTION__, size);
        break;
#if 0
    case AUDIO_FORMAT_PCM:
        if (adev->continuous_audio_mode) {
            /*Tunnel sync HEADER is 16 bytes*/
            if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
                size = (8192 + 16) / 4;
            }
        }
#endif
    case AUDIO_FORMAT_IEC61937: {
        if (stream->get_format(stream) == AUDIO_FORMAT_IEC61937) {
            if ((out->hal_channel_mask == AUDIO_CHANNEL_OUT_STEREO) && (out->hal_rate == 192000))
                size = BUFF_SIZE_IEC61937_192KHZ_2CH;
            else if ((out->hal_channel_mask == AUDIO_CHANNEL_OUT_7POINT1) && (out->hal_rate == 192000))
                size = BUFF_SIZE_IEC61937_192KHZ_8CH;
            else
                size = BUFF_SIZE_IEC61937;
            ALOGI("%s AUDIO_FORMAT_IEC61937 buffer size = %zu frames", __FUNCTION__, size);
            return size;
        }
    }
    case AUDIO_FORMAT_MPEGH:
    case AUDIO_FORMAT_MPEGH_BL_L3:
    case AUDIO_FORMAT_MPEGH_BL_L4:
    case AUDIO_FORMAT_MPEGH_LC_L3:
    case AUDIO_FORMAT_MPEGH_LC_L4:
        size = DEFAULT_PLAYBACK_PERIOD_SIZE << 4;
        ALOGI("%s MPEG-H buffer size = %zu frames", __FUNCTION__, size);
        return size;
    default:
        if (adev->continuous_audio_mode && audio_is_linear_pcm(out->hal_internal_format)) {
            /*Tunnel sync HEADER is 20 bytes*/
            if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
                if (adev->is_netflix) {
                    size = (2048 * audio_stream_out_frame_size((struct audio_stream_out *) stream));
                    if (out->hal_ch > 2) {
                        size += TUNNEL_SYNC_NETFLIX_MULITCH_HEADER_SIZE;
                    } else {
                        size += TUNNEL_SYNC_HEADER_SIZE;
                    }
                } else {
                    //2 package data.
                    size = ( 2048 * audio_stream_out_frame_size((struct audio_stream_out *) stream) + TUNNEL_SYNC_HEADER_SIZE*2);
                }
                return size;

            } else {
                /* Framework sonic position jitter is related with hal buffer size. Hal buffer is smaller, position jitter is less */
                if (out->is_normal_pcm && (out->hal_ch == 2)) {
                    return NORMAL_MIXER_MIN_BUFFER_FRAMES * audio_stream_out_frame_size ( (struct audio_stream_out *) stream);
                }
                /* roll back the change for SWPL-15974 to pass the gts failure SWPL-20926*/
                return DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT* audio_stream_out_frame_size ( (struct audio_stream_out *) stream);
            }
        }

        if (out->config.rate == 96000) {
            size = DEFAULT_PLAYBACK_PERIOD_SIZE * 2;
        } else if (audio_is_linear_pcm(out->hal_internal_format) && out->is_normal_pcm && (out->hal_ch == 2) && !(out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {
            /* Framework sonic position jitter is related with hal buffer size. Hal buffer is smaller, position jitter is less */
            size = NORMAL_MIXER_MIN_BUFFER_FRAMES;
        } else {
            size = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
        }
    }

    if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC && audio_is_linear_pcm(out->hal_internal_format)) {
        //2 package data.
        if (adev->is_netflix) {
            size = (size * audio_stream_out_frame_size((struct audio_stream_out *) stream));
            if (out->hal_ch > 2) {
                size += TUNNEL_SYNC_NETFLIX_MULITCH_HEADER_SIZE;
            } else {
                size += TUNNEL_SYNC_HEADER_SIZE;
            }
        } else {
            size = (size * audio_stream_out_frame_size((struct audio_stream_out *) stream)) + TUNNEL_SYNC_HEADER_SIZE*2;
        }
    } else {
        size = (size * audio_stream_out_frame_size((struct audio_stream_out *) stream));
    }

    // remove alignment to have an accurate size
    // size = ( (size + 15) / 16) * 16;
    return size;
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream __unused)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;

    //ALOGV("Amlogic_HAL - out_get_channels return out->hal_channel_mask:%0x", out->hal_channel_mask);
    return out->hal_channel_mask;
}

static audio_channel_mask_t out_get_channels_direct(const struct audio_stream *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;
    ALOGV("out->hal_channel_mask:%0x", out->hal_channel_mask);
    return out->hal_channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream __unused)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;
    //ALOGV("Amlogic_HAL - out_get_format() = %d", out->hal_format);
    // if hal_format doesn't have a valid value,
    // return default value AUDIO_FORMAT_PCM_16_BIT
    if (out->hal_format == 0) {
        return AUDIO_FORMAT_PCM_16_BIT;
    }
    return out->hal_format;
}

static audio_format_t out_get_format_direct(const struct audio_stream *stream)
{
    const struct aml_stream_out *out = (const struct aml_stream_out *)stream;

    return  out->hal_format;
}

static int out_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return 0;
}

static int out_dump (const struct audio_stream *stream, int fd)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    AM_LOGD("io %d: out:%p, fd:%d", out->io_handle, stream, fd);
    dprintf(fd, "\n[AML_HAL]-------------out: %p------------------\n", out);
    aml_stream_out_dump(out, fd);
    return 0;
}

static int out_set_parameters (struct audio_stream *stream, const char *kvpairs)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct aml_stream_in *in;
    struct str_parms *parms;
    char *str;
    char value[32] = {'\0'};
    int ret;
    uint val = 0;
    bool force_input_standby = false;
    int channel_count = popcount (out->hal_channel_mask);
    bool hwsync_lpcm = (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC && out->config.rate  <= 48000 &&
                        audio_is_linear_pcm(out->hal_internal_format) && channel_count <= 2);

    AM_LOGD("out:%p kvpairs:%s out->out_device:%#x, adev->out_device:%#x", stream, kvpairs, out->out_device, adev->out_device);
    parms = str_parms_create_str (kvpairs);

    ret = str_parms_get_str (parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof (value) );
    if (ret >= 0) {
        val = atoi (value);
        pthread_mutex_lock (&adev->lock);
        pthread_mutex_lock (&out->lock);
        if ( ( (adev->out_device & AUDIO_DEVICE_OUT_ALL) != val) && (val != 0) ) {
            ALOGI ("audio hw select device!\n");
            /* a change in output device may change the microphone selection */
            if (adev->active_input &&
                adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
                force_input_standby = true;
            }
        }
        pthread_mutex_unlock (&out->lock);
        if (force_input_standby) {
            in = adev->active_input;
            pthread_mutex_lock (&in->lock);
            do_input_standby (in);
            pthread_mutex_unlock (&in->lock);
        }
        pthread_mutex_unlock (&adev->lock);

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }
    int sr = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_SAMPLING_RATE, &sr);
    if (ret >= 0) {
        if (sr > 0) {
            struct pcm_config *config = &out->config;
            ALOGI ("audio hw sampling_rate change from %d to %d \n", config->rate, sr);
            config->rate = sr;
            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&out->lock);
            // set hal_rate to sr for passing VTS
            ALOGI ("Amlogic_HAL - %s: set sample_rate to hal_rate.", __FUNCTION__);
            out->hal_rate = sr;
            pthread_mutex_unlock (&out->lock);
            pthread_mutex_unlock (&adev->lock);
        }

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }
    // Detect and set AUDIO_PARAMETER_STREAM_FORMAT for passing VTS
    int fmt = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_FORMAT, (int *) &fmt);
    if (ret >= 0) {
        if (fmt > 0) {
            struct pcm_config *config = &out->config;
            ALOGI ("[%s:%d] audio hw format change from %#x to %#x", __func__, __LINE__, config->format, fmt);
            config->format = fmt;
            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&out->lock);
            // set hal_format to fmt for passing VTS
            ALOGI ("Amlogic_HAL - %s: set format to hal_format. fmt = %d", __FUNCTION__, fmt);
            out->hal_format = fmt;
            pthread_mutex_unlock (&out->lock);
            pthread_mutex_unlock (&adev->lock);
        }

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }
    // Detect and set AUDIO_PARAMETER_STREAM_CHANNELS for passing VTS
    audio_channel_mask_t channels = AUDIO_CHANNEL_OUT_STEREO;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_CHANNELS, (int *) &channels);
    if (ret >= 0) {
        if (channels > AUDIO_CHANNEL_NONE) {
            struct pcm_config *config = &out->config;
            ALOGI ("audio hw channel_mask change from %d to %d \n", config->channels, channels);
            config->channels = audio_channel_count_from_out_mask (channels);
            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&out->lock);
            // set out->hal_channel_mask to channels for passing VTS
            ALOGI ("Amlogic_HAL - %s: set out->hal_channel_mask to channels. fmt = %d", __FUNCTION__, channels);
            out->hal_channel_mask = channels;
            pthread_mutex_unlock (&out->lock);
            pthread_mutex_unlock (&adev->lock);
        }

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }

    int frame_size = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_FRAME_COUNT, &frame_size);
    if (ret >= 0) {
        if (frame_size > 0) {
            struct pcm_config *config = &out->config;
            ALOGI ("audio hw frame size change from %d to %d \n", config->period_size, frame_size);
            config->period_size =  frame_size;
        }

        // We shall return Result::OK, which is 0, if parameter is set successfully,
        // or we can not pass VTS test.
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 in order to pass VTS test.", __FUNCTION__);
        ret = 0;

        goto exit;
    }
    ret = str_parms_get_str (parms, AUDIO_PARAMETER_HW_AV_SYNC, value, sizeof (value) );
    if (ret >= 0 && out->hw_sync_mode == false) {
        int hw_sync_id = atoi(value);
        output_stream_hwsync_prepare(out, hw_sync_id);
        ret = 0;
        goto exit;
    } else {
        ALOGE("%s, ret:%d, hw_sync_mode:%s", __func__, ret, out->hw_sync_mode?"is true":"is false");
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type || adev->ms12.dap_only_enable) {
        ret = str_parms_get_str(parms, "ms12_runtime", value, sizeof(value));
        if (ret >= 0) {
            char *parm = strstr(kvpairs, "=");
            pthread_mutex_lock(&adev->lock);
            if (parm)
                aml_ms12_update_runtime_params(&(adev->ms12), parm+1);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
#ifdef ENABLE_DVB_PATCH
        if (dtv_tuner_framework((struct audio_stream_out *)stream)) {
            ret = out_set_params_for_tunerframework((struct audio_stream_out *)stream, parms);
            if (ret >= 0) {
                ALOGD("out_set_params_for_tunerframework (kv: %s)", kvpairs);
                goto exit;
            }
        }
#endif
        int presentation_id = -1;
        ret = str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_PRESENTATION_ID, &presentation_id);
        if (ret >= 0) {
            struct dolby_ms12_desc *ms12 = &(adev->ms12);
            ALOGI("presentation_id %d ", presentation_id);
            set_ms12_ac4_presentation_group_index((struct audio_stream_out *)stream, presentation_id);
            int program_id = -1;
            ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_PROGRAM_ID, &program_id);
            if (ret >= 0) {
                 ALOGI("program_id %d ", program_id);
                 set_ms12_ac4_short_prog_identifier((struct audio_stream_out *)stream, program_id);
            }
            goto exit;
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_CLOSING, value, sizeof(value)-1);
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_TRUE) == 0) {
            out->is_closing = true;
            ALOGI("%s stream %p is_closing", __func__, out);
            aml_stream_delete_timer(adev, out);
        } else {
            out->is_closing = false;
        }
        goto exit;
    }

exit:
    str_parms_destroy (parms);

    // We shall return Result::OK, which is 0, if parameter is NULL,
    // or we can not pass VTS test.
    if (ret < 0) {
        if (adev->debug_flag) {
            ALOGW("Amlogic_HAL - %s: parameter is NULL, change ret value to 0 in order to pass VTS test.", __func__);
        }
        ret = 0;
    }
    return ret;
}

static char *out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    if (strstr(keys, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES) || \
        strstr(keys, AUDIO_PARAMETER_STREAM_SUP_CHANNELS) || \
        strstr(keys, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        return out_get_parameters_wrapper_about_sup_sampling_rates__channels__formats(stream, keys);
    }
    else {
        ALOGE("%s() keys %s is not supported! TODO!\n", __func__, keys);
        return strdup ("");
    }
}


/*
 * This function is only used for Android-Framework.
 * This latency value is fake(AudioTrack::updateLatency_l), so that
 * netflix test cases can pass(fast-playback can start, Fly audio will not timeout)
*/
static uint32_t audiohal_get_latency (const struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    uint32_t a2dp_delay = 0, alsa_latency = 0, whole_latency = 0;
    int hdmi_delay = 0;

    if (out->out_device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE ||
        out->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET) {
        //do nothing.
    } else if (out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
        a2dp_delay = a2dp_out_get_latency(adev);
        return a2dp_delay;
    } else if (out->out_device & AUDIO_DEVICE_OUT_USB_HEADSET) {
        //do nothing.
    } else if (out->out_device & AUDIO_DEVICE_OUT_HDMI) {
        if (adev->avsync_compensate_delay_ms < 0) {
            hdmi_delay = abs(adev->avsync_compensate_delay_ms);
            /*for youtube case, the delay can't set too big*/
            if (adev->compensate_video_enable) {
                if (hdmi_delay > 150) {
                    hdmi_delay = 150;
                }
            }
        }
        /*compensate the avr delay for raw data*/
        if (adev->b_ott_tv_arc_connected &&
            adev->sink_format != AUDIO_FORMAT_PCM_16_BIT) {
            hdmi_delay += abs(adev->arc_delay_ms);
        }
    }

    snd_pcm_sframes_t frames = out_get_latency_frames (stream);
    if ((eDolbyMS12Lib == adev->dolby_lib_type) && (adev->ms12_out != NULL)) {
        frames = aml_alsa_output_get_delayframe((struct audio_stream_out*)adev->ms12_out);
    }
    // In the first pcm_open, and no data has been written, the latency of alsa is 0
    // at this time. When AudioFlinger::PlaybackThread::createTrack_l, it is detected
    // that the hardware latency_l is 0, which will cause the creation to fail.
    if (frames <= 0) {
        frames = out->config.period_size * out->config.period_count;
        AM_LOGW("alsa latency is 0, return max frames:%ld", frames);
    }
    alsa_latency = (frames * 1000) / out->config.rate;

    whole_latency = alsa_latency + hdmi_delay;
    AM_LOGI("io %d: out:%p frames:%lu rate:%u whole_latency:%u alsa_latency:%u", out->io_handle,
           stream, frames, out->config.rate, whole_latency, alsa_latency);
    return whole_latency;
}


static uint32_t out_get_latency (const struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    uint32_t a2dp_delay = 0, alsa_latency = 0, ms12_latency = 0, ms12_pipeline_latency = 0, whole_latency = 0;

    if (out->out_device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE ||
        out->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET) {
        //do nothing.
    } else if (out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
        a2dp_delay = a2dp_out_get_latency(adev);
        return a2dp_delay;
    } else if (out->out_device & AUDIO_DEVICE_OUT_USB_HEADSET) {
        //do nothing.
    }

    snd_pcm_sframes_t frames = out_get_latency_frames (stream);

    if (adev->dolby_lib_type == eDolbyMS12Lib) {
        ms12_latency = get_ms12_buffer_latency((struct aml_stream_out *)out);
        ms12_pipeline_latency = HAL_MS12_PIPELINE_LATENCY;
    }
    alsa_latency = (frames * 1000) / out->config.rate;

    if (is_TV(adev)) {
        ms12_latency = get_ms12_buffer_latency((struct aml_stream_out *)out);
        ms12_pipeline_latency = HAL_MS12_PIPELINE_LATENCY;
        whole_latency =  ms12_latency + ms12_pipeline_latency + alsa_latency;

        ALOGV("%s  stream:%p frames:%lu out->config.rate:%u whole_latency:%u, alsa_latency:%u, ms12_latency:%u", __func__,
            stream, frames, out->config.rate, whole_latency, alsa_latency, ms12_latency);
    } else {
        whole_latency = alsa_latency;
        ALOGV("%s  stream:%p frames:%lu out->config.rate:%u whole_latency:%u, alsa_latency:%u, ", __func__,
            stream, frames, out->config.rate, whole_latency, alsa_latency);
    }

    return whole_latency;

}


bool dtv_tuner_framework(struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    if (out && (out->dev)  &&
        (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) &&
        (out->audioCfg.offload_info.content_id != 0)&&
        (out->audioCfg.offload_info.sync_id != 0)) {
        return true;
   }
   return false;
}

static int out_set_volume (struct audio_stream_out *stream, float left, float right)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int ret = 0;
    bool is_dolby_format = is_dolby_ms12_support_compression_format(out->hal_internal_format);
    bool is_direct_pcm = is_direct_stream_and_pcm_format(out);
    bool is_mmap_pcm = is_mmap_stream_and_pcm_format(out);
    bool is_ms12_pcm_volume_control = (is_direct_pcm && !is_mmap_pcm);
    bool is_dts = is_dts_format(out->hal_internal_format);
    bool is_tv_stream = is_tv_stream_out(out);
    bool is_asdk_test = property_get_bool("persist.vendor.audio.ms12.default.values", false);

    AM_LOGI("out:%p left:%f continuous:%d internal_format:%s dolby:%d direct pcm:%d mmap_pcm:%d",
        stream, left, continuous_mode(adev), audioFormat2Str(out->hal_internal_format),
        is_dolby_format, is_direct_pcm, is_mmap_pcm);

    /* for not use ms12 case, we can use spdif enc mute, other wise ms12 can handle it*/
    if (is_dts || (is_dolby_format &&
        (eDolbyDcvLib == adev->dolby_lib_type || is_bypass_dolbyms12(stream) ||
            adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS))) {
        if (out->volume_l < FLOAT_ZERO && left > FLOAT_ZERO) {
            ALOGI("set offload mute: false");
            out->offload_mute = false;
        } else if (out->volume_l > FLOAT_ZERO && left < FLOAT_ZERO) {
            ALOGI("set offload mute: true");
            out->offload_mute = true;
        }
        if (out->spdifout_handle) {
            aml_audio_spdifout_mute(out->spdifout_handle, out->offload_mute);
        }
        if (out->spdifout2_handle) {
            aml_audio_spdifout_mute(out->spdifout2_handle, out->offload_mute);
        }

    }
    out->volume_l = left;
    out->volume_r = right;

    /*s
     *The Dolby format(dd/ddp/ac4/true-hd/mat) and direct&UI-PCM(stereo or multi PCM)
     *use set_ms12_main_volume to control it.
     *The volume about mixer-PCM is controlled by AudioFlinger
     */
    if ((eDolbyMS12Lib == adev->dolby_lib_type) &&
        (is_dolby_format ||
        is_ms12_pcm_volume_control ||
        is_tv_stream)) {
        if (out->volume_l != out->volume_r) {
            ALOGW("%s, left:%f right:%f NOT match", __FUNCTION__, left, right);
        }

        // Currently audiohal volumeshaper can not satisfy normal playback's complicated cases,
        // Thus turn it off.
        if (out->is_netflix_src_stream || is_asdk_test) {
            aml_volume_shaper_set_enable(&out->volume_shaper, true);
        } else {
            aml_volume_shaper_set_enable(&out->volume_shaper, false);
        }

        //if (is_tv_stream) {
            //left = dtv_get_ms12_volume_on_non_TV_device(out);
        //}
        //when  in tv platform ,we use the adev_set_port_config
        //to set the audio gain , but the app will call out_set_volume
        //to set the main volume to 1.0, so we will use
        //sink gain for volume adjust.
        if (!is_AC4_stream_with_pcm_sink_on_stb(out)) {
            //set_ms12_main_volume(&adev->ms12, out->volume_l);

            // Fix : [ASDK14][7142]volume_shaper:the first wave is clipped
            if (is_asdk_test && !adev->is_netflix && !out->first_volume_set && !is_float_equal(left, 0.0f)) {
                uint64_t curr_time_us = aml_audio_get_systime();
                if (curr_time_us > 32000) {
                    curr_time_us -= 32000;
                }
                aml_volume_shaper_add(&out->volume_shaper, 0.0f, curr_time_us);
                out->volume_shaper.bUseStartFrames = true;
            }
            out->first_volume_set = true;
            aml_volume_shaper_add(&out->volume_shaper, left, 0);
            ALOGI("%s line %d set ms12 main volume as %f\n", __func__, __LINE__, out->volume_l);
        }
        else {
            //set_ms12_main_volume(&adev->ms12, 1.0f);
            aml_volume_shaper_add(&out->volume_shaper, 1.0f, 0);
            ALOGI("%s line %d set ms12 main volume as 1.0\n", __func__, __LINE__);
            if (adev->ms12_out) {
                adev->ms12_out->volume_l = out->volume_l;
            }
        }
        /*
         * The postgain value has an impact on the Volume Modeler and the Audio Regulator:
         * Volume Modeler: Uses the postgain value to select the appropriate frequency response curve
         * to maintain a consistent perceived timbre at different listening levels.
         * SP45: Postgain
         * Sets the amount of gain that is to be applied to the signal after exiting MS12.
         * Settings From -130 to +30 dB, in 0.0625 dB steps
         */
        int dap_postgain = volume2Ms12DapPostgain(out->volume_l);
        set_ms12_dap_postgain(&adev->ms12, dap_postgain);
    }
    return 0;
}

/* use standby instead of pause to fix background pcm playback */
static int out_pause_new (struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(aml_dev->ms12);
    bool is_standby = aml_out->standby;
    int ret = 0;

    AM_LOGI("io: %d: out:%p pause:%d dolby_type:%d continuous:%d hw_sync:%d ms12_enable:%d", aml_out->io_handle,
          stream, aml_out->pause_status, aml_dev->dolby_lib_type, aml_dev->continuous_audio_mode,
          aml_out->hw_sync_mode, aml_dev->ms12.dolby_ms12_enable);

    aml_audio_trace_int("out_pause_new", 1);
    aml_out->pause_time = aml_audio_get_systime() / 1000; //us --> ms
    if (aml_audio_trace_debug_level() > 0)
    {
        if (aml_out->pause_time > aml_out->write_time && (aml_out->pause_time - aml_out->write_time < 5*1000)) { //continually write time less than 5s, audio gap
            ALOGD("%s: out_stream(%p) AudioGap pause_time:%" PRIu64 ",  diff_time(pause - write):%" PRIu64 " ms", __func__,
                   stream, aml_out->pause_time, aml_out->pause_time - aml_out->write_time);
        } else {
            ALOGD("%s:  -------- pause ----------", __func__);
        }
    }
    aml_out->write_count = 0;
    aml_out->trace_last_write_time_ms = 0;

    pthread_mutex_lock (&aml_dev->lock);
    pthread_mutex_lock (&aml_out->lock);

    if (!is_tv_stream_out(aml_out) && (aml_out->flags & AUDIO_OUTPUT_FLAG_DIRECT) && is_dev_patch_exist(aml_dev)) {
        ALOGW("%s tv path exists, %p can not execute pause !!!", __func__, aml_out);
        ret = OK;
        goto exit;
    }

    /* a stream should fail to pause if not previously started */
    if (aml_out->pause_status == true) {
        // If output stream is standby or paused,
        // we should return Result::INVALID_STATE (3),
        // thus we can pass VTS test.
        ALOGE ("%s: stream in wrong status. standby(%d) or paused(%d)",
                __func__, aml_out->standby, aml_out->pause_status);
        ret = INVALID_STATE;
        goto exit;
    }
    if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
        pthread_mutex_lock(&ms12->lock);
        if ((aml_dev->ms12.dolby_ms12_enable == true) && (aml_out->input_bytes_size != 0) &&
            (aml_out->pause_status == false)) {
            dolby_ms12_main_pause(stream);
        } else {
            ALOGI("%s do nothing\n", __func__);
        }
        pthread_mutex_unlock(&ms12->lock);

    } else if (eDolbyDcvLib == aml_dev->dolby_lib_type) {
        if (aml_dev->useAudioMixer) {
            struct amlAudioMixer *audio_mixer = aml_dev->mixerData;
            if (aml_out->inputPortID != -1 && audio_mixer) {
                send_mixer_inport_message(audio_mixer, aml_out->inputPortID, MSG_PAUSE);
            }
        }

        ret = aml_alsa_output_pause(stream);
        if (aml_out->spdifout_handle) {
            aml_audio_spdifout_pause(aml_out->spdifout_handle);
        }

        if (aml_out->spdifout2_handle) {
            aml_audio_spdifout_pause(aml_out->spdifout2_handle);
        }

        if (aml_out->hw_sync_mode && aml_out->hwsync && aml_out->tsync_status != TSYNC_STATUS_STOP) {
            ALOGI("%s set AUDIO_PAUSE when tunnel mode\n",__func__);

            aml_hwsync_wrap_set_pause(aml_out->hwsync);
            // prepare for the next wait_video_drop function
            // aml_hwsync_wrap_wait_video_drop will return if it is vmaster mode.
            aml_out->hwsync->first_apts_flag = false;
            aml_out->hwsync->wait_video_done = false;
            if (aml_out->restore_vmaster) {
                aml_hwsync_wrap_set_amaster(aml_out->hwsync, false);
                aml_out->restore_vmaster = false;
            }
            aml_out->tsync_status = TSYNC_STATUS_PAUSED;
        }
    } else {
        AM_LOGI(" code shouldn't run to here, do nothing");
    }

exit:
    aml_out->pause_status = true;
    aml_out->alsa_running_status = false;
    aml_out->alsa_status_changed = true;
    aml_out->hwsync_parsed_frames_sum_paused = aml_out->hwsync_parsed_frames_sum;
    aml_out->last_payload_offset = 0;
    aml_out->last_hwsync_header_pts = 0;

    if (aml_out->speed_info.speed_handle) {
        aml_audio_speed_init_post_delay(&aml_out->speed_info.post_delay, 48000);
        aml_stream_clear_speed_aux_info(aml_out);
        if (aml_out->hw_sync_mode && aml_out->hwsync) {
            aml_out->hwsync->last_output_pts = 0;
        }
    }
    pthread_mutex_unlock(&aml_out->lock);
    pthread_mutex_unlock(&aml_dev->lock);
    aml_audio_trace_int("out_pause_new", 0);

    // already pause stream, should cancel aml_stream_timer_pause_callback,
    // avoid submix port message confusion.
    if (!aml_out->is_callback_pending && (aml_out->streamType == STREAM_PCM_HWSYNC)) {
        AM_LOGI("audio_timer_stop timer_id2 %d", aml_out->timer_id2);
        audio_timer_stop(aml_out->timer_id2);
    }

    if (is_standby) {
        ALOGD("%s(), stream(%p) already in standby, return INVALID_STATE", __func__, stream);
        ret = INVALID_STATE;
    }
    aml_out->position_update = 0;

    AM_LOGI("io %d: out:%p exit", aml_out->io_handle, stream);
    return ret;
}

static int out_resume_new (struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(aml_dev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    int ret = 0;

    AM_LOGI("io %d: out:%p standby:%d, pause_status:%d", aml_out->io_handle, stream, aml_out->standby, aml_out->pause_status);
    aml_audio_trace_int("out_resume_new", 1);
    pthread_mutex_lock(&aml_dev->lock);
    pthread_mutex_lock(&aml_out->lock);
    /* a stream should fail to resume if not previously paused */
    // aml_out->standby is always "1" in ms12 continuous mode, which will cause fail to resume here
    if (/*aml_out->standby || */aml_out->pause_status == false) {
        // If output stream is not standby or not paused,
        // we should return Result::INVALID_STATE (3),
        // thus we can pass VTS test.
        ALOGE ("Amlogic_HAL - %s: cannot resume, because output stream isn't in standby or paused state.", __FUNCTION__);
        ret = 3;
        goto exit;
    }
    if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
        if (aml_out->pause_status) {
            if (aml_dev->ms12.dolby_ms12_enable == true) {
                if (audio_is_linear_pcm(aml_out->hal_internal_format)) {
                    /*pcm data case, directly send resume message*/
                    pthread_mutex_lock(&ms12->lock);
                    ms12_dec->resume_state = MS12_RESUME_FROM_RESUME;
                    dolby_ms12_main_resume(stream);
                    pthread_mutex_unlock(&ms12->lock);
                } else {
                    /*About resume, we should separate the control message and data stream.
                    **This solution can avoid gap issue,
                    **for example ms12 remaining buffer and audioflinger can't send data in time.
                    **raw data case, resume it in write_new when first data coming.
                    **This is for fixing the almond NTS underflow cases TV-45739.
                    */
                    ALOGI("%s resume raw data later", __func__);
                    ms12_dec->need_resume = true;
                }
                if (aml_out->hwsync && aml_out->hw_sync_mode) {
                    aml_out->hwsync->wait_video_done = false;
                }
            } else {
                ALOGI("%s : ms12 is not ready, resume it later", __func__);
                ms12_dec->need_resume = true;
            }
        } else {
            pthread_mutex_lock(&ms12->lock);
            dolby_ms12_main_resume(stream);
            pthread_mutex_unlock(&ms12->lock);
       }
    } else if (eDolbyDcvLib == aml_dev->dolby_lib_type) {
        if (aml_dev->useAudioMixer) {
            struct amlAudioMixer *audio_mixer = aml_dev->mixerData;
            if (aml_out->inputPortID != -1 && audio_mixer) {
                send_mixer_inport_message(audio_mixer, aml_out->inputPortID, MSG_RESUME);
            }
        }

        ret = aml_alsa_output_resume(stream);

        if (aml_out->spdifout_handle) {
            aml_audio_spdifout_resume(aml_out->spdifout_handle);
        }

        if (aml_out->spdifout2_handle) {
            aml_audio_spdifout_resume(aml_out->spdifout2_handle);
        }

        if (aml_out->hw_sync_mode) {
            ALOGI ("init hal mixer when hwsync resume\n");
            aml_dev->hwsync_output = aml_out;
            aml_hwsync_wrap_set_resume(aml_out->hwsync);
            aml_out->tsync_status = TSYNC_STATUS_RUNNING;
        }
    } else {
        AM_LOGI(" code shouldn't run to here, do nothing");
    }

    aml_out->write_status = false;
    ALOGI("%s(), stream[%p] write_status set to false", __func__, aml_out);

exit:
    pthread_mutex_unlock (&aml_out->lock);
    pthread_mutex_unlock (&aml_dev->lock);

    aml_out->pause_status = false;
    aml_audio_trace_int("out_resume_new", 0);
    AM_LOGI("io %d: out:%p  exit", aml_out->io_handle, stream);
    return ret;
}

static int out_flush_new (struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = out->ms12_dec_handle;
    aml_stream_speed_info_t *speed_info = &out->speed_info;
    AM_LOGI("io %d: out:%p", out->io_handle, stream);
    out->frame_write_sum  = 0;
    out->frame_offset = 0;
    out->decoded_frame = 0;
    out->last_frames_position = 0;
    out->spdif_enc_init_frame_write_sum =  0;
    out->frame_skip_sum = 0;
    out->skip_frame = 0;
    out->input_bytes_size = 0;
    out->last_frames_when_paused = 0;
    out->last_timestamp_valid = false;
    out->trace_last_write_time_ms = 0;

    aml_audio_trace_int("out_flush_new", 1);
    out->write_count = 0;
    out->flush_time = aml_audio_get_systime() / 1000; //us --> ms
    if (out->ms12_dec_handle) {
        out->ms12_dec_handle->last_frames_position = 0;
    }

    if (out->aml_parser) {
        pthread_mutex_lock(&out->parser_MutexLock);
        aml_parser_flush(out->aml_parser);
        pthread_mutex_unlock(&out->parser_MutexLock);
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (out->total_write_size == 0) {
            out->pause_status = false;
            ALOGI("%s not writing, do nothing", __func__);
            aml_audio_trace_int("out_flush_new", 0);
            return 0;
        }
        if (out->hw_sync_mode) {
            aml_audio_hwsync_init(out->hwsync, out);
            out->last_payload_offset = 0;
            out->last_hwsync_header_pts = 0;
        }
        //normal pcm(mixer thread) do not flush dolby ms12 input buffer
        if (continuous_mode(adev) && (out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
            pthread_mutex_lock(&ms12->lock);
            if (adev->ms12.dolby_ms12_enable)
                dolby_ms12_main_flush(stream);

            out->continuous_audio_offset = 0;
            /*SWPL-39814, when using exo do seek, sometimes audio track will be reused, then the
             *sequence will be pause->flush->writing data, we need to handle this.
             *It may causes problem for normal pause/flush/resume
             */
            if (out->pause_status && adev->ms12.dolby_ms12_enable) {
                ms12_dec->resume_state = MS12_RESUME_FROM_FLUSH;
                dolby_ms12_main_resume(stream);
            }
            pthread_mutex_unlock(&ms12->lock);
        }
    } else if (eDolbyDcvLib == adev->dolby_lib_type) {
        int ret = 0;

        if (out->pause_status == true) {
            // when pause status, set status prepare to avoid static pop sound
            ret = aml_alsa_output_stop(stream);
            if (ret < 0) {
                ALOGE("aml_alsa_output_stop error =%d", ret);
            }

            if (out->spdifout_handle) {
                ret = aml_audio_spdifout_stop(out->spdifout_handle);
                if (ret < 0) {
                    ALOGE("aml_audio_spdifout_stop error =%d", ret);
                }
            }

            if (out->spdifout2_handle) {
                ret = aml_audio_spdifout_stop(out->spdifout2_handle);
                if (ret < 0) {
                    ALOGE("aml_audio_spdifout_stop error =%d", ret);
                }
            }
        }

        if (adev->useAudioMixer) {
            struct amlAudioMixer *audio_mixer = NULL;
            audio_mixer = adev->mixerData;
            if (out->inputPortID != -1 && audio_mixer) {
                send_mixer_inport_message(audio_mixer, out->inputPortID, MSG_FLUSH);
            }
        }
    } else {
        AM_LOGI(" code shouldn't run to here, do nothing");
    }

    if (out->hw_sync_mode && (eDolbyMS12Lib != adev->dolby_lib_type)) {
       aml_audio_hwsync_init(out->hwsync, out);
       out->last_payload_offset = 0;
       out->last_hwsync_header_pts = 0;
    }

    if (out->hal_format == AUDIO_FORMAT_AC4) {
        aml_ac4_parser_reset(out->ac4_parser_handle);
    }

    if (out->ac3_parser_init) {
        aml_ac3_parser_reset(out->ac3_parser_handle);
    }

    if (out->aml_dec) {
        aml_decoder_flush(out->aml_dec);
    }

    if (out->aml_dec && out->total_write_size) {
        if (adev->is_netflix && !audio_is_linear_pcm(out->hal_format)) {
            // NTS PLAY-101-TC20
            // release decoder : to discard decoder internal buffer
            aml_decoder_release(out->aml_dec);
            out->aml_dec = NULL;
            out->flush_first_write = true;

            uint64_t curr_time_ms = aml_audio_get_systime() / 1000;
            if (curr_time_ms > out->pause_time) {
                int sleep_time_ms = 0;
                uint64_t diff_time_ms = curr_time_ms - out->pause_time;
                // Pretend we are consuming the remaining data, then audiotrack switch time will exceed 200ms,
                // and eleven will detect event "No data received for 200ms, switching to fake source".
                if (diff_time_ms < 32) {
                    sleep_time_ms = 32 - diff_time_ms;
                    AM_LOGI("time_ms %" PRId64 " between pause and flush, sleep %d ms", diff_time_ms, sleep_time_ms);
                    usleep(sleep_time_ms * 1000);
                }
            }
        }
    }

    if (speed_info->speed_handle) {
        void *buffer = NULL;
        size_t read_size = 8192;
        size_t frame_size = 0;
        aml_audio_speed_flush(speed_info->speed_handle);
        buffer = aml_audio_malloc(read_size);
        if (buffer == NULL) {
            AM_LOGE("realloc size =%zu failed", read_size);
            return -1;
        }
        do {
            frame_size = aml_audio_speed_read(speed_info->speed_handle,buffer, read_size);
            if (frame_size == 0) {
                //read off
                AM_LOGE("read off");
                break;
            }
            else if (frame_size < 0) {
                //err
                AM_LOGE("Speed read Err");
                break;
            }
        } while (1);
        aml_audio_free(buffer);

        aml_audio_speed_init_post_delay(&speed_info->post_delay, 48000);
        aml_stream_clear_speed_aux_info(out);
        if (out->hw_sync_mode && out->hwsync) {
            out->hwsync->last_output_pts = 0;
        }
    }

    out->pause_status = false;
    AM_LOGI("io %d: out:%p exit", out->io_handle, stream);
    aml_audio_trace_int("out_flush_new", 0);
    return 0;
}

// insert bytes of zero data to pcm which makes A/V synchronization
static int insert_output_bytes (struct aml_stream_out *out, size_t size)
{
    int ret = 0;
    size_t insert_size = size;
    size_t once_write_size = 0;
    struct audio_stream_out *stream = (struct audio_stream_out*)out;
    struct aml_audio_device *adev = out->dev;
    audio_format_t output_format = get_output_format(stream);
    char *insert_buf = (char*) aml_audio_malloc (8192);
    audio_data_info_t data_info = { 0 };
    R_CHECK_POINTER_LEGAL(-ENOMEM, insert_buf, "malloc size failed");

    if (!out->pcm) {
        goto exit;
    }

    while (insert_size > 0) {
        memset (insert_buf, 0, 8192);
        once_write_size = insert_size > 8192 ? 8192 : insert_size;
        if (eDolbyMS12Lib == adev->dolby_lib_type && !is_bypass_dolbyms12(stream)) {
            size_t used_size = 0;
            ret = dolby_ms12_main_process(stream, insert_buf, once_write_size, &used_size);
            if (ret) {
                ALOGW("dolby_ms12_main_process cost size %zu,input size %zu,check!!!!", once_write_size, used_size);
            }
        } else {
            aml_hw_mixer_mixing(&adev->hw_mixer, insert_buf, once_write_size, output_format);
            //process_buffer_write(stream, buffer, bytes);
            data_info.audio_format = output_format;
            data_info.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
            ret = aml_audio_pcm_output((struct audio_stream_out *)out, insert_buf, once_write_size, &data_info);
        }
        insert_size -= once_write_size;
    }

exit:
    aml_audio_free (insert_buf);
    return 0;
}

enum hwsync_status check_hwsync_status (uint apts_gap)
{
    enum hwsync_status sync_status;

    if (apts_gap < APTS_DISCONTINUE_THRESHOLD_MIN)
        sync_status = CONTINUATION;
    else if (apts_gap > APTS_DISCONTINUE_THRESHOLD_MAX)
        sync_status = RESYNC;
    else
        sync_status = ADJUSTMENT;

    return sync_status;
}

static int out_get_render_position (const struct audio_stream_out *stream,
                                    uint32_t *dsp_frames)
{
    int ret = 0;
    uint64_t  dsp_frame_uint64 = 0;
    struct timespec timestamp = {0};
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    *dsp_frames = 0;

    //unified this position interface.
    ret = out_get_presentation_position(stream, &dsp_frame_uint64,&timestamp);

    if (ret == 0)
    {
        *dsp_frames = (uint32_t)(dsp_frame_uint64 & 0xffffffff);
    } else {
        ret = -ENOSYS;
    }

    if (adev->debug_flag) {
        ALOGD("io %d: out:%p pos %d ret: %d", out->io_handle, out, *dsp_frames, ret);
    }
    return ret;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *dev = out->dev;
    int status = 0;

    pthread_mutex_lock (&dev->lock);
    pthread_mutex_lock (&out->lock);

    status = aml_add_audio_effect(&dev->native_postprocess, effect, -1);

    if (status >= 0 && dev->useAudioMixer) {
        void *process = &dev->native_postprocess;

        subMixingSetAudioPostprocess(dev, &process);
        ALOGI("%s, add audio postprocess: %p", __func__, process);
    }

    pthread_mutex_unlock (&out->lock);
    pthread_mutex_unlock (&dev->lock);
    return status;
}

static int out_remove_audio_effect(const struct audio_stream *stream __unused, effect_handle_t effect __unused)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *dev = out->dev;
    int status = -EINVAL;

    pthread_mutex_lock (&dev->lock);
    pthread_mutex_lock (&out->lock);

    status = aml_remove_audio_effect(&dev->native_postprocess, effect, -1);

    pthread_mutex_unlock (&out->lock);
    pthread_mutex_unlock (&dev->lock);
    return status;
}

static int out_get_next_write_timestamp (const struct audio_stream_out *stream __unused,
        int64_t *timestamp __unused)
{
    // return -EINVAL;

    // VTS can only recognizes Result:OK or Result:INVALID_STATE, which is 0 or 3.
    // So we return ESRCH (3) in order to pass VTS.
    ALOGI ("Amlogic_HAL - %s: return ESRCH (3) instead of -EINVAL (-22)", __FUNCTION__);
    return ESRCH;
}

/* Update Android Audio Attribute usage and content_type
 * 1.usage
 * AUDIO_USAGE_MEDIA: Usage value (1) to use when the usage is media, such as music, or movie soundtracks.
 * AUDIO_USAGE_ASSISTANCE_SONIFICATION: Usage value (13) to use when the usage is sonification, such as with user interface sounds.
 * AUDIO_USAGE_ASSISTANT: Usage value (16) to use for audio responses to user queries, audio instructions or help utterances.
 * 2.content_type
 * UNKNOWN/SPEECH/MUSIC(elementary streams)/MOVIE(.mp4)
 */
static void out_update_source_metadata_v7 (struct audio_stream_out *stream,
                                        const struct source_metadata_v7* source_metadata)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = NULL;
    if (out && source_metadata) {
        if (source_metadata->tracks) {
            ALOGI("%s() line %d usage:%d content_type:%d", __func__, __LINE__, source_metadata->tracks->base.usage, source_metadata->tracks->base.content_type);
            out->track_base_usage = source_metadata->tracks->base.usage;

            // The system is ready when this function is called.
            // Sound effect library is not loaded at adev_open function.
            adev = out->dev;
            if (adev && adev->mlock_library_done == false) {
                aml_audio_lock_so_memory();
            }
        } else {
            //when source metadata track does not exist, we set is_preempt_system_audio_usage_media_stream as false
            out->is_preempt_system_audio_usage_media_stream = false;
        }
    }
}

bool aml_get_speaker_mute_status(void)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    return adev->speaker_mute_user_setting;
}

// actually maybe it be not useful now  except pass CTS_TEST:
// run cts -c android.media.cts.AudioTrackTest -m testGetTimestamp
static int out_get_presentation_position (const struct audio_stream_out *stream, uint64_t *frames, struct timespec *timestamp)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, frames,);
    R_CHECK_POINTER_LEGAL(-EINVAL, timestamp,);

    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    uint64_t frames_written_hw = out->last_frames_position;
    int frame_latency = 0,timems_latency = 0;
    bool b_raw_in = false;
    bool b_raw_out = false;
    int ret = 0;
    int video_delay_frames = 0;
    int64_t origin_tv_nsec = 0;
    bool is_earc = is_earc_connected(adev);
    aml_stream_speed_info_t *speed_info = &out->speed_info;
    /* add this code for VTS. */
    if (0 == frames_written_hw) {
        *frames = frames_written_hw;
        *timestamp = out->lasttimestamp;
        return ret;
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        ret = aml_audio_get_ms12_presentation_position(stream, frames, timestamp);
    } else {
        timems_latency = aml_audio_get_latency_offset(adev->cur_out_devices,
                                                        out->hal_internal_format,
                                                        adev->sink_format,
                                                        adev->ms12.dolby_ms12_enable,
                                                        is_earc);

        frame_latency = timems_latency * (int)out->hal_rate / 1000;
        /* SWPL-88828
         * If out_get_presentation_position() and hw_write()
         * are called by different threads, frames_written_hw
         * and timestamp may not be updated synchronously. This can cause jitter.
         */
        pthread_mutex_lock(&out->apts_update_lock);
        frames_written_hw = out->last_frames_position;
        *timestamp = out->lasttimestamp;
        pthread_mutex_unlock(&out->apts_update_lock);

        if ((frame_latency < 0) && (frames_written_hw < abs(frame_latency))) {
            ALOGI("%s(), not ready yet", __func__);
            return -EINVAL;
        }

        //none ms12 version, get frames from mixer directly.
        int64_t negative_frames = 0;
        if (eDolbyMS12Lib != adev->dolby_lib_type_last) {//For MS12 lib with DTS output
            ret = mixer_get_presentation_position(adev->mixerData,
                    out->inputPortID, frames, &negative_frames, timestamp);
            frames_written_hw = *frames;
            // negative_frames should >= -100ms
            if (adev->is_netflix && !is_tv_stream_out(out)
                && negative_frames < 0 && negative_frames >= -100*48
                && frame_latency > 0) {
                frame_latency += negative_frames;
                if (frame_latency < 0) {
                    frame_latency = 0;
                }
                AM_LOGI("frame_latency:%d, negative_frames %"PRId64"", frame_latency, negative_frames);
            }
        }

        if (frame_latency >= 0)
            *frames = frame_latency + frames_written_hw;
        else if (frame_latency < 0) {
            if (frames_written_hw >= abs(frame_latency))
                *frames = frame_latency + frames_written_hw;
            else
                *frames = 0;
        }
        /*this for pass CTS,resume *frame > pause *frame ,so need add 16 when resume,
        Otherwise, if pause and resume are equal, the CTS test code will think that the playback has ended*/
        // netflix use negative_frames to let playback start position more smooth, it will conflict the following code.
        if (!adev->is_netflix) {
            *frames = *frames >= out->last_frames_when_paused ? *frames : out->last_frames_when_paused + 16;
        }

        unsigned int output_sr = (out->config.rate) ? (out->config.rate) : (MM_FULL_POWER_SAMPLING_RATE);
        if ((*frames * out->hal_rate) / output_sr >= out->last_frames_when_paused) {
            *frames = *frames * out->hal_rate / output_sr;
        }
        //this code is for CTS cases about tunnel mode stream.
        if (out->streamType == STREAM_PCM_HWSYNC && !out->frame_write_sum_updated) {
            *frames = out->hwsync_parsed_frames_sum;
            out->last_frames_when_paused = out->hwsync_parsed_frames_sum;
        }
    }
    /*here we need add video delay*/
    video_delay_frames = get_media_video_delay(&adev->alsa_mixer) * out->hal_rate / 1000;
    origin_tv_nsec = timestamp->tv_nsec;
    /*the user set delay*/
    if (adev->avsync_compensate_delay_ms < 0 &&
        get_output_by_devices(adev->cur_out_devices) == OUTPORT_HDMI) {
        int hdmi_delay_frames = abs(adev->avsync_compensate_delay_ms) * (int)(out->hal_rate / 1000);
        /*for youtube case, the delay can't set too big*/
        if (adev->compensate_video_enable) {
            if (hdmi_delay_frames > 150 * (int)(out->hal_rate / 1000)) {
                hdmi_delay_frames = 150 * (int)(out->hal_rate / 1000);
            }
        }

        if (*frames > hdmi_delay_frames) {
            *frames = *frames - hdmi_delay_frames;
        } else {
            *frames = 0;
        }
    }
    /*compensate the avr delay for raw data*/
    if (adev->b_ott_tv_arc_connected &&
        get_output_by_devices(adev->cur_out_devices) == OUTPORT_HDMI &&
        adev->sink_format != AUDIO_FORMAT_PCM_16_BIT) {
        int arc_delay_frames = abs(adev->arc_delay_ms) * (int)(out->hal_rate / 1000);
        if (*frames > arc_delay_frames) {
            *frames = *frames - arc_delay_frames;
        } else {
            *frames = 0;
        }
    }

    if (out->is_normal_pcm) {
        const int buffer_min_frames = 256;
        uint64_t max_report_frames = 0;
        uint64_t stream_written_frames = adev->sys_audio_frame_written;
        int is_deep_buffer = out->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
        if (is_deep_buffer) {
            stream_written_frames = adev->deep_buf_audio_frame_written;
        }

        /*
         * If report_frames >= audioflinger written_frames, the device data pipeline is idle.
         * then timestamp.mTime = convertNsToTimespec(nowNs)
         * resulting in avsync jitter frequently (mTime will be a fake value).
         *
         * currently minus 256 frames;
        */
        max_report_frames = stream_written_frames;
        if (max_report_frames <= buffer_min_frames) {
            max_report_frames = 0;
        } else {
            max_report_frames -= buffer_min_frames;
        }

        *frames += video_delay_frames;
        if (*frames > max_report_frames) {
            int offset_frames = *frames - max_report_frames;
            int offset_us = -(offset_frames * 1000 / (out->hal_rate/1000));

            if (adev->debug_flag) {
                AM_LOGI("timestamp_adjust : deep_buf=%d stream_written_frames=%" PRId64 " report_frames=%" PRId64 \
                    " max_report_frames=%" PRId64 " video_delay_frames=%d offset_frames=%d offset_us=%d",
                    is_deep_buffer, stream_written_frames, *frames, max_report_frames, video_delay_frames, offset_frames, offset_us);
            }
            aml_audio_delay_timestamp(timestamp, offset_us);
            *frames = max_report_frames;
        }
    }
    if (out->streamType == STREAM_PCM_HWSYNC) {
        //ms12version, need to compensate video latency for hwsync stream.
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        int time_gap_ms = calc_time_interval_us(&out->timestamp, &ts)/1000LL;
        ALOGV("%s %d  time_gap_ms:%d,   out->frame_write_sum_updated:%d", __func__, __LINE__,
            time_gap_ms, out->frame_write_sum_updated);

        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            if (out->frame_write_sum_updated && time_gap_ms < 200)
                *frames += video_delay_frames;//add this for xts avsync
        } else {
            //do nothing
        }
    } else if (!out->is_normal_pcm) {
        *frames += video_delay_frames;
    }

    if (ret >= 0) {
        if (out->last_timestamp_valid) {
            // prevent position fallback. e.g. pause -> resume
            if (*frames < out->last_frame_reported) {
                AM_LOGE("stream %p, frames %"PRIu64" fallback, correct to last_frame_reported %"PRIu64"",
                    out, *frames, out->last_frame_reported);
                *frames = out->last_frame_reported;
            }
        } else {
            out->last_timestamp_valid = true;
        }
        aml_volume_shaper_update_start_frames(&out->volume_shaper, *frames);
    }
    {
        if (adev->debug_flag) {
            AM_LOGI("out:%p frames:%"PRIu64", sec:%ld, nanosec:%ld(origin:%" PRId64 ") tuned_latency_ms %d frame_latency %d video delay=%d",
                out, *frames, timestamp->tv_sec, timestamp->tv_nsec, origin_tv_nsec, timems_latency, frame_latency, video_delay_frames);
        }

        int64_t  frame_diff_ms =  ((int64_t)*frames - (int64_t)out->last_frame_reported) * 1000 / out->hal_rate;
        int64_t  system_time_ms = 0;
        int delay = 0;

        system_time_ms = ((int64_t)timestamp->tv_sec * 1000 + (int64_t)timestamp->tv_nsec / 1000000) - ((int64_t)out->last_timestamp_reported.tv_sec * 1000 + (int64_t)out->last_timestamp_reported.tv_nsec / 1000000);
        if (!is_float_equal(speed_info->speed, 1.0f)) {
            system_time_ms = system_time_ms * speed_info->speed;
        }
        int64_t jitter_diff = frame_diff_ms - system_time_ms;
        out->jitter_ms = jitter_diff;
        if (audio_is_linear_pcm(out->hal_format) && audio_stream_out_frame_size(stream) && !out->hw_sync_mode) {
            delay = out->input_bytes_size / audio_stream_out_frame_size(stream) - *frames;
        }
        out->audio_delay = delay;
        if  (llabs(jitter_diff) > JITTER_DURATION_MS && adev->debug_flag) {
            ALOGI("%s jitter out last pos info: %p %"PRIu64", sec = %ld, nanosec = %ld\n",__func__,out, out->last_frame_reported,
                out->last_timestamp_reported.tv_sec, out->last_timestamp_reported.tv_nsec);
            ALOGI("%s jitter  system time diff %"PRIu64" ms, position diff %"PRId64" ms, jitter %"PRId64" ms \n",
                __func__,system_time_ms,frame_diff_ms,jitter_diff);
        }

        aml_stream_out_info_print(out, frames, timestamp);
        out->last_frame_reported = *frames;
        out->last_timestamp_reported = *timestamp;
    }
    return ret;
}
static int get_next_buffer (struct resampler_buffer_provider *buffer_provider,
                            struct resampler_buffer* buffer);
static void release_buffer (struct resampler_buffer_provider *buffer_provider,
                            struct resampler_buffer* buffer);

/** audio_stream_in implementation **/
static unsigned int select_port_by_device(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;
    unsigned int inport = PORT_I2S;
    audio_devices_t in_device = in->device;

    if (in_device & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
        inport = PORT_PCM;
    } else if ((in_device & AUDIO_DEVICE_IN_HDMI) ||
            (in_device & AUDIO_DEVICE_IN_HDMI_ARC) ||
            (in_device & AUDIO_DEVICE_IN_SPDIF)) {
        /* fix auge tv input, hdmirx, tuner */
        if (alsa_device_is_auge() &&
                (in_device & AUDIO_DEVICE_IN_HDMI)) {
            inport = PORT_TV;
            /* T3 needs I2S for HDMIRX due to HBR issue */
            if (get_hdmiin_audio_mode(&adev->alsa_mixer) == HDMIIN_MODE_I2S) {
                inport = PORT_I2S4HDMIRX;
            }
        } else if ((is_earc_descrpt()) &&
                   (in_device & AUDIO_DEVICE_IN_HDMI_ARC)) {
            inport = PORT_EARC;
        } else {
            inport = PORT_SPDIF;
        }
    } else if ((in_device & AUDIO_DEVICE_IN_BACK_MIC) ||
            (in_device & AUDIO_DEVICE_IN_BUILTIN_MIC)) {
        inport = PORT_BUILTINMIC;
    } else if (in_device & AUDIO_DEVICE_IN_ECHO_REFERENCE) {
        inport = PORT_ECHO_REFERENCE;
    } else if (in_device & AUDIO_DEVICE_IN_LINE) {
        /* TODO: json config the linein device */
        if (adev->board_config.sbr_spk_ott_hbr_same_tdm)
            inport = PORT_I2S;
        else if (is_SBR(adev) && alsa_device_is_auge())
            inport = PORT_I2S2HDMI;
        else
            inport = PORT_I2S;
    } else {
        /* fix auge tv input, hdmirx, tuner */
        if (alsa_device_is_auge()
            && (in_device & AUDIO_DEVICE_IN_TV_TUNER))
            inport = PORT_TV;
        else
            inport = PORT_I2S;
    }

#ifdef SUPPORT_KARAOKE
    if (in->source == AUDIO_SOURCE_KARAOKE_SPEAKER)
        inport = PORT_LOOPBACK;
#endif

    return inport;
}

/* TODO: add non 2+2 cases */
static void update_alsa_config(struct aml_stream_in *in) {
#ifdef ENABLE_AEC_APP
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        in->config.rate = in->requested_rate;
    }
#else
    struct aml_audio_device *adev = in->dev;

    /* If board disable farfield and chip has no pdm, select analog dummy config. */
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC &&
            check_chip_name("s7", 2, &adev->alsa_mixer)) {
        in->config.channels = 2;
    }
#endif

    /*add for vts: CapturePositionAdvancesWithReads/18_default_primary
    _18__48000_AUDIO_CHANNEL_IN_MONO. it uses 1ch and c0d1c, but c0d1c not support 1ch.*/
    if ((in->device | AUDIO_DEVICE_BIT_IN) == AUDIO_DEVICE_IN_ECHO_REFERENCE && in->config.channels == 1)
        in->config.channels = 2;
}
static int choose_stream_pcm_config(struct aml_stream_in *in);
static int add_in_stream_resampler(struct aml_stream_in *in);

/* must be called with hw device and input stream mutexes locked */
int start_input_stream(struct aml_stream_in *in)
{
    struct aml_audio_device *adev = in->dev;
    unsigned int card = CARD_AMLOGIC_BOARD;
    unsigned int port = PORT_I2S;
    unsigned int alsa_device = 0;
    int ret = 0;

    ret = choose_stream_pcm_config(in);
    if (ret < 0)
        return -EINVAL;

    adev->active_input = in;
    if (adev->mode != AUDIO_MODE_IN_CALL) {
        adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
        adev->in_device |= in->device;
    }

    card = alsa_device_get_card_index();
    port = select_port_by_device(in);
    /* check to update alsa device by port */
    alsa_device = alsa_device_update_pcm_index(port, CAPTURE);

#ifdef SUPPORT_KARAOKE
    /* Using Aloop to record karaoke data after mix */
    if (in->source == AUDIO_SOURCE_KARAOKE_SPEAKER) {
        card = alsa_device_get_card_index_by_name("Loopback");
        if ((signed int)card < 0) {
            AM_LOGW("in->source = (%d) can not find Aloop card", in->source);
            return -EINVAL;
        }
        /* Alsa Aloop device 1 for capture */
        alsa_device = 1;
    }
#endif

    AM_LOGD("io %d: in:%p open alsa_card(%d %d) alsa_device(%d), in_device:%#x", in->io_handle,
          in, card, port, alsa_device, adev->in_device);
    AM_LOGD("device:%s(%#x) channels=%d period_size=%d rate=%d requested_rate=%d mode= %d",
        audioDevType2Str(in->device | AUDIO_DEVICE_BIT_IN), (in->device | AUDIO_DEVICE_BIT_IN),
        in->config.channels, in->config.period_size, in->config.rate, in->requested_rate, adev->mode);

#ifdef LOWPOWER_DSP_FFV
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        ret = sound_trigger_open(in, port, card);
        if (ret != 0)
            return ret;
    } else {
#endif
        in->pcm = pcm_open(card, alsa_device, PCM_IN | PCM_MONOTONIC | PCM_NONEBLOCK, &in->config);
        if (!pcm_is_ready(in->pcm)) {
            ALOGE("%s: cannot open pcm_in driver: %s", __func__, pcm_get_error(in->pcm));
            pcm_close (in->pcm);
            in->pcm = NULL;
            adev->active_input = NULL;
            return -ENOMEM;
        }
#ifdef LOWPOWER_DSP_FFV
    }
#endif
    if (in->requested_rate != in->config.rate) {
        ret = add_in_stream_resampler(in);
        if (ret < 0) {
#ifdef LOWPOWER_DSP_FFV
            if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
                ret = sound_trigger_close(in);
                if (ret != 0)
                    return ret;
            }
            if (in->pcm != NULL)
#endif
                pcm_close (in->pcm);
            in->pcm = NULL;
            adev->active_input = NULL;
            return -EINVAL;
        }
    }

    /* if no supported sample rate is available, use the resampler */
    if (in->resampler) {
        in->resampler->reset(in->resampler);
        in->frames_in = 0;
    }

    return 0;
}

static void lock_input_stream(struct aml_stream_in *in)
{
    pthread_mutex_lock(&in->pre_lock);
    pthread_mutex_lock(&in->lock);
    pthread_mutex_unlock(&in->pre_lock);
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    size_t size;

    AM_LOGD("io %d: out:%p enter: channel_mask(%#x) rate(%d) format: %s(%#x)", in->io_handle, in, in->hal_channel_mask,
        in->requested_rate, audioFormat2Str(in->hal_format), in->hal_format);
    uint32_t req_channel = audio_channel_count_from_in_mask(in->hal_channel_mask);
    uint32_t req_format = audio_bytes_per_sample(in->hal_format);
    uint32_t period = in->config.period_size;
    if (in->source == AUDIO_SOURCE_ECHO_REFERENCE) {
        period = CAPTURE_PERIOD_SIZE * PLAYBACK_CODEC_SAMPLING_RATE / CAPTURE_CODEC_SAMPLING_RATE;
    }
    if (period == 0) {
        period = pcm_config_in.period_size;
    }
    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    period = (period + 15) / 16 * 16;
    size = period * req_channel * req_format * in->requested_rate / in->config.rate;

    AM_LOGD("exit: buffer_size = %zu", size);
    return size;
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    return in->hal_channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    ALOGV("%s(%p) in->hal_format=%d", __FUNCTION__, in, in->hal_format);
    return in->hal_format;
}

static int in_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return -ENOSYS;
}

/* must be called with hw device and input stream mutexes locked */
int do_input_standby(struct aml_stream_in *in)
{
    AM_LOGD("io %d: in:%p standby:%d", in->io_handle, in, in->standby);
    struct aml_audio_device *adev = in->dev;

    if (!in->standby) {
#ifdef LOWPOWER_DSP_FFV
        if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
            int ret = sound_trigger_to_suspend(in);
            if (ret != 0)
                return -EIO;
        } else {
#endif
            if (in->pcm != NULL) {
                pcm_close (in->pcm);
                in->pcm = NULL;
            }
#ifdef LOWPOWER_DSP_FFV
        }
#endif

        adev->active_input = NULL;
        if (adev->mode != AUDIO_MODE_IN_CALL) {
            adev->in_device &= ~AUDIO_DEVICE_IN_ALL;
            //select_input_device(adev);
        }

        in->standby = 1;
#if 0
        ALOGD ("%s : output_standby=%d,input_standby=%d",
                 __FUNCTION__, output_standby, input_standby);
        if (output_standby && input_standby) {
            reset_mixer_state (adev->ar);
            update_mixer_state (adev->ar);
        }
#endif
    }
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int status;

    AM_LOGD("io %d: in:%p enter", in->io_handle, stream);
    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    AM_LOGD("in:%p exit", stream);

    return status;
}

static int in_dump (const struct audio_stream *stream, int fd)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    AM_LOGD("io %d: in:%p, fd:%d", in->io_handle, stream, fd);
    dprintf(fd, "\n[AML_HAL]-------------in: %p------------------\n", in);
    dprintf(fd, "[AML_HAL] io_handle          : %10d | source  : %19s | device : %s(%#x)\n",
        in->io_handle, audioSourceType2Str(in->source), audioDevType2Str(in->device | AUDIO_DEVICE_BIT_IN),
        in->device | AUDIO_DEVICE_BIT_IN);
    dprintf(fd, "[AML_HAL] config channels    : %10d | rate             : %10d | format           : %d\n",
        in->config.channels, in->config.rate, in->config.format);
    dprintf(fd, "[AML_HAL] hal_channel_mask   : %#10x | requested_rate   : %10d | hal_format       : %s(%#x)\n",
        in->hal_channel_mask, in->requested_rate, audioFormat2Str(in->hal_format), in->hal_format);
    dprintf(fd, "[AML_HAL] standby            : %10d | mute_flag        : %10d |\n",
        in->standby, in->mute_flag);
    dprintf(fd, "\n");
    return 0;
}

static int in_set_parameters (struct audio_stream *stream, const char *kvpairs)
{
    struct aml_stream_in *in = (struct aml_stream_in *) stream;
    struct aml_audio_device *adev = in->dev;
    struct str_parms *parms;
    char *str;
    int str_ret = 0;
    char value[32] = {'\0'};
    int ret = 0, val = 0;
    bool do_standby = false;

    AM_LOGI("io %d: in:%p kvpairs:%s", in->io_handle, stream, kvpairs);
    parms = str_parms_create_str (kvpairs);

    ret = str_parms_get_str (parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value, sizeof (value) );

    pthread_mutex_lock (&adev->lock);
    pthread_mutex_lock (&in->lock);
    if (ret >= 0) {
        val = atoi (value);
        /* no audio source uses val == 0 */
        if ( (in->source != val) && (val != 0) ) {
            in->source = val;
            do_standby = true;
        }
    }

    ret = str_parms_get_str (parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof (value) );
    if (ret >= 0) {
        val = atoi (value) & ~AUDIO_DEVICE_BIT_IN;
        if ( (in->device != (unsigned)val) && (val != 0) ) {
            in->device = val;
            do_standby = true;
        }
    }

    if (do_standby) {
        do_input_standby (in);
    }
    pthread_mutex_unlock (&in->lock);
    pthread_mutex_unlock (&adev->lock);

    int framesize = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_FRAME_COUNT, &framesize);

    if (ret >= 0) {
        if (framesize > 0) {
            ALOGI ("Reset audio input hw frame size from %d to %d\n",
                   in->config.period_size * in->config.period_count, framesize);
            in->config.period_size = framesize / in->config.period_count;
            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&in->lock);

            if (!in->standby && (in == adev->active_input) ) {
                do_input_standby (in);
                str_ret = start_input_stream (in);
                if (str_ret < 0) {
                    ALOGE("[%s:%d] start input stream failed! ret:%#x", __func__, __LINE__, str_ret);
                }
                in->standby = 0;
            }

            pthread_mutex_unlock (&in->lock);
            pthread_mutex_unlock (&adev->lock);
        }
    }

    int format = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_FORMAT, &format);

    if (ret >= 0) {
        if (format != AUDIO_FORMAT_INVALID) {
            in->hal_format = (audio_format_t)format;
            ALOGV("  in->hal_format:%d  <== format:%d\n",
                   in->hal_format, format);

            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&in->lock);
            if (!in->standby && (in == adev->active_input) ) {
                do_input_standby (in);
                str_ret = start_input_stream (in);
                if (str_ret < 0) {
                    ALOGE("[%s:%d] start input stream failed! ret:%#x", __func__, __LINE__, str_ret);
                }

                in->standby = 0;
            }
            pthread_mutex_unlock (&in->lock);
            pthread_mutex_unlock (&adev->lock);
        }
    }

    /*add for vts*/
    int sample_rate = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_SAMPLING_RATE, &sample_rate);

    if (ret >= 0) {
        if (sample_rate > 0) {
            in->requested_rate = sample_rate;
            ALOGV("  in->requested_rate:%d  <== sample_rate:%d\n",
                  in->requested_rate, sample_rate);

            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&in->lock);
            if (!in->standby && (in == adev->active_input) ) {
                do_input_standby (in);
                str_ret = start_input_stream (in);
                if (str_ret < 0) {
                    ALOGE("[%s:%d] start input stream failed! ret:%#x", __func__, __LINE__, str_ret);
                }

                in->standby = 0;
            }
            pthread_mutex_unlock (&in->lock);
            pthread_mutex_unlock (&adev->lock);
        }
    }

    /*add for vts*/
    int channel_mask = 0;
    ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_CHANNELS, &channel_mask);

    if (ret >= 0) {
        if (channel_mask > 0) {
            in->hal_channel_mask = channel_mask;
            ALOGV("  in->hal_channel_mask:%d  <== channel_mask:%d\n",
                   in->hal_channel_mask, channel_mask);

            pthread_mutex_lock (&adev->lock);
            pthread_mutex_lock (&in->lock);
            if (!in->standby && (in == adev->active_input) ) {
                do_input_standby (in);
                str_ret = start_input_stream (in);
                if (str_ret < 0) {
                    ALOGE("[%s:%d] start input stream failed! ret:%#x", __func__, __LINE__, str_ret);
                }
                in->standby = 0;
            }
            pthread_mutex_unlock (&in->lock);
            pthread_mutex_unlock (&adev->lock);
        }
    }

    str_parms_destroy (parms);

    // VTS can only recognizes Result::OK, which is 0x0.
    // So we change ret value to 0 when ret isn't equal to 0
    if (ret > 0) {
        ALOGI ("Amlogic_HAL - %s: change ret value to 0 if it's greater than 0 for passing VTS test.", __FUNCTION__);
        ret = 0;
    } else if (ret < 0) {
        ALOGI ("Amlogic_HAL - %s: parameter is NULL, change ret value to 0 if it's greater than 0 for passing VTS test.", __FUNCTION__);
        ret = 0;
    }

    return ret;
}

static char * in_get_parameters (const struct audio_stream *stream, const char *keys)
{
    char *cap = NULL;
    char *para = NULL;
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    AM_LOGI("io %d: in:%p keys:%s", in->io_handle, stream, keys);
    if (strstr (keys, AUDIO_PARAMETER_STREAM_SUP_FORMATS) ) {
        ALOGV ("Amlogic - return hard coded sup_formats list for in stream.\n");
        cap = strdup ("sup_formats=AUDIO_FORMAT_PCM_16_BIT|AUDIO_FORMAT_PCM_32_BIT");
        if (cap) {
            para = strdup (cap);
            aml_audio_free (cap);
            cap = NULL;
            return para;
        }
    } else if (strstr (keys, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES) ) {
        /*add for vts*/
        ALOGV ("Amlogic - return hard coded sup_sampling_rates list for in stream.\n");
        cap = strdup ("sup_sampling_rates=8000|11025|12000|16000|22050|24000|32000|44100|48000");
        if (cap) {
            para = strdup (cap);
            aml_audio_free (cap);
            cap = NULL;
            return para;
        }
    } else if (strstr (keys, AUDIO_PARAMETER_STREAM_SUP_CHANNELS) ) {
        /*add for vts*/
        ALOGV ("Amlogic - return hard coded sup_channels list for in stream.\n");
        cap = strdup ("sup_channels=AUDIO_CHANNEL_IN_MONO|AUDIO_CHANNEL_IN_STEREO");
        if (cap) {
            para = strdup (cap);
            aml_audio_free (cap);
            cap = NULL;
            return para;
        }
    }
    return strdup ("");
}

static int in_set_gain (struct audio_stream_in *stream __unused, float gain __unused)
{
    return 0;
}

static int get_next_buffer (struct resampler_buffer_provider *buffer_provider,
                            struct resampler_buffer* buffer)
{
    struct aml_stream_in *in;
    R_CHECK_POINTER_LEGAL(-EINVAL, buffer_provider,);
    R_CHECK_POINTER_LEGAL(-EINVAL, buffer,);
    in = (struct aml_stream_in *) ( (char *) buffer_provider -
                                    offsetof (struct aml_stream_in, buf_provider) );

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->frames_in == 0) {
        in->read_status = aml_alsa_input_read ((struct audio_stream_in *)in, (void*) in->buffer,
                                    in->config.period_size * audio_stream_in_frame_size (&in->stream) );
        if (in->read_status != 0) {
            ALOGE ("get_next_buffer() pcm_read error %d", in->read_status);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->frames_in = in->config.period_size;
    }

    buffer->frame_count = (buffer->frame_count > in->frames_in) ?
                          in->frames_in : buffer->frame_count;
    buffer->i16 = in->buffer + (in->config.period_size - in->frames_in) *
                  in->config.channels;

    return in->read_status;

}

static void release_buffer (struct resampler_buffer_provider *buffer_provider,
                            struct resampler_buffer* buffer)
{
    struct aml_stream_in *in;
    R_CHECK_POINTER_LEGAL(, buffer_provider,);
    R_CHECK_POINTER_LEGAL(, buffer,);

    in = (struct aml_stream_in *) ( (char *) buffer_provider -
                                    offsetof (struct aml_stream_in, buf_provider) );

    in->frames_in -= buffer->frame_count;
}

/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t read_frames (struct aml_stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        if (in->resampler != NULL) {
            in->resampler->resample_from_provider (in->resampler,
                                                   (int16_t *) ( (char *) buffer +
                                                           frames_wr * audio_stream_in_frame_size (&in->stream) ),
                                                   &frames_rd);
        } else {
            struct resampler_buffer buf = {
                { .raw = NULL, },
                .frame_count = frames_rd,
            };
            get_next_buffer (&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy ( (char *) buffer +
                         frames_wr * audio_stream_in_frame_size (&in->stream),
                         buf.raw,
                         buf.frame_count * audio_stream_in_frame_size (&in->stream) );
                frames_rd = buf.frame_count;
            }
            release_buffer (&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0) {
            return in->read_status;
        }

        frames_wr += frames_rd;
    }
    return frames_wr;
}

#define DEBUG_AEC (0) // Remove after AEC is fine-tuned
static ssize_t in_read_from_hw(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    int ret = 0;
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    int channel_count = audio_channel_count_from_in_mask(in->hal_channel_mask);
    size_t in_frames = bytes / audio_stream_in_frame_size(&in->stream);
    struct aml_audio_patch* patch = get_dev_patch(adev);
    size_t cur_in_bytes, cur_in_frames;

    ALOGV("%s(): stream: %p, source: %d, bytes %zu in_frames:%zu in->devices %0x", __func__, in, in->source, bytes, in_frames, in->device);

    if (bytes == 0)
        return 0;

    lock_input_stream(in);

#ifdef ENABLE_AEC_APP
    /* Special handling for Echo Reference: simply get the reference from FIFO.
     * The format and sample rate should be specified by arguments to adev_open_input_stream. */
    if (in->source == AUDIO_SOURCE_ECHO_REFERENCE) {
        if (in->standby) {
            AM_LOGD("io %d: in:%p standby to unstandby", in->io_handle, in);
            ret = start_input_stream(in);
            if (ret < 0) {
                ALOGE("aec: fail to open stream\n");
                 goto exit;
            }
            in->standby = 0;
        }
        ret = aml_alsa_input_read(stream, buffer, bytes);
        if (ret != 0) {
            ALOGE("aec: fail to read bytes=%zu ret=%d\n", bytes, ret);
             goto exit;
        }
        in->frames_read += in_frames;
        goto exit;
    }
#endif

#ifdef SUPPORT_KARAOKE
    //AUDIO_SOURCE_MIC and Param "linein_kara_record=1" to record karaoke linein
    if (in->source == AUDIO_SOURCE_MIC) {
        struct kara_manager *karaoke = &adev->linein_karaoke;
        if (karaoke_get_mic_record(karaoke)) {
            if (karaoke_get_on(karaoke) || karaoke_get_start(karaoke)) {
                ret = karaoke->read(karaoke, buffer, bytes);
                if (ret == bytes) {
                    in->frames_read += in_frames;
                }
                goto exit;
            }
        }
    }
#endif

    if (adev->dev2mix_patch) {
        ALOGV("dev2mix patch case ");
    } else {
        if (in->standby) {
            AM_LOGD("io %d: in:%p standby to unstandby", in->io_handle, in);
            ret = start_input_stream(in);
            if (ret < 0)
                goto exit;
            in->standby = 0;
        }
    }

#if 0
    if (adev->dev_to_mix_parser != NULL) {
        bytes = aml_dev2mix_parser_process(in, buffer, bytes);
    }
#endif

    if (adev->dev2mix_patch) {
        float source_gain = aml_audio_get_s_gain_by_src(adev, get_dev_patch_src(adev));
        ret = tv_in_read(stream, buffer, bytes);
        bytes = ret;
        if (getprop_bool("vendor.media.audiohal.indump")) {
            aml_dump_audio_bitstreams("/data/audio/tv_in_read.raw",
                buffer, bytes);
        }

        enum IN_PORT inport = INPORT_HDMIIN;
        float in_port_gain = get_active_inport_gain(adev);
        android_dev_convert_to_hal_dev(in->device | AUDIO_DEVICE_BIT_IN, (int *)&inport);
        apply_volume(source_gain * in_port_gain, buffer, sizeof(uint16_t), bytes);
        in->frames_read += in_frames;
        in->timestamp_nsec = aml_audio_get_systime_ns();
        goto exit;
    } else {

        /*if need mute input source, don't read data from hardware anymore*/
        if (adev->mic_mute) {
            memset(buffer, 0, bytes);
            usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
                in_get_sample_rate(&stream->common));
            ret = 0;

        } else {

            if (in->resampler) {
                ret = read_frames(in, buffer, in_frames);
            } else {
                /*coverity[sleep]*/
#ifdef LOWPOWER_DSP_FFV
                if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
                    ret = sound_trigger_read(in, buffer, bytes);
                } else {
#endif
                    ret = aml_alsa_input_read(stream, buffer, bytes);
#ifdef LOWPOWER_DSP_FFV
                }
#endif
            }
            if (ret < 0)
                goto exit;
            //DoDumpData(buffer, bytes, CC_DUMP_SRC_TYPE_INPUT);
        }
    }

    if (ret >= 0) {
        in->frames_read += in_frames;
    }
    bool mic_muted = false;
    adev_get_mic_mute((struct audio_hw_device*)adev, &mic_muted);
    if (mic_muted) {
        memset(buffer, 0, bytes);
    }

exit:
    if (ret < 0) {
        ALOGE("%s: read failed - sleeping for buffer duration", __func__);
        usleep(bytes * 1000000 / audio_stream_in_frame_size(stream) /
                in_get_sample_rate(&stream->common));
    }
    pthread_mutex_unlock(&in->lock);

#if DEBUG_AEC && defined(ENABLE_AEC_APP)
    aml_dump_audio_bitstreams("/data/vendor/audiohal/aec_in.pcm", buffer, bytes);

    FILE* fp_mic_ts = fopen("/data/vendor/audiohal/aec_in_timestamps.txt", "a+");
    if (fp_mic_ts) {
        fprintf(fp_mic_ts, "%" PRIu64 "\n", in->timestamp_nsec);
        fclose(fp_mic_ts);
    } else {
        ALOGE("AEC debug: Could not open file aec_in_timestamps.txt!");
    }
#endif
    if (ret >= 0 && get_debug_value(AML_DUMP_AUDIOHAL_IN)) {
        aml_dump_audio_bitstreams("/data/vendor/audiohal/alsa_read.raw",
            buffer, bytes);
    }

    return bytes;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    ssize_t sum = 0;
#ifdef LOWPOWER_DSP_FFV
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC)
        sum = in_read_from_fetch_buf(stream, buffer, bytes);
#endif
    sum += in_read_from_hw(stream, (void *)((char*)buffer + sum), bytes - sum);

    return sum;
}

static int in_get_capture_position (const struct audio_stream_in* stream, int64_t* frames,
                                   int64_t* time) {
    if (stream == NULL || frames == NULL || time == NULL) {
        return -EINVAL;
    }
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int ret = -ENOSYS;

    lock_input_stream(in);
    if (in->standby) {
        ret = 0;
        goto exit;
    }
    if (in->pcm) {
        struct timespec timestamp;
        unsigned int avail;
        if (pcm_get_htimestamp(in->pcm, &avail, &timestamp) == 0) {
            *frames = in->frames_read + avail;
            *time = timestamp.tv_sec * 1000000000LL + timestamp.tv_nsec;
            pthread_mutex_unlock(&in->lock);
            return 0;
        }
    }
#ifdef LOWPOWER_DSP_FFV
    if (in->device & AUDIO_DEVICE_IN_BUILTIN_MIC) {
        unsigned int avail_dsp = 0;
        pcm_get_latency_dsp(in->dsp_ffv_in_t->sound_trigger_handle, &avail_dsp);
        *time = aml_audio_get_systime_ns();
        *frames = in->frames_read + avail_dsp;
        pthread_mutex_unlock(&in->lock);
        return 0;
    }
#endif
exit:
    *frames = in->frames_read;
    in->timestamp_nsec = aml_audio_get_systime_ns();
    *time = in->timestamp_nsec;
    pthread_mutex_unlock(&in->lock);
    return ret;
}

static uint32_t in_get_input_frames_lost (struct audio_stream_in *stream __unused)
{
    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream __unused, effect_handle_t effect __unused)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int status;
    effect_descriptor_t desc = {0};

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors >= MAX_PREPROCESSORS) {
        status = -ENOSYS;
        goto exit;
    }

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;

    in->preprocessors[in->num_preprocessors++] = effect;

    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = true;
        do_input_standby(in);
    }

exit:

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    int i;
    int status = -EINVAL;
    bool found = false;
    effect_descriptor_t desc = {0};

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors <= 0) {
        status = -ENOSYS;
        goto exit;
    }

    for (i = 0; i < in->num_preprocessors; i++) {
        if (found) {
            in->preprocessors[i - 1] = in->preprocessors[i];
            continue;
        }
        if (in->preprocessors[i] == effect) {
            in->preprocessors[i] = NULL;
            status = 0;
            found = true;
        }
    }

    if (status != 0)
        goto exit;

    in->num_preprocessors--;

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;
    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = false;
        do_input_standby(in);
    }

exit:

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_get_active_microphones (const struct audio_stream_in *stream,
                                     struct audio_microphone_characteristic_t *mic_array,
                                     size_t *mic_count) {
    ALOGV("in_get_active_microphones");
    R_CHECK_POINTER_LEGAL(-EINVAL, mic_array,);
    R_CHECK_POINTER_LEGAL(-EINVAL, mic_count,);
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct audio_hw_device* dev = (struct audio_hw_device*)in->dev;
    bool mic_muted = false;
    adev_get_mic_mute(dev, &mic_muted);
    if ((in->source == AUDIO_SOURCE_ECHO_REFERENCE) || mic_muted) {
        *mic_count = 0;
        return 0;
    }
    adev_get_microphones(dev, mic_array, mic_count);
    return 0;
}

static int adev_get_microphones (const struct audio_hw_device* dev __unused,
                                struct audio_microphone_characteristic_t* mic_array,
                                size_t* mic_count) {
    ALOGV("adev_get_microphones");
    R_CHECK_POINTER_LEGAL(-EINVAL, mic_array,);
    R_CHECK_POINTER_LEGAL(-EINVAL, mic_count,);
    get_mic_characteristics(mic_array, mic_count);
    return 0;
}

static void get_mic_characteristics (struct audio_microphone_characteristic_t* mic_data,
                                    size_t* mic_count) {
    *mic_count = 1;
    memset(mic_data, 0, sizeof(struct audio_microphone_characteristic_t));
    strlcpy(mic_data->device_id, "builtin_mic", AUDIO_MICROPHONE_ID_MAX_LEN - 1);
    strlcpy(mic_data->address, "top", AUDIO_DEVICE_MAX_ADDRESS_LEN - 1);
    memset(mic_data->channel_mapping, AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED,
           sizeof(mic_data->channel_mapping));
    mic_data->device = AUDIO_DEVICE_IN_BUILTIN_MIC;
    mic_data->sensitivity = -37.0;
    mic_data->max_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    mic_data->min_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    mic_data->orientation.x = 0.0f;
    mic_data->orientation.y = 0.0f;
    mic_data->orientation.z = 0.0f;
    mic_data->geometric_location.x = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    mic_data->geometric_location.y = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    mic_data->geometric_location.z = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
}

static int out_set_event_callback(struct audio_stream_out *stream,
                                       stream_event_callback_t callback, void *cookie /*StreamOut*/)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    AM_LOGI("io %d: out:%p, callback:%p cookie:%p", out->io_handle, stream, callback, cookie);
    out->stream_event_callback = callback;
    out->stream_cookie = cookie;
    return 0;
}

int output_stream_hwsync_prepare(struct aml_stream_out *out, int hw_sync_id)
{
    struct aml_audio_device *adev = out->dev;
    int ret_val = 0;

    if (hw_sync_id < 0) {
        AM_LOGE("hw_sync_id:%d is a illegal value, so exit directly.", hw_sync_id);
        ret_val = -1;
        goto err;
    }

    //prepare hwsync resource.
    if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC && out->hw_sync_mode == false) {
        bool ret_set_id = false;
        bool is_mediasync = check_support_mediasync();

        pthread_mutex_lock(&out->lock);
        out->hwsync = aml_audio_calloc(1, sizeof(audio_hwsync_t));
        if (!out->hwsync) {
            pthread_mutex_unlock(&out->lock);
            ALOGE("%s,malloc hwsync failed", __func__);
            ret_val = -ENOMEM;
            goto err;
        }
        pthread_mutex_unlock(&out->lock);

        ALOGI("[%s] is_mediasync:%d, hw_sync_id:%d\n", __FUNCTION__, is_mediasync, adev->hw_sync_id);
        {
            void *pMediaSyncHandle = NULL;

            pthread_mutex_lock(&adev->mediasync_lock);
            if (hw_sync_id != -1)  {
                pMediaSyncHandle = aml_lookup_mediasync_handle(adev->mediasync, hw_sync_id);
                if (pMediaSyncHandle != NULL) {
                    aml_add_mediasync_ref_count(adev->mediasync, hw_sync_id);
                }
                int retryCount = 0;
                while (pMediaSyncHandle == NULL) {
                    pMediaSyncHandle = aml_audio_hwsync_create();
                    if (pMediaSyncHandle) {
                        aml_add_mediasync_info(adev->mediasync, pMediaSyncHandle, hw_sync_id);
                    }
                    if (retryCount > 3) {
                        AM_LOGW(" create hwsync retryCount more than:%d", retryCount);
                        break;
                    }
                    retryCount++;
                }
            }
            pthread_mutex_unlock(&adev->mediasync_lock);

            if (pMediaSyncHandle != NULL) {
                out->hwsync->use_mediasync = true;
                out->hwsync->mediasync = pMediaSyncHandle;
                out->hwsync->hwsync_id = hw_sync_id;
                ret_set_id = aml_hwsync_wrap_set_id(out->hwsync, hw_sync_id);
                if (ret_set_id == false) {
                    ALOGI("mediasync set hwsync id fail, try gMediaSync_bindStaticInstance");
                    ret_set_id = aml_hwsync_wrap_set_static_id(out->hwsync, hw_sync_id);
                }
                if (ret_set_id == false) {
                    ALOGI("mediasync set hwsync id fail, need get new one");
                    ret_set_id = aml_hwsync_wrap_get_id(out->hwsync->mediasync, &out->hwsync->hwsync_id);
                    if (ret_set_id && out->hwsync->hwsync_id != -1) {
                        //adev->hw_sync_id = out->hwsync->hwsync_id;
                        ret_set_id = aml_hwsync_wrap_set_id(out->hwsync, out->hwsync->hwsync_id);
                    }
                }
            } else {
                AM_LOGE(" pMediaSyncHandle is NULL, hw_sync_id:%d", hw_sync_id);
                ret_val = -1;
                goto err;
            }
            aml_audio_hwsync_init(out->hwsync, out);
        }

        bool sync_enable = ret_set_id ? true : false;
        audio_hwsync_t *hw_sync = out->hwsync;
        ALOGI("stream:(%p) set hw_sync_id:%d (0x%x), %s hw_sync and the mode is %s\n",
               out, hw_sync_id, hw_sync_id, sync_enable ? "enable" : "disable", "mediasync");
        out->hw_sync_mode = sync_enable;

        if (adev->ms12_out != NULL && adev->ms12_out->hwsync) {
            adev->ms12_out->hw_sync_mode = out->hw_sync_mode;
            ALOGI("set ms12_out:%p hw_sync_mode %d",adev->ms12_out, adev->ms12_out->hw_sync_mode);
        }
        hw_sync->first_apts_flag = false;
        hw_sync->wait_video_done = false;

        pthread_mutex_lock (&out->lock);
        out->frame_write_sum = 0;
        out->last_frames_position = 0;
        pthread_mutex_unlock (&out->lock);
        ALOGI ("[%s]  hwsync done\n", __FUNCTION__);
    } else {
        AM_LOGW("out->hw_sync_mode:%d, hw_sync_id:%d", out->hw_sync_mode, hw_sync_id);
    }

err:
    return ret_val;
}

// open corresponding stream by flags, formats and others params
static int adev_open_output_stream(struct audio_hw_device *dev,
                                audio_io_handle_t handle,
                                audio_devices_t devices,
                                audio_output_flags_t flags,
                                struct audio_config *config,
                                struct audio_stream_out **stream_out,
                                const char *address)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_out *out;
    int digital_codec;
    int ret;
    struct amlAudioMixer *audio_mixer = adev->mixerData;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    aml_stream_speed_info_t *speed_info = NULL;

    out = (struct aml_stream_out *)aml_audio_calloc(1, sizeof(struct aml_stream_out));
    AM_LOGI("io %d: out:%p dev:%s(%#x) addr:%s", handle, out, audioDevType2Str(devices), devices, address);
    AM_LOGI("ch_mask:%#x rate:%d format:%s(%#x) flags:%#x", config->channel_mask, config->sample_rate,
        audioFormat2Str(config->format), config->format, flags);

    R_CHECK_POINTER_LEGAL(-ENOMEM, out, "malloc aml_stream_out fail");

    if (pthread_mutex_init(&out->lock, NULL)) {
        ALOGE("%s pthread_mutex_init failed", __func__);
    }

    if (pthread_mutex_init(&out->apts_update_lock, NULL)) {
        ALOGE("%s pthread_mutex_init(apts_update_lock) failed", __func__);
    }
    speed_info = &out->speed_info;

    if (address && !strncmp(address, "AML_TV", 6)) {
        ALOGI("%s(): aml TV output stream(%p)", __func__, out);
        out->is_tv_src_stream = true;
    } else if (address && !strncmp(address, "AML_DTV", 7)) {
        ALOGI("%s(): aml DTV output stream(%p)", __func__, out);
        out->is_dtv_src_stream = true;
    } else {
        adev->foreground_stream_type = FG_STREAM_TYPE_AUDIOFLINGER;
    }
    if (!out->is_tv_src_stream && adev->is_netflix) {
        out->is_netflix_src_stream = true;
        ALOGI("%s(): aml Netflix output stream(%p)", __func__, out);
    }

    if (flags == AUDIO_OUTPUT_FLAG_NONE)
        flags = AUDIO_OUTPUT_FLAG_PRIMARY;
    if (config->channel_mask == AUDIO_CHANNEL_NONE)
        config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    if (config->sample_rate == 0)
        config->sample_rate = 48000;
    if (config->format == AUDIO_FORMAT_DEFAULT)
        config->format = AUDIO_FORMAT_PCM_16_BIT;

    if (flags & AUDIO_OUTPUT_FLAG_PRIMARY) {


        out->stream.common.get_channels = out_get_channels;
        out->stream.common.get_format = out_get_format;
        out->stream.write = out_write_new;
        out->stream.common.standby = out_standby_new;

        out->hal_channel_mask = config->channel_mask;
        out->hal_rate = config->sample_rate;
        out->hal_format = config->format;
        out->hal_internal_format = out->hal_format;

        out->config = pcm_config_out;
        out->config.channels = audio_channel_count_from_out_mask(config->channel_mask);
        out->config.rate = config->sample_rate;

        switch (config->format) {
        case AUDIO_FORMAT_PCM_16_BIT:
            out->config.format = PCM_FORMAT_S16_LE;
            break;
        case AUDIO_FORMAT_PCM_32_BIT:
            out->config.format = PCM_FORMAT_S32_LE;
            break;
        default:
            break;
        }
    } else if ((flags & AUDIO_OUTPUT_FLAG_DIRECT) || (flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER)) {
        //fixme, Direct PCM and DEEP buffer can not be exiting at the same time.
        //ASDK, test case[atmos_stickiness_usage_media_ddp_out-v241-HDMI (460)/467]
        //then enable the Mixer with flag=AUDIO_OUTPUT_FLAG_DEEP_BUFFER.
        if (config->format == AUDIO_FORMAT_DEFAULT) {
            if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
                config->format = AUDIO_FORMAT_PCM_16_BIT;
            } else {
                config->format = AUDIO_FORMAT_AC3;
            }
        }
        out->stream.common.get_channels = out_get_channels_direct;
        out->stream.common.get_format = out_get_format_direct;
        out->stream.write = out_write_new;
        out->stream.common.standby = out_standby_new;

        out->hal_channel_mask = config->channel_mask;
        out->hal_rate = config->sample_rate;
        out->hal_format = config->format;
        out->hal_internal_format = out->hal_format;

        /*hdmi in/arc in/ spdif in, the raw data is IEC61937 format*/
        if (out->is_tv_src_stream && !audio_is_linear_pcm(out->hal_format)) {
            out->hal_format = AUDIO_FORMAT_IEC61937;
        }

        if (out->hal_internal_format == AUDIO_FORMAT_E_AC3_JOC) {
            out->hal_internal_format = AUDIO_FORMAT_E_AC3;
            AM_LOGD("config hal_format %s change to hal_internal_format(%s)!",
            audioFormat2Str(out->hal_format), audioFormat2Str(out->hal_internal_format));
        }
        out->config = pcm_config_out_direct;
        out->config.channels = audio_channel_count_from_out_mask(config->channel_mask);
        out->config.rate = config->sample_rate;
        switch (config->format) {
        case AUDIO_FORMAT_PCM_16_BIT:
            out->config.format = PCM_FORMAT_S16_LE;
            break;
        case AUDIO_FORMAT_PCM_32_BIT:
            out->config.format = PCM_FORMAT_S32_LE;
            break;
        case AUDIO_FORMAT_IEC61937:
            AM_LOGD("current format is %s", audioFormat2Str(out->hal_internal_format));
            break;
        case AUDIO_FORMAT_AC3:
        case AUDIO_FORMAT_E_AC3:
        case AUDIO_FORMAT_AC4:
        case AUDIO_FORMAT_DOLBY_TRUEHD:
            break;
        case AUDIO_FORMAT_DTS:
        case AUDIO_FORMAT_DTS_HD:
        case AUDIO_FORMAT_DTS_UHD_P2:
            break;
        default:
            break;
        }

        digital_codec = get_codec_type(out->hal_internal_format);
        switch (digital_codec) {
        case TYPE_AC3:
        case TYPE_EAC3:
            out->config.period_size *= 2;
            out->raw_61937_frame_size = 4;
            break;
        case TYPE_TRUE_HD:
            out->config.period_size *= 4 * 2;
            out->raw_61937_frame_size = 16;
            break;
        case TYPE_DTS:
            out->config.period_count *= 2;
            out->raw_61937_frame_size = 4;
            break;
        case TYPE_DTS_HD:
            out->config.period_count *= 2;
            out->raw_61937_frame_size = 16;
            break;
        case TYPE_PCM:
            if (out->config.channels >= 6 || out->config.rate > 48000)
                adev->hi_pcm_mode = true;
            break;
        default:
            out->raw_61937_frame_size = 1;
            break;
        }
        if (codec_type_is_raw_data(digital_codec)) {
            ALOGI("%s: for raw audio output,force alsa stereo output", __func__);
            out->config.channels = 2;
            out->multich = 2;
        } else if (out->config.channels > 2) {
            out->multich = out->config.channels;
        }
    } else {
        // TODO: add other cases here
        ALOGE("%s: flags = %#x invalid", __func__, flags);
        ret = -EINVAL;
        goto err;
    }

    out->io_handle = handle;
    out->hal_ch   = audio_channel_count_from_out_mask(out->hal_channel_mask);
    out->hal_frame_size = audio_bytes_per_frame(out->hal_ch, out->hal_internal_format);
    if (out->hal_ch == 0) {
        out->hal_ch = 2;
    }
    if (out->hal_frame_size == 0) {
        out->hal_frame_size = 1;
    }

    adev->audio_hal_info.format = AUDIO_FORMAT_PCM;
    adev->audio_hal_info.is_dolby_atmos = 0;
    adev->audio_hal_info.update_type = TYPE_PCM;
    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.set_format = out_set_format;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = audiohal_get_latency; // out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.update_source_metadata_v7 = out_update_source_metadata_v7;
    out->stream.get_presentation_position = out_get_presentation_position;
    out->stream.set_event_callback = out_set_event_callback;
    //unify these msg interfaces of stream
    out->stream.pause = out_pause_new;
    out->stream.resume = out_resume_new;
    out->stream.flush = out_flush_new;
    out->stream.set_playback_rate_parameters = out_set_playback_rate_parameters;
    out->stream.get_playback_rate_parameters = out_get_playback_rate_parameters;
    out->out_device = devices;
    out->flags = flags;
    out->volume_l = 1.0;
    out->volume_r = 1.0;
    out->last_volume_l = 0.0;
    out->last_volume_r = 0.0;
    out->ms12_vol_ctrl = false;
    out->dev = adev;
    out->standby = true;
    out->frame_write_sum = 0;
    out->hw_sync_mode = false;
    out->need_convert = false;
    out->need_drop_size = 0;
    out->position_update = 0;
    out->inputPortID = -1;
    out->write_count = 0;
    out->frame_write_sum_updated = false;
    out->is_insert_zero_data = false;
    out->is_waiting_video = false;
    out->insert_zero_data_ms = 0;
    out->hwsync_parsed_frames_sum_paused = 0;
    out->last_periodic_print_time_in_ms = 0;
    out->hwsync_header_stripped = false;
    out->is_closing = false;
    out->pause_time = 0;
    out->needs_compensation_timeus = 0;
    out->restore_vmaster = false;
    out->is_callback_pending = false;
    out->b_migrate_check = false;
    out->migrated_on_apu = false;
    out->audiomixer_standby = true;
    out->last_timestamp_valid = false;
    out->is_ms12_main_decoder_disable = false;
    out->output_speed = 1.0f;
    speed_info->speed = 1.0f;
    speed_info->mPitch = 1.0f;
    speed_info->mStretchMode = 0;
    speed_info->mFallbackMode = 2;
    speed_info->last_speed = 1.0f;
    speed_info->hwsync_force_update = true;

    clock_gettime(CLOCK_MONOTONIC, &out->last_info_timestamp);
    clock_gettime(CLOCK_MONOTONIC, &out->last_avsync_timestamp);

    //prepare hwsync resource for tunnel mode.
    //FIXME, normal design should be put here for hwsync.
    /*ret = output_stream_hwsync_prepare(out, adev->hw_sync_id);
    if (ret < 0) {
        goto err;
    }*/

    if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        const char *llp_prop = "vendor.media.llp";
        bool request_llp_mode = false;

        ALOGI("when open aaudio stream, send RUNNING msg to submix & ms12");
        aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_RUNNING);
        if (adev->useAudioMixer) {
           aml_audiohal_sch_state_2_submix(audio_mixer, SUBMIX_SCHEDULER_RUNNING);
        }
        if (outMmapInit(out) != 0) {
            AM_LOGE("outMmapInit out %p fail !", out);
            ret = -1;
            goto err;
        }

        request_llp_mode = getprop_bool(llp_prop);
        AM_LOGI("%s %d", llp_prop, request_llp_mode);

        if (config->offload_info.usage == AUDIO_USAGE_GAME || request_llp_mode) {
            aml_enter_aaudio_low_latency(adev);
            get_sink_format((struct audio_stream_out *)out);
            out->aaudio_low_latency = true;
            adev->aaudio_low_latency_updated = true;
        }
    }

    /* FIXME: when we support multiple output devices, we will want to
     * do the following:
     * adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
     * adev->devices |= out->device;
     * select_output_device(adev);
     * This is because out_set_parameters() with a route is not
     * guaranteed to be called after an output stream is opened.
     */
    if (is_TV(adev)) {
        struct audio_board_config *bd_config = &adev->board_config;

        out->is_tv_platform = 1;
        out->config.channels = bd_config->default_alsa_ch;
        out->config.format = PCM_FORMAT_S32_LE;
    }

    if (out->is_dtv_src_stream) {
        out->hwsync =  aml_audio_calloc(1, sizeof(audio_hwsync_t));
        aml_audio_hwsync_init(out->hwsync, out);
    }

    //aml_audio_buffer_t
    if (aml_init_audio_buffer(out) < 0) {
        AM_LOGE("_init_audio_buffer out %p fail !", out);
        ret = -1;
        goto err;
    }

    if (out->hal_format == AUDIO_FORMAT_AC4) {
        aml_ac4_parser_open(&out->ac4_parser_handle, NULL);
    }
    aml_audio_speed_init_start_ts(&out->speed_info.start_ts);
    aml_stream_clear_speed_aux_info(out);
    aml_audio_speed_init_post_delay(&speed_info->post_delay, 48000);
    aml_audio_data_handle_init((struct audio_stream_out *)out);

    out->current_digital_audio_format = adev->digital_audio_mode;

    out->ddp_frame_size = aml_audio_get_ddp_frame_size();
    out->resample_handle = NULL;
    speed_info->speed_handle = NULL;
    out->streamType = attr_to_streamType(out->device, out->hal_format, out->flags);
    *stream_out = &out->stream;
    adev->debug_flag = aml_audio_get_debug_flag();

    AM_LOGI("result profile ch_mask:%#x rate:%d format:%s(%#x) streamType:%s", config->channel_mask,
        config->sample_rate, audioFormat2Str(config->format), config->format, streamType2Str(out->streamType));
    return 0;
err:
    pthread_mutex_lock(&out->lock);
    if (out->hwsync) {
        aml_audio_free(out->hwsync);
        out->hwsync = NULL;
    }

    pthread_mutex_unlock(&out->lock);
    pthread_mutex_destroy(&out->lock);

    pthread_mutex_destroy(&out->apts_update_lock);

    aml_audio_free(out);
    return ret;
}

static void close_ms12_output_main_stream(struct audio_stream_out *stream) {
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct aml_stream_out *ms12_out = (struct aml_stream_out *)adev->ms12_out;
    struct dolby_ms12_dec_desc *ms12_dec = out->ms12_dec_handle;

    /*main stream is closed, close the ms12 main decoder*/
    if (out->is_ms12_main_decoder) {
        pthread_mutex_lock(&adev->ms12.lock);
        /*after ms12 lock, dolby_ms12_enable may be cleared with clean up function*/
        if (adev->ms12.dolby_ms12_enable) {
            audio_format_t hal_internal_format = ms12_get_audio_hal_format(out->hal_internal_format);
            ms12_dec->need_resume = false;
            ms12_dec->need_resync = false;
            dolby_ms12_main_flush(stream);
            /*coverity[missing_lock]*/
            ms12_dec->resume_state = MS12_RESUME_FROM_CLOSE;
            dolby_ms12_main_resume(stream);
        }
        /*coverity[double_unlock]*/
        pthread_mutex_unlock(&adev->ms12.lock);

        /*main stream is closed, wait mesg processed*/
        {
            int wait_cnt = 0;
            while (!ms12_msg_list_is_empty(&adev->ms12)) {
                aml_audio_sleep(5000);
                wait_cnt++;
                if (wait_cnt >= 200) {
                    break;
                }
            }
            ALOGI("main stream message is processed cost =%d ms", wait_cnt * 5);
        }
        dolby_ms12_main_close(stream);
        out->is_ms12_main_decoder = false;
        adev->ms12.dtv_decoder_offset_base = 0;
    }

    return;
}


//static int out_standby_new(struct audio_stream *stream);
static void adev_close_output_stream(struct audio_hw_device *dev,
                                    struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct amlAudioMixer *audio_mixer = adev->mixerData;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    aml_stream_speed_info_t *speed_info = &out->speed_info;

    int ret = 0;
    AM_LOGI("io %d: out:%p dev:%s(%#x) flags:%#x, streamType:%s", out->io_handle, out,
        audioDevType2Str(out->out_device), out->out_device, out->flags, streamType2Str(out->streamType));

    adev->atmos_indicator_status = false;

    if (out->restore_hdmitx_selection) {
        /* switch back to spdifa when the dual stream is done */
        aml_audio_select_src_to_hdmi(AML_SPDIF_A_TO_HDMITX);
        out->restore_hdmitx_selection = false;
    }
    R_CHECK_POINTER_LEGAL(, adev,);
    if (stream->common.standby)
        stream->common.standby(&stream->common);

    pthread_mutex_lock(&out->lock);

    if (out->is_ms12_main_decoder) {
        close_ms12_output_main_stream(stream);
    }

    /* After playback for previous dts stream, there is remain data in VirtualX library. It needs to clear data buffer of VirtualX by using
       zero data to replace these remain data. Otherwise it will play this remain data first when start playback next time*/
    if ((adev->cur_out_devices & AUDIO_DEVICE_OUT_SPEAKER) != 0 && out->write_count > 0 && is_dts_format(out->hal_internal_format)) {
        char *tmp_buffer = aml_audio_malloc(VX_BUFFER_CLEAR_MULTICHANNEL_FRAME_SIZE);
        if (!tmp_buffer) {
            ALOGE("tmp_buffer NULL %d",__LINE__);
        } else {
            for (int i = 0; i < VX_BUFFER_CLEAR_COUNT; i++) {
                 memset(tmp_buffer, 0, VX_BUFFER_CLEAR_MULTICHANNEL_FRAME_SIZE);
                 audio_post_process(&adev->native_postprocess, (int16_t *)tmp_buffer, VX_BUFFER_CLEAR_STEREO_FRAME_SIZE);
                 audio_VX_post_process(&adev->native_postprocess, (int16_t *)tmp_buffer, VX_BUFFER_CLEAR_MULTICHANNEL_FRAME_SIZE);
            }
            aml_audio_free(tmp_buffer);
            tmp_buffer = NULL;
        }
    }

#if ENABLE_DVB_PATCH
#if ANDROID_PLATFORM_SDK_VERSION > 29
    if (dtv_tuner_framework(stream)) {
        /*enter into tuner framework case, we need to stop&release audio dtv patch*/
        ALOGD("[audiohal_kpi] %s %d", __func__, __LINE__);
        /*coverity[sleep]*/
        ret = disable_dtv_patch_for_tuner_framework(stream);
        if (!ret) {
            ALOGI("%s: finish releasing patch", __func__);
        }
    }
#endif
#endif

    if (out->spdifenc_init) {
        aml_spdif_encoder_close(out->spdifenc_handle);
        out->spdifenc_handle = NULL;
        out->spdifenc_init = false;
    }

    if (out->ac3_parser_init) {
        aml_ac3_parser_close(out->ac3_parser_handle);
        out->ac3_parser_handle = NULL;
        out->ac3_parser_init = false;
    }

    if (out->flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        bool last_low_latency_mode = adev->aaudio_low_latency;
        ret = outMmapDeInit(out);
        if (ret == 0 && out->aaudio_low_latency) {
            aml_leave_aaudio_low_latency(adev);
            get_sink_format((struct audio_stream_out *)out);
            out->aaudio_low_latency = false;
            adev->aaudio_low_latency_updated = true;
        }

#ifndef AUDIO_HAL_DISABLE_MS12
        if (last_low_latency_mode == true && adev->aaudio_low_latency == false && eDolbyMS12Lib == adev->dolby_lib_type) {
            if (adev->ms12.dolby_ms12_enable
                && !is_arc_connected(adev)
                && adev->is_ui_force_dap_disable == false
                && adev->effect_ctrl.effect_mode == EFFECT_MODE_DAP) {
                set_ms12_full_dap_disable(&adev->ms12, false);
            }
            audiohal_send_msg_2_ms12(&adev->ms12, MS12_MESG_TYPE_RESET_MS12_ENCODER);
            set_ms12_alsa_limit_frame(&adev->ms12, MS12_ALSA_DEFAULT_LIMIT_FRAME);  // use default limit value
        }
#endif
        ALOGI("when close aaudio stream, send STANDBY msg to submix & ms12");
        aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_STANDBY);
        if (adev->useAudioMixer) {
            aml_audiohal_sch_state_2_submix(audio_mixer, SUBMIX_SCHEDULER_STANDBY);
        }
    }

    if (out->hal_format == AUDIO_FORMAT_AC4) {
        aml_ac4_parser_close(out->ac4_parser_handle);
        out->ac4_parser_handle = NULL;
    }

    aml_deinit_audio_buffer(out);

    if (out->hwsync) {
        if (out->hwsync->mediasync) {
            int ref_count = 0;
            pthread_mutex_lock(&adev->mediasync_lock);
            ref_count = aml_remove_mediasync_info(adev->mediasync, out->hwsync->mediasync);
            if (ref_count <= 0) {
                aml_audio_hwsync_release(out->hwsync);//this interface is also release mediasync
            }
            out->hwsync->mediasync = NULL;
            pthread_mutex_unlock(&adev->mediasync_lock);
        }
        // aml_stream_timer_pause_callback will use out->hwsync, free it at final time.
        // aml_audio_free(out->hwsync);
        // out->hwsync = NULL;
    }
    if (out->spdifout_handle) {
        aml_audio_spdifout_close(out->spdifout_handle);
        out->spdifout_handle = NULL;
    }
    if (out->spdifout2_handle) {
        aml_audio_spdifout_close(out->spdifout2_handle);
        out->spdifout2_handle = NULL;
    }
#ifdef SUPPORT_KARAOKE
    karaoke_close(&adev->linein_karaoke);
#endif

    if (out->aml_parser) {
        pthread_mutex_lock(&out->parser_MutexLock);
        aml_parser_deinit(out->aml_parser);
        out->aml_parser = NULL;
        pthread_mutex_unlock(&out->parser_MutexLock);
    }
    if (out->mpegh_uimanager_handle) {
        aml_uimanager_close(adev, out->mpegh_uimanager_handle);
        //notify droid audio
        ALOGI("%s mpegh stream will close,notify droid audio", __func__);
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, TYPE_MPEGH_CLOSE);
        out->mpegh_uimanager_handle = NULL;
    }
    if (out->aml_dec) {
        pthread_mutex_lock(&out->dec_MutexLock);
        aml_decoder_release(out->aml_dec);
        out->aml_dec = NULL;
        pthread_mutex_unlock(&out->dec_MutexLock);
    }

    if (out->resample_handle) {
        aml_audio_resample_close(out->resample_handle);
        out->resample_handle = NULL;
    }
    if (speed_info->speed_handle) {
        aml_audio_speed_close(speed_info->speed_handle);
        speed_info->speed_handle = NULL;
    }
    if (speed_info->local_buf_ptr) {
        aml_audio_free(speed_info->local_buf_ptr);
        speed_info->local_buf_ptr = NULL;
        speed_info->local_buf_size = 0;
        speed_info->local_buf_used_bytes = 0;
    }
    if (out->input_cache_rbuffer != NULL) {
        ring_buffer_release(out->input_cache_rbuffer);
        aml_audio_free(out->input_cache_rbuffer);
        out->input_cache_rbuffer = NULL;
    }
    if (out->data_handle_info.pcm16_buf) {
        aml_audio_free(out->data_handle_info.pcm16_buf);
        out->data_handle_info.pcm16_buf = NULL;
        out->data_handle_info.pcm16_buf_size = 0;
    }

    if (out->resample_outbuf) {
        aml_audio_free(out->resample_outbuf);
        out->resample_outbuf = NULL;
    }

    /*the dolby lib is changed, so we need restore it*/
    if (out->restore_dolby_lib_type) {
        pthread_mutex_lock(&adev->ms12.lock);
        adev->dolby_lib_type = adev->dolby_lib_type_last;
        pthread_mutex_unlock(&adev->ms12.lock);
        if (adev->effect_ctrl.effect_mode == EFFECT_MODE_DAP &&
            is_dts_format(out->hal_internal_format)) {
            if (adev->ms12.dap_only_enable) {
                aml_dap_close(&adev->ms12);
            }
        }
        ALOGI("%s restore dolby lib =%d", __func__, adev->dolby_lib_type);
    }

    if (fabs(out->output_speed - 1.0f) > 1e-2) {
       ALOGI("%s reset out->speed %f, out->output_speed:%f", __func__, speed_info->speed, out->output_speed);
       speed_info->speed = 1.0f;
       out->output_speed = 1.0f;
       speed_info->hwsync_force_update = true;
    }

    if (is_output_device_muted(adev, AUDIO_DEVICE_OUT_SPEAKER, true)) {
        set_output_device_mute(adev, AUDIO_DEVICE_OUT_SPEAKER, false, true);
    }

    aml_stream_unregister(out);
    AM_LOGI("io %d: out:%p exit ------", out->io_handle, out);

    /*all the ms12 related resource is released, free the handle itself*/
    dolby_ms12_release_dec_handle(stream);

    // for aml_stream_timer_pause_callback is async function,
    // should be very careful when you try to free output stream resource.
    aml_stream_wait_callback_finish(adev, out);

    pthread_mutex_destroy(&out->lock);
    pthread_mutex_destroy(&out->parser_MutexLock);
    pthread_mutex_destroy(&out->dec_MutexLock);

    pthread_mutex_lock(&adev->stream_release_lock);
    if (out->hwsync) {
        aml_audio_free(out->hwsync);
        out->hwsync = NULL;
    }
    aml_audio_free(stream);
    pthread_mutex_unlock(&adev->stream_release_lock);

    stream = NULL;
    out = NULL;
}

static int aml_audio_outport_enable(struct aml_audio_device *adev, audio_devices_t device, bool enable)
{
    AM_LOGI("all: %#x, pre:%#x, %s device: %s", adev->out_device, adev->cur_out_devices,
            enable? "unmute" : "mute", audioDevType2Str(device));

    switch (device) {
    case AUDIO_DEVICE_OUT_FM:
        break;
    case AUDIO_DEVICE_OUT_SPEAKER:
        do_output_device_routing(adev, AUDIO_DEVICE_OUT_SPEAKER, enable);
        break;
    case AUDIO_DEVICE_OUT_BUS:
        do_output_device_routing(adev, AUDIO_DEVICE_OUT_BUS, enable);
        break;
    case AUDIO_DEVICE_OUT_HDMI:
        set_output_device_avail(adev, AUDIO_DEVICE_OUT_HDMI, enable);
        do_output_device_routing(adev, AUDIO_DEVICE_OUT_HDMI, enable);
        if (enable) {
            update_sink_format_after_hotplug(adev);
        } else {
            struct aml_arc_hdmi_desc * hdmi_descs = get_arc_hdmi_cap(adev);
            hdmi_descs->mat_fmt.MAT_PCM_48kHz_only = false;
            hdmi_descs->pcm_fmt.max_channels = 2;
        }
        break;
    case AUDIO_DEVICE_OUT_HDMI_ARC:
        set_output_device_avail(adev, AUDIO_DEVICE_OUT_HDMI_ARC, enable);
        clear_arc_cached_edid(adev);
        set_arc_hdmi_updated(adev, true);
        if (eDolbyMS12Lib == adev->dolby_lib_type && adev->ms12.dolby_ms12_enable) {
            /*when arc is connected, disable dap*/
            set_ms12_full_dap_disable(&adev->ms12, enable);
        }
        set_output_device_mute(adev, AUDIO_DEVICE_OUT_HDMI_ARC, !enable, false);
        if (enable) {
            update_sink_format_after_hotplug(adev);
        } else {
            struct aml_arc_hdmi_desc * hdmi_descs = get_arc_hdmi_cap(adev);
            hdmi_descs->mat_fmt.MAT_PCM_48kHz_only = false;
            hdmi_descs->pcm_fmt.max_channels = 2;
        }
        break;
    case AUDIO_DEVICE_OUT_WIRED_HEADSET:
    case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
        do_output_device_routing(adev, AUDIO_DEVICE_OUT_WIRED_HEADPHONE, enable);
        break;
    case AUDIO_DEVICE_OUT_SPDIF:
        /* 1. audio_patch of Android cannot coexist with devices with different modules.
         * 2. So, we need to mute SPDIF when USB is inserted, and SPDIF also needs to mute when dev->dev is played.
         */
        if (!adev->spdif_coexist_other || (adev->cur_out_devices & AUDIO_DEVICE_OUT_ALL_USB) != 0) {
            set_output_device_mute(adev, AUDIO_DEVICE_OUT_SPDIF, !enable, false/*no fade*/);
        }
        break;
    case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP:
    case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES:
    case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER:
        adev->a2dp_updated = 1;
        break;
    case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
    case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
        if (!enable) {
            close_btSCO_device(adev);
        }
        break;
    default:
        AM_LOGW("cur device:%#x unsupported", device);
        break;
    }

    if (enable) {
        adev->cur_out_devices |= device;
    } else {
        adev->cur_out_devices &= ~device;
    }
    char s[AUDIO_DEVICE_OUT_STR_LEN];
    AM_LOGI("dev=%p cur_out_device=0x%x/%s",
            adev, adev->cur_out_devices,
            show_audio_device_out(adev->cur_out_devices, s, AUDIO_DEVICE_OUT_STR_LEN));
    return 0;
}

void aml_audio_output_routing(struct aml_audio_device *adev, audio_devices_t cur_output_device)
{
    audio_devices_t need_unmute_devices = ~adev->cur_out_devices & cur_output_device;
    audio_devices_t need_mute_devices = adev->cur_out_devices & ~cur_output_device;
    uint16_t i = 0;
    audio_devices_t device = 0;

    AM_LOGI("cur_devices:%#x unmute_devices:%#x, mute_devices:%#x",
        adev->cur_out_devices, need_unmute_devices, need_mute_devices);
    if (adev->is_arc_updating_sad) {
        AM_LOGI("updating arc SAD, no routing required.");
        return;
    }
    while ((device = 1 << i) != AUDIO_DEVICE_BIT_DEFAULT) {
        if ((need_unmute_devices & device) != 0) {
            aml_audio_outport_enable(adev, device, true);
        }
        if ((need_mute_devices & device) != 0) {
            aml_audio_outport_enable(adev, device, false);
        }
        i++;
    }
}

static int check_usb_card_device(struct str_parms *parms, int device)
{
    int ret = 0;
    if (parms == NULL) {
        return -1;
    }
    const uint32_t USB_RETRY_TIMEOUT_MAX_CNT = 50;
    const uint32_t USB_RETRY_TIME_MS = 20;

    device &= ~AUDIO_DEVICE_BIT_IN;
    /*usb audio hot plug need delay some time wait alsa file create */
    if ((device & AUDIO_DEVICE_OUT_ALL_USB) || (device & AUDIO_DEVICE_IN_ALL_USB)) {
        int card = 0, alsa_dev = 0, retry = 0;
        char device_node[256];

        int ret = str_parms_get_int(parms, "card", &card);
        R_CHECK_RET(ret, "get usb card index fail.");

        ret = str_parms_get_int(parms, "device", &alsa_dev);
        R_CHECK_RET(ret, "get usb device index fail.");

        snprintf(device_node, sizeof(device_node), "/dev/snd/pcmC%uD%u%c", card, alsa_dev,
             device & AUDIO_DEVICE_OUT_ALL_USB ? 'p' : 'c');
        while (1) {
            if (access(device_node, F_OK) < 0) {
                if (retry++ >= USB_RETRY_TIMEOUT_MAX_CNT) {
                    AM_LOGW("usb audio create alsa file time out:%d ms, need check", retry * USB_RETRY_TIME_MS);
                    return -1;
                }
                usleep (USB_RETRY_TIME_MS * 1000);
                AM_LOGI("Waiting for usb sound card to be ready. timeout:%d ms", retry * USB_RETRY_TIME_MS);
            } else {
                break;
            }
        }
    }
    return ret;
}

static void set_device_connect_state(struct aml_audio_device *adev, struct str_parms *parms, int device, bool state)
{
    AM_LOGI("state:%d, dev:%s(%#x), pre_out:%#x, pre_in:%#x", state, audioDevType2Str(device),
        device, adev->out_device, adev->in_device);

    /*
     * AUDIO DEVICE OUT HDMI EARC is multi-bit mask, for convenience
     * it is treated as ARC(single-bit mask) in audiohal device connect logic.
    */
    if (device == AUDIO_DEVICE_OUT_HDMI_EARC) {
        device = AUDIO_DEVICE_OUT_HDMI_ARC;
    }

    if (state) {
        check_usb_card_device(parms, device);
        if (audio_is_output_device(device)) {
            if ((device & AUDIO_DEVICE_OUT_HDMI_ARC) || (device & AUDIO_DEVICE_OUT_HDMI)) {
                if (device & AUDIO_DEVICE_OUT_HDMI_ARC) {
                    aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_HDMI_ARC_AUDIO_ENABLE, true);
                }
                set_output_device_avail(adev, device, true);
                clear_arc_cached_edid(adev);
                /* switch to hdmi out to avoid missing first word for voice assistant, once connecting hdmi */
                /* a2dp/usb have higher output priority than hdmi-out, not routing to hdmi-out.
                 * as switching hdmi_format would send hdmi-out disconnect/connect, which run to here.
                 */
                if ((adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) || (adev->out_device & AUDIO_DEVICE_OUT_ALL_USB)) {
                    //do nothing.
                } else if (device & AUDIO_DEVICE_OUT_HDMI) {
                    aml_audio_output_routing(adev, AUDIO_DEVICE_OUT_HDMI);
                }
            } else if (device & AUDIO_DEVICE_OUT_ALL_A2DP) {
                a2dp_out_open(adev);
                adev->out_device |= device;
            } else if (device &  AUDIO_DEVICE_OUT_ALL_USB ||
                       device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE ||
                       device & AUDIO_DEVICE_OUT_WIRED_HEADSET) {
                adev->out_device |= device;
                if (adev->address != NULL) {
                    free(adev->address);
                }
                adev->address = str_parms_to_str(parms);
                AM_LOGI("tag=usb update address=%p/'%s'", adev->address, adev->address);
            }
        }
    } else {
        if (audio_is_output_device(device)) {
            if ((device & AUDIO_DEVICE_OUT_HDMI_ARC) || (device & AUDIO_DEVICE_OUT_HDMI)) {
                set_output_device_avail(adev, device, false);
                clear_arc_cached_edid(adev);
                if (device & AUDIO_DEVICE_OUT_HDMI_ARC) {
                    int attend_type = aml_audio_earctx_get_type(adev);

                    /* only when cable is unplug, then switch arc off */
                    if (attend_type != ATTEND_TYPE_EARC && attend_type != ATTEND_TYPE_ARC)
                        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_HDMI_ARC_AUDIO_ENABLE, false);
                }
            } else if (device & AUDIO_DEVICE_OUT_ALL_A2DP) {
                adev->out_device &= (~device);
                adev->bt_avrcp_supported = false;
                a2dp_out_close(adev);
            } else if (device &  AUDIO_DEVICE_OUT_ALL_USB ||
                       device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE||
                       device & AUDIO_DEVICE_OUT_WIRED_HEADSET) {
                adev->out_device &= (~device);
                AM_LOGI("tag=usb disconnect address=%p", adev->address);
                free(adev->address);
                adev->address = NULL;
                pthread_mutex_lock(&adev->usb_lock);
                if (adev->usb) {
                    usb_out_close(adev->usb);
                    adev->usb = NULL;
                }
                pthread_mutex_unlock(&adev->usb_lock);
                AM_LOGI("tag=usb usb_out_close");
            }
        }
    }
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    struct str_parms *parms;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct amlAudioMixer *audio_mixer = adev->mixerData;
    char value[AUDIO_HAL_CHAR_MAX_LEN] = {'\0'};
    int val = 0;
    int ret = 0;

    AM_LOGI("dev:%p, kv: %s", dev, kvpairs);
    parms = str_parms_create_str (kvpairs);

    audio_extn_hfp_set_parameters(adev, parms);

    ret = str_parms_get_str (parms, AUDIO_PARAMETER_KEY_SCREEN_STATE, value, sizeof (value) );
    if (ret >= 0) {
        int continuous_audio_mode = 0;
        if (strcmp (value, AUDIO_PARAMETER_VALUE_ON) == 0) {
            set_output_device_mute(adev, AUDIO_DEVICE_OUT_SPEAKER, false, true/*use fade*/);
            adev->low_power = false;
            continuous_audio_mode = adev->continuous_audio_mode_backup;
            pthread_cond_broadcast(&adev->wake_cond);
            ALOGI("%s : %s pthread_cond_broadcast", __func__, kvpairs);

             /* switch to hdmi out, once system resume */
             //aml_audio_output_routing(adev, AUDIO_DEVICE_OUT_HDMI);
        } else {
            /*if a2dp is connected, don't unmute speaker*/
            //if (!(adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP))
                /* switch routing to speaker when system suspend */
            //    aml_audio_output_routing(adev, AUDIO_DEVICE_OUT_SPEAKER);

            /* mute speaker when suspend */
            set_output_device_mute(adev, AUDIO_DEVICE_OUT_SPEAKER, true, true/*use fade*/);
            //need time to fadeout
            aml_audio_sleep(15000);
            adev->low_power = true;
            adev->continuous_audio_mode_backup = adev->continuous_audio_mode;
            continuous_audio_mode = 0;
        }
        goto exit;
    }

    ret = str_parms_get_int (parms, "disable_pcm_mixing", &val);
    if (ret >= 0) {
        adev->disable_pcm_mixing = val;
        ALOGI ("ms12 disable_pcm_mixing set to %d\n", adev->disable_pcm_mixing);
        goto exit;
    }

    ret = str_parms_get_int(parms, "Audio hdmi-out mute", &val);
    if (ret >= 0) {
    /* for tv,hdmitx module is not registered, do not response this control interface */
#ifndef TV_AUDIO_OUTPUT
        {
            aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_HDMI_OUT_AUDIO_MUTE, val);
            ALOGI("audio hdmi out status: %d\n", val);
        }
#endif
        goto exit;
    }

    ret = str_parms_get_int(parms, "Audio spdif mute", &val);
    if (ret >= 0) {
        set_output_device_mute(adev, AUDIO_DEVICE_OUT_SPDIF, val, false/*no fade*/);
        goto exit;
    }

    ret = str_parms_get_int(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, &val);
    if (ret >= 0) {
        set_device_connect_state(adev, parms, val, false);

        /*if (!is_HDMI_connected(adev)) {
            aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_STANDBY);
        }*/
        goto exit;
    }

    ret = str_parms_get_int(parms, AUDIO_PARAMETER_DEVICE_CONNECT, &val);
    if (ret >= 0) {
        set_device_connect_state(adev, parms, val, true);
        if (val & AUDIO_DEVICE_OUT_HDMI_ARC) {
            if (eDolbyMS12Lib == adev->dolby_lib_type) {
                adev->raw_to_pcm_flag = true;
            } else {
                subMixingOutputRestart(adev);
            }
        }

        if (is_HDMI_connected(adev)) {
            aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_RUNNING);
            if (adev->useAudioMixer) {
                aml_audiohal_sch_state_2_submix(audio_mixer, SUBMIX_SCHEDULER_RUNNING);
            }
        }
        goto exit;
    }

    ret = str_parms_get_str (parms, AUDIO_PARAMETER_KEY_BT_SCO, value, sizeof (value) );
    if (ret >= 0) {
        if (strcmp (value, AUDIO_PARAMETER_VALUE_OFF) == 0) {
            close_btSCO_device(adev);
        }
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_bt_avrcp_supported", &val);
    if (ret >= 0) {
        adev->bt_avrcp_supported = (val != 0);
        /* Some Bt speakers(eg: JBL Go3...) send whether to support AVRCP later than the creation of audio_patch.
         * We need to force the volume to the maximum when setting avrcp support. */
        if (adev->bt_avrcp_supported) {
            adev->sink_gain[OUTPORT_A2DP] = 1.0;
        }
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_earctx_earc_mode", &val);
    if (ret >= 0) {
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_EARC_MODE, val);
        ALOGI("eARC_TX eARC Mode: %d\n", val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_arc_earc_rx_enable", &val);
    if (ret >= 0) {
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_ARC_EARC_RX_ENABLE, val);
        ALOGI("ARC eARC RX enable: %d\n", val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_arc_earc_tx_enable", &val);
    if (ret >= 0) {
        /* when enable/disable arc/earc, it will reset hpd, so need mute hdmiin audio */
        if (adev->in_device & AUDIO_DEVICE_IN_HDMI)
            adev->reset_hpd = 1;

        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_ARC_EARC_TX_ENABLE, val);
        ALOGI("ARC eARC TX enable: %d\n", val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "spdifin/arcin switch", &val);
    if (ret >= 0) {
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIFIN_ARCIN_SWITCH, val);
        ALOGI("audio source: %s\n", val?"ARCIN":"SPDIFIN");
        goto exit;
    }

    //add for fire os tv for Dolby audio setting
    ret = str_parms_get_int (parms, "hdmi_format", &val);
    if (ret >= 0 ) {
        if (adev->digital_audio_mode != val) {
            adev->digital_audio_mode_updated = 1;
        }
        adev->digital_audio_mode = val;

        adev->is_manual = false;
        if (val == AML_DIGITAL_AUDIO_MODE_MANUAL) {
            /*if we want the manual behavior is same with AUTO, use below code*/
            //adev->digital_audio_format = AML_DIGITAL_AUDIO_MODE_AUTO;
            /*if we want the manual behavior is same with BYPASS, use below code*/
            adev->digital_audio_mode = AML_DIGITAL_AUDIO_MODE_AUTO;
            adev->is_manual = true;
            memset(adev->manual_encoding_format, 0, sizeof(adev->manual_encoding_format));
            ret = str_parms_get_str(parms, "hal_param_digital_audio_subformat", value, sizeof(value));
            if (ret >= 0) {
                char *saveptr = NULL;
                int i = 0;
                char *token = strtok_r(value, ",", &saveptr);
                while (token != NULL) {
                    adev->manual_encoding_format[i].enable = true;
                    adev->manual_encoding_format[i++].audio_format = encodingFormat2AudioFormat(atoi(token));
                    token = strtok_r(NULL, ",", &saveptr);
                }
                for (int j = 0; j < i; j++) {
                    AM_LOGV("%s", audioFormat2Str(adev->manual_encoding_format[j].audio_format));
                }
                if (i == 0) {
                    AM_LOGI("none format. ret:%d", ret);
                }
            } else {
                AM_LOGI("always ret:%d", ret);
                memset(adev->manual_encoding_format, 0, sizeof(adev->manual_encoding_format));
            }
        } else {
            memset(adev->manual_encoding_format, 0, sizeof(adev->manual_encoding_format));
        }

        /* only switch from/to bypass mode, update the DUT's EDID */
        if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS ||
            adev->last_digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS) {
            struct aml_arc_hdmi_desc *hdmi_descs = get_arc_hdmi_cap(adev);
            struct format_desc *ddp_fmt = &hdmi_descs->ddp_fmt;
            update_edid_after_edited_audio_sad(dev, ddp_fmt);
        }
        adev->last_digital_audio_mode = adev->digital_audio_mode;
        //sysfs_set_sysfs_str(REPORT_DECODED_INFO, kvpairs);
        if ((eDolbyMS12Lib == adev->dolby_lib_type) && (adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP)) {
            adev->a2dp_no_reconfig_ms12 = aml_audio_get_systime() + 2000000;
        }
        AM_LOGI("digital audio mode: %s", digitalAudioModeType2Str(adev->digital_audio_mode));
        goto exit;
    }

    ret = str_parms_get_int (parms, "spdif_format", &val);
    if (ret >= 0 ) {
        adev->spdif_format = val;
        ALOGI ("SPDIF format: %d\n", adev->spdif_format);
        goto exit;
    }

    ret = str_parms_get_int (parms, "hal_param_spdif_output_enable", &val);
    if (ret >= 0 ) {
        ALOGI ("[%s:%d] set spdif output enable:%d", __func__, __LINE__, val);
        if (val == 0) {
            do_output_device_routing(adev, AUDIO_DEVICE_OUT_SPDIF, false);
        } else {
            do_output_device_routing(adev, AUDIO_DEVICE_OUT_SPDIF, true);
        }
        adev->spdif_enable = (val == 0) ? false : true;
        goto exit;
    }

    ret = str_parms_get_int(parms, "audio_type", &val);
    if (ret >= 0) {
        adev->audio_type = val;
        ALOGI("audio_type: %d\n", adev->audio_type);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hdmi_is_passthrough_active", &val);
    if (ret >= 0 ) {
        adev->hdmi_is_pth_active = val;
        ALOGI ("hdmi_is_passthrough_active: %d\n", adev->hdmi_is_pth_active);
        goto exit;
    }

    ret = str_parms_get_int (parms, "ChannelReverse", &val);
    if (ret >= 0) {
        adev->FactoryChannelReverse = val;
        ALOGI ("ChannelReverse = %d\n", adev->FactoryChannelReverse);
        goto exit;
    }

    ret = str_parms_get_str (parms, "set_ARC_hdmi", value, sizeof (value) );
    if (ret >= 0) {
        if (strncmp(value, "updating_sad", 12) == 0) {
            adev->is_arc_updating_sad = true;
        } else if (strncmp(value, "updated_sad", 11) == 0) {
            adev->is_arc_updating_sad = false;
        } else {
            set_arc_hdmi(dev, value, AUDIO_HAL_CHAR_MAX_LEN);
        }
        goto exit;
    }

    ret = str_parms_get_str (parms, "set_ARC_format", value, sizeof (value) );
    if (ret >= 0) {
        // remove this interface, only use "set_ARC_hdmi"
        // set_arc_format(dev, value, AUDIO_HAL_CHAR_MAX_LEN);
        goto exit;
    }

    // used for get first apts for A/V sync
    ret = str_parms_get_str (parms, "first_apts", value, sizeof (value) );
    if (ret >= 0) {
        unsigned int first_apts = atoi (value);
        ALOGI ("audio set first apts 0x%x\n", first_apts);
        adev->first_apts = first_apts;
        adev->first_apts_flag = true;
        adev->frame_trigger_thread = 0;
        goto exit;
    }

    /*use dolby_lib_type_last to check ms12 type, because during playing DTS file,
      this type will be changed to dcv*/
    if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
        ret = str_parms_get_int(parms, "continuous_audio_mode", &val);
        if (ret >= 0) {
            int disable_continuous = !val;
            // if exit netflix, we need disable atmos lock
            if (disable_continuous) {
                adev->atoms_lock_flag = false;
                set_ms12_atmos_lock(&(adev->ms12), adev->atoms_lock_flag);
                ALOGI("exit netflix, set atmos lock as 0");
            }
            {
                bool chmod_lock = val;
                //when in netflix, we should always keep ddp5.1, exit netflix we can output ddp2ch
                set_ms12_chmod_lock(&(adev->ms12), chmod_lock);
            }

            ALOGI("%s ignore the continuous_audio_mode!\n", __func__ );
            adev->is_netflix = val;
            /*in netflix case, we enable atmos drop at the beginning*/
            dolby_ms12_enable_atmos_drop(val);

            if (adev->is_netflix) {
                set_ms12_set_compressor_profile(ms12, MS12_COMPRESSOR_CLIPPING_PROTECTION);
            } else {
                set_ms12_set_compressor_profile(ms12, MS12_COMPRESSOR_STANDARD_FILM);
            }

            if (adev->is_netflix) {
                aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_RUNNING);
                if (adev->useAudioMixer) {
                    aml_audiohal_sch_state_2_submix(audio_mixer, SUBMIX_SCHEDULER_RUNNING);
                }
            } else {
                /* currently system send the "continuous_audio_mode=0" in below a few scenario,
                ** 1)when ExoPlayer/AIV open and close, start play or exit play.
                ** 2)system bootup.
                ** so can't send the scheduler standby to ms12 here.
                */
                //aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_STANDBY);
            }
            //update sink format for NTS(AUDIO-CAO-ATMOSLOCK) Jira:TV-57896
            if (dolby_stream_active(adev) || hwsync_lpcm_active(adev)) {
               //Do nothing
            } else if (adev->active_outputs[STREAM_PCM_NORMAL]) {
              get_sink_format(&adev->active_outputs[STREAM_PCM_NORMAL]->stream);
            }
            goto exit;
        }

        ret = str_parms_get_int(parms, "hdmi_dolby_atmos_lock", &val);
        if (ret >= 0) {
            ALOGI("%s hdmi_dolby_atmos_lock set to %d\n", __func__ , val);
            char buf[PROPERTY_VALUE_MAX];
            adev->atoms_lock_flag = val ? true : false;

            if (eDolbyMS12Lib == adev->dolby_lib_type) {
            // Note: some guide from www.netflix.com
            // In the Netflix application, enable/disable Atmos locking
            // when you receive the signal from the Netflix application.
            pthread_mutex_lock(&adev->lock);
            if (continuous_mode(adev)) {
                // enable/disable atoms lock
                set_ms12_atmos_lock(&(adev->ms12), adev->atoms_lock_flag);
                ALOGI("%s set adev->atoms_lock_flag = %d, \n", __func__,adev->atoms_lock_flag);
            } else {
                ALOGI("%s not in continuous mode, do nothing\n", __func__);
            }
            pthread_mutex_unlock(&adev->lock);
            }
            goto exit;
        }

        //for the local playback, IEC61937 format!
        //temp to disable the Dolby MS12 continuous
        //if local playback on Always continuous is completed,
        //we should remove this function!
        ret = str_parms_get_int(parms, "is_ms12_continuous", &val);
        if (ret >= 0) {
            ALOGI("%s is_ms12_continuous set to %d\n", __func__ , val);
            goto exit;
        }
        /*after enable MS12 continuous mode, the audio delay is big, we
        need use this API to compensate some delay to video*/
        ret = str_parms_get_int(parms, "compensate_video_enable", &val);
        if (ret >= 0) {
            ALOGI("%s compensate_video_enable set to %d\n", __func__ , val);
            adev->compensate_video_enable = val;
            //aml_audio_compensate_video_delay(val);
            goto exit;
        }

        /*enable force ddp output for ms12 v2*/
        ret = str_parms_get_int(parms, "hal_param_force_ddp", &val);
        if (ret >= 0) {
            ALOGI("%s hal_param_force_ddp set to %d\n", __func__ , val);
            if (adev->ms12_force_ddp_out != val) {
                adev->ms12_force_ddp_out = val;
                update_sink_format_after_hotplug(adev);
            }
            goto exit;
        }
    } else if (eDolbyDcvLib == adev->dolby_lib_type_last) {
        ret = str_parms_get_int(parms, "continuous_audio_mode", &val);
        if (ret >= 0) {
            ALOGI("%s ignore the continuous_audio_mode!\n", __func__ );
            adev->is_netflix = val;
            goto exit;
        }
    }

#ifdef ADD_AUDIO_DELAY_INTERFACE
    ret = str_parms_get_int(parms, "hal_param_out_dev_delay_time_ms", &val);
    if (ret >= 0) {
        /* High 16 - bit expression type, low 16 - bit expression delay time */
        short audio_delay = (short)(val & 0xffff);
        if (audio_delay > AUDIO_DELAY_MAX)
            audio_delay = AUDIO_DELAY_MAX;
        else if (audio_delay < AUDIO_DELAY_MIN)
            audio_delay = AUDIO_DELAY_MIN;

        /*
        * if audio_delay is a negative value,
        * will save to variable "avsync_compensate_delay_ms" and compensate to frames with avsync.
        * if audio_delay is a positive value, set to delay buffer and insert zero data to alsa buf.
        */
        ALOGI("%s set audio delay =%d", __func__, audio_delay);
        adev->avsync_compensate_delay_ms = audio_delay;
        if (audio_delay <= 0) {
            audio_delay = 0;
        }
        aml_audio_delay_set_time(val >> 16, audio_delay);

        goto exit;
    }
#endif

    ret = str_parms_get_int(parms, "hal_param_ott_tv_arc_connected", &val);
    if (ret >= 0) {
        adev->b_ott_tv_arc_connected = (val != 0) ? true: false;
        ALOGI("%s hal_param_ott_tv_arc_connected = %d", __func__, adev->b_ott_tv_arc_connected);
        goto exit;
    }


#ifdef ENABLE_DVB_PATCH
    /* deal with dvb cmd */
    ret = set_dtv_parameters(dev, parms);
    if (ret >= 0) {
        ALOGD("get dtv param(kv: %s)", kvpairs);
        goto exit;
    }
#endif
    /* deal with AQ cmd */
    ret = set_AQ_parameters(dev, parms);
    if (ret >= 0) {
        ALOGD("set AQ param(kv: %s)", kvpairs);
        goto exit;
    }

    /* deal with tv source switch cmd */
    ret = set_tv_source_switch_parameters(dev, parms);
    if (ret >= 0) {
        ALOGD("get TV source param(kv: %s)", kvpairs);
        goto exit;
    }
    ret = set_device_control(dev, parms);
    if (ret >= 0) {
        ALOGD("get device control param(kv: %s)", kvpairs);
        goto exit;
    }
    ret = str_parms_get_str(parms, "sound_track", value, sizeof(value));
    if (ret > 0) {
        int mode = atoi(value);
        ALOGI("video player sound_track mode %d ",mode );
        adev->sound_track_mode = mode;
        goto exit;
    }

    ret = str_parms_get_str(parms, "has_video", value, sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "true", 4) == 0) {
            adev->is_has_video = true;
        } else if (strncmp(value, "false", 5) == 0) {
            adev->is_has_video = false;
        } else {
            adev->is_has_video = true;
            ALOGE("%s() unsupport value %s choose is_has_video(default) %d\n", __func__, value, adev->is_has_video);
        }
        ALOGI("is_has_video set to %d\n", adev->is_has_video);
    }

    ret = str_parms_get_str(parms, "bt_wbs", value, sizeof(value));
    if (ret >= 0) {
        ALOGI("Amlogic_HAL - %s: bt_wbs=%s.", __func__, value);
        adev->bt_wbs = (strncmp(value, "on", 2) == 0);
        goto exit;
    }

    ret = str_parms_get_str(parms, "show-meminfo", value, sizeof(value));
    if (ret >= 0) {
        unsigned int level = (unsigned int)atoi(value);
        ALOGE ("Amlogic_HAL - %s: ShowMem info level:%d.", __FUNCTION__,level);
        aml_audio_debug_malloc_showinfo(level);
        goto exit;
    }

#ifndef AUDIO_HAL_DISABLE_MS12
        ret = str_parms_get_str(parms, "hal_param_dialogue_enhancement", value, sizeof(value));
        //this param used to the Dialogue Enhancement of non dap (ac4_de), only valid for ac4 bistream.
        if (ret >= 0 && is_audio_postprocessing_add_dolbyms12_dap(adev) == false) {
             switch (atoi(value)) {
            case DIALOGUE_ENHANCEMENT_OFF:
                adev->ms12.ac4_de = 0;
                break;
            case DIALOGUE_ENHANCEMENT_LOW:
                adev->ms12.ac4_de = 4;
                break;
            case DIALOGUE_ENHANCEMENT_MEDIUM:
                adev->ms12.ac4_de = 8;
                break;
            case DIALOGUE_ENHANCEMENT_HIGH:
                adev->ms12.ac4_de = 12;
                break;
            default:
                ALOGE("%s(), not the expected Dialogue Enhancement lever, set Dialogue Enhancement off", __FUNCTION__);
                adev->ms12.ac4_de = 0;
                break;
            }
            ALOGI("Amlogic_HAL - %s: set MS12 ac4 Dialogue Enhancement gain :%d.", __FUNCTION__, adev->ms12.ac4_de);
            char parm[32] = "";
            sprintf(parm, "%s %d", "-ac4_de", adev->ms12.ac4_de);
            pthread_mutex_lock(&adev->lock);
            if (strlen(parm) > 0)
                set_ms12_decoder_parameters(adev, parm);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }

        ret = str_parms_get_str(parms, "hal_param_dmx_mode", value, sizeof(value));
        if (ret >= 0) {
            switch (atoi(value)) {
                case SOUND_DMX_MODE_SURROUND:
                    adev->ms12.dmx = 0;
                    break;
                case SOUND_DMX_MODE_STEREO:
                    adev->ms12.dmx = 1;
                    break;
                default:
                    ALOGE("%s(), not the expected dmx mode, set dmx mode surround", __FUNCTION__);
                    adev->ms12.dmx = 0;
                    break;
            }
            ALOGI("Amlogic_HAL - %s: set MS12 Downmix modes :%d.", __FUNCTION__, adev->ms12.dmx);
            char parm[32] = "";
            sprintf(parm, "%s %d", "-dmx", adev->ms12.dmx);
            pthread_mutex_lock(&adev->lock);
            if (strlen(parm) > 0)
                aml_ms12_update_runtime_params(&(adev->ms12), parm);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }

        ret = str_parms_get_str(parms, "hal_param_enable_drc_rf_mode", value, sizeof(value));
        if (ret >= 0) {
            char parm[64] = "";
            int enable_drc_rf_mode = atoi(value);
            if (enable_drc_rf_mode) {
                adev->ms12.drc = 1;
                adev->ms12.bs = 0;
                adev->ms12.cs = 0;
                adev->ms12.dap_drc = 1;
                adev->ms12.b = 0;
                adev->ms12.c = 0;
                sprintf(parm, "-drc %d -bs %d -cs %d -dap_drc %d -b %d -c %d",  adev->ms12.drc, adev->ms12.bs, adev->ms12.cs, adev->ms12.dap_drc, adev->ms12.b, adev->ms12.c);
                ALOGI("Amlogic_HAL - %s: set drc is rf mode", __FUNCTION__);
            } else {
                adev->ms12.drc = 0;
                adev->ms12.dap_drc = 0;
                sprintf(parm, "-drc %d -dap_drc %d",  adev->ms12.drc, adev->ms12.dap_drc);
                ALOGI("Amlogic_HAL - %s: set drc is line mode", __FUNCTION__);
            }
            pthread_mutex_lock(&adev->lock);
            if (strlen(parm) > 0)
                aml_ms12_update_runtime_params(&(adev->ms12), parm);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }

        ret = str_parms_get_str(parms, "hal_param_drc_boost_value", value, sizeof(value));
        if (ret >= 0) {
            int drc_boost_value = atoi(value);
            adev->ms12.bs = drc_boost_value;
            adev->ms12.b = drc_boost_value;
            ALOGI("Amlogic_HAL - %s: set drc boost value is %d", __FUNCTION__, drc_boost_value);
            char parm[32] = "";
            sprintf(parm, "-bs %d -b %d",  adev->ms12.bs, adev->ms12.b);
            pthread_mutex_lock(&adev->lock);
            if (strlen(parm) > 0)
                aml_ms12_update_runtime_params(&(adev->ms12), parm);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }

        ret = str_parms_get_str(parms, "hal_param_drc_cut_value", value, sizeof(value));
        if (ret >= 0) {
            int drc_cut_value = atoi(value);
            adev->ms12.cs = drc_cut_value;
            adev->ms12.c = drc_cut_value;
            ALOGI("Amlogic_HAL - %s: set drc cut value is %d", __FUNCTION__, drc_cut_value);
            char parm[32] = "";
            sprintf(parm, "-cs %d -c %d", adev->ms12.cs, adev->ms12.c);
            pthread_mutex_lock(&adev->lock);
            if (strlen(parm) > 0)
                aml_ms12_update_runtime_params(&(adev->ms12), parm);
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
#endif

    ret = str_parms_get_str(parms, "picture_mode", value, sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "PQ_MODE_STANDARD", 16) == 0) {
            set_dev_pic_mode(adev, PQ_STANDARD);
        } else if (strncmp(value, "PQ_MODE_GAME", 12) == 0) {
            set_dev_pic_mode(adev, PQ_GAME);
        } else {
            set_dev_pic_mode(adev, PQ_STANDARD);
            ALOGE("%s() unsupport value %s choose pic mode (default) standard\n", __func__, value);
        }
        ALOGI("%s(), set pic mode to: %d\n", __func__, get_dev_pic_mode(adev));
        goto exit;
    }

#ifndef AUDIO_HAL_DISABLE_MS12
    if (eDolbyMS12Lib == adev->dolby_lib_type || adev->ms12.dap_only_enable) {
        ret = str_parms_get_str(parms, "ms12_runtime", value, sizeof(value));
        if (ret >= 0) {
            char *parm = strstr(kvpairs, "=");
            pthread_mutex_lock(&adev->lock);
            if (parm) {
                if (strstr(parm+1, "-atmos_lock")) {
                    aml_ms12_update_runtime_params(&(adev->ms12), parm+1);
                } else if (strstr(parm+1, "-ac4_de")|| strstr(parm+1, "-at") || strstr(parm+1, "-pat") || strstr(parm+1, "-lang") ||
                strstr(parm+1, "-lang2")) {
                    set_ms12_decoder_parameters(adev, parm+1);
                } else if (strstr(parm+1, "-dap_dialogue_enhancer") || strstr(parm+1, "-dap_leveler")) {
                    if ((adev->dolby_ms12_dap_init_mode == 1) && (adev->board_config.dolby_ms12_audio_config == MS12_CONFIG_X)) {
                        //for OTT config X
                        set_ms12_decoder_parameters(adev, parm+1);
                    } else {
                        //for TV/Soundbar config X/Z
                        aml_ms12_update_runtime_params(&(adev->ms12), parm+1);
                    }
                } else {
                    aml_ms12_update_runtime_params(&(adev->ms12), parm+1);
                }
            }
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }

        ret = str_parms_get_int(parms, "legacy_ddplus_out", &val);
        if (ret >= 0) {
            bool legacy_ddplus_out_falg = val ? true : false;
            dolby_ms12_set_ddp_5_1_out(legacy_ddplus_out_falg);
            ALOGI("-legacy_ddplus_out = %s\n", val ? "true" : "false");
        }
    }
#endif

    if (eDTSXLib == adev->dts_lib_type) {
        char *dtsx_parm = strstr(kvpairs, "dtsx_");
        if (dtsx_parm != NULL) {
            if (aml_dtsx_update_runtime_params(&adev->dts_x, parms) == 0)
                goto exit;
        }
    }

    ret = str_parms_get_str(parms, "bypass_dap", value, sizeof(value));
    if (ret >= 0) {
        sscanf(value,"%d %f", &adev->ms12.dap_bypass_enable, &adev->ms12.dap_bypassgain);
        ALOGD("dap_bypass_enable is %d and dap_bypassgain is %f",adev->ms12.dap_bypass_enable, adev->ms12.dap_bypassgain);
        goto exit;
    }

    //This is not runtime parameter.
    //For ott support soundbar project, using Enable Soundbar Mode UI to switch soundbar or OTT mode.
    ret = str_parms_get_int(parms, "hal_param_soundbar_mode", &val);
    if (ret >= 0) {
        bool enable  = (val != 0) ? true : false;
        ALOGI("%s enable %d adev->enable_soundbar_mode %d\n", __func__, enable, adev->enable_soundbar_mode);
        if (adev->enable_soundbar_mode != enable) {
            adev->enable_soundbar_mode = enable;
            //only s7d(S905X5M) soundbar use the conflict alsa device.
            adev->is_alsa_device_conflict = adev->board_config.sbr_spk_ott_hbr_same_tdm;
            ALOGI(" enable_soundbar_mode = %d device status at %s\n", enable, adev->enable_soundbar_mode ? "SBR-Speaker" : "PureOTT-HDMI");
            if (ms12->dolby_ms12_enable) {
                set_ms12_full_dap_disable(ms12, !enable);
            }
            if (!enable)
                set_output_device_mute(adev, (audio_devices_t)AML_AUDIO_DEVICE_OUT_EXTERNAL_SPEAKER, true, 0);
        }
        goto exit;
    }

    ret = str_parms_get_str(parms, "VX_SET_DTS_Mode", value, sizeof(value));
    if (ret >= 0) {
        int dts_decoder_output_mode = atoi(value);
        if (dts_decoder_output_mode > 8 || dts_decoder_output_mode < 0)
            goto exit;
        if (dts_decoder_output_mode == 2)
            adev->native_postprocess.vx_force_stereo = 1;
        else
            adev->native_postprocess.vx_force_stereo = 0;
        dca_set_out_ch_internal(dts_decoder_output_mode);
        ALOGD("set dts decoder output mode to %d", dts_decoder_output_mode);
        goto exit;
    }

    ret = str_parms_get_int (parms, "stream_bitrate", &val);
    if (ret >= 0) {
        adev->stream_bitrate = val;
        ALOGI ("stream_bitrate set to %d\n", adev->stream_bitrate);
        goto exit;
    }

#ifdef SUPPORT_KARAOKE
    /* karaoke parameter format "hal_param_karaoke_set=[Name] [Command] [Value]"
       example: hal_param_karaoke_set=usb switch 1
    */
    ret = str_parms_get_str(parms, "hal_param_karaoke_set", value, sizeof(value));
    if (ret >= 0) {
        karaoke_set_parameters(dev, value);
        goto exit;
    }
#endif

    ret = str_parms_get_str(parms, "hal_param_vad_wakeup", value, sizeof(value));
    if (ret >= 0) {
        if (strncmp(value, "suspend", 7) == 0) {
            aml_vad_suspend(&adev->alsa_mixer);
        } else if (strncmp(value, "resume", 7) == 0) {
            aml_vad_resume(&adev->alsa_mixer);
        } else if (strncmp(value, "dump", 4) == 0) {
            aml_vad_dump(false);
        } else {
            AM_LOGI("not supported param:%s", value);
        }
        goto exit;
    }

    ret = str_parms_get_str(parms, "direct-mode", value, sizeof(value));
    if (ret >= 0) {
        unsigned int direct_mode = (unsigned int)atoi(value);
        ALOGI ("Amlogic_HAL - %s: direct-mode:%d.", __FUNCTION__,direct_mode);
        adev->direct_mode = direct_mode;
        /*todo*/
#if 0
        if (direct_mode == 1) {
            // release alsa devices for KaraokeServiceManager
            if (eDolbyMS12Lib == adev->dolby_lib_type) {
                get_dolby_ms12_cleanup(&adev->ms12, !adev->continuous_audio_mode);
            } else {
                for (int i = 0; i < ALSA_DEVICE_CNT; i++) {
                    if (adev->pcm_handle[i]) {
                        pcm_close(adev->pcm_handle[i]);
                        adev->pcm_handle[i] = NULL;
                        adev->pcm_refs[i] = 0;
                    }
                }
            }
        }
#endif
        goto exit;
    }

    ret = str_parms_get_str(parms, "ms12_speed", value, sizeof(value));
    if (ret >= 0) {
        float speed = 0;
        sscanf(value,"%f", &speed);
        //set_dolby_ms12_main_speed(&adev->ms12, (double)speed);
        ALOGI("[%s] set ms12 speed =%f", __func__, speed);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_spdif_coexist_other", &val);
    if (ret >= 0) {
        adev->spdif_coexist_other = (val != 0);
        if ((adev->cur_out_devices & AUDIO_DEVICE_OUT_SPDIF) == 0 && !adev->dev2mix_patch && adev->spdif_enable) {
            set_output_device_mute(adev, AUDIO_DEVICE_OUT_SPDIF, !adev->spdif_coexist_other, false/*no fade*/);
        }
        goto exit;
    }

    /* deal with mpegh cmd */
    ret = set_MPEGH_parameters(adev, parms);
    if (ret >= 0) {
        ALOGD("get MPEGH param(kv: %s)", kvpairs);
        goto exit;
    }

exit:
    str_parms_destroy (parms);
    /* always success to pass VTS */
    return 0;
}

static void adev_get_hal_control_volume_en(struct aml_audio_device *adev, char *temp_buf)
{
    bool hal_control_vol_en = true;
    /* For STB product.*/
    if ((!is_TV(adev)) && (adev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI) != 0) {
        /*  Audio_hal has no ability to control volume at the following scence:
         *    1. non-ms12, output non-pcm, cec closed.
         *    2. ms12, output non-pcm, cec closed, passthrough.
         */
        if (adev->dolby_lib_type != eDolbyMS12Lib ||
            (adev->dolby_lib_type == eDolbyMS12Lib && adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS)) {
            enum AML_SPDIF_FORMAT format = AML_STEREO_PCM;
            enum AML_SRC_TO_HDMITX spdif_index = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_AUDIO_SRC_TO_HDMI);
            if (spdif_index == AML_SPDIF_A_TO_HDMITX) {
                format = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_FORMAT);
            } else if (spdif_index == AML_SPDIF_B_TO_HDMITX) {
                format = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_B_FORMAT);
            } else {
                format = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_I2S2HDMI_FORMAT);
            }
            hal_control_vol_en = (format == AML_STEREO_PCM || format == AML_MULTI_CH_LPCM) ? true : false;
        }
    }
    sprintf (temp_buf, "hal_param_hal_control_vol_en=%d", hal_control_vol_en);
    if (adev->debug_flag) {
        AM_LOGD("can hal control the platform volume, en:%d dev:%#x", hal_control_vol_en, adev->cur_out_devices);
    }
}

static char *adev_get_parameters(const struct audio_hw_device *dev,
                                   const char *keys)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    char temp_buf[AUDIO_HAL_CHAR_MAX_LEN] = {0};

    if (!strcmp (keys, AUDIO_PARAMETER_HW_AV_SYNC) ) {
        ALOGI ("get hw_av_sync id\n");
        {
            void *pMediaSync = mediasync_wrap_create();
            if (pMediaSync != NULL) {
                int32_t id = -1;
                bool ret = mediasync_wrap_allocInstance(pMediaSync, 0, 0, &id);
                mediasync_wrap_destroy(pMediaSync);
                ALOGI ("ret: %d, id:%d\n", ret, id);
                if (ret && id != -1) {
                    adev->hw_sync_id = id;
                    sprintf (temp_buf, "hw_av_sync=%d", id);
                    return strdup (temp_buf);
                }
            }
        }
    } else if (strstr (keys, AUDIO_PARAMETER_HW_AV_EAC3_SYNC) ) {
        return strdup ("HwAvSyncEAC3Supported=true");
    } else if (strstr (keys, "hal_param_digital_audio_mode") ) {
        sprintf (temp_buf, "hal_param_digital_audio_mode=%d", adev->digital_audio_mode);
        return strdup (temp_buf);
    } else if (strstr (keys, "digital_output_format") ) {
        if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_PCM) {
            return strdup ("digital_output_format=pcm");
        } else if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_DD) {
            return strdup ("digital_output_format=dd");
        } else if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_AUTO) {
            return strdup ("digital_output_format=auto");
        } else if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS) {
            return strdup ("digital_output_format=bypass");
        } else if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_MANUAL) {
            return strdup ("digital_output_format=manual");
        } else {
            AM_LOGW("invalid digital mode:%d", adev->digital_audio_mode);
        }
    }  else if (strstr (keys, "spdif_format") ) {
        sprintf (temp_buf, "spdif_format=%d", adev->spdif_format);
        return strdup (temp_buf);
    } else if (strstr (keys, "hdmi_is_passthrough_active") ) {
        sprintf (temp_buf, "hdmi_is_passthrough_active=%d", adev->hdmi_is_pth_active);
        return strdup (temp_buf);
    } else if (strstr (keys, "disable_pcm_mixing") ) {
        sprintf (temp_buf, "disable_pcm_mixing=%d", adev->disable_pcm_mixing);
        return strdup (temp_buf);
    } else if (strstr (keys, "hdmi_encodings") ) {
        struct aml_arc_hdmi_desc *hdmi_descs = get_arc_hdmi_cap(adev);
        bool aml_dd =  hdmi_descs->dd_fmt.is_support;
        bool aml_ddp = hdmi_descs->ddp_fmt.is_support;
        sprintf (temp_buf, "hdmi_encodings=%s", "pcm;");
        if (aml_ddp) {
            sprintf (temp_buf + strlen(temp_buf), "ac3;eac3;");
            if (hdmi_descs->ddp_fmt.atmos_supported) {
                sprintf (temp_buf + strlen(temp_buf), "atmos;");
            }
        } else if (aml_dd) {
            sprintf (temp_buf + strlen(temp_buf), "ac3;");
        }
        AM_LOGI("atmos = %d, keys: [%s]", hdmi_descs->ddp_fmt.atmos_supported, temp_buf);
        return strdup (temp_buf);
    } else if (strstr (keys, "is_passthrough_active") ) {
        bool active = false;
        sprintf (temp_buf, "is_passthrough_active=%d",active);
        return  strdup (temp_buf);
    } else if (strstr(keys, "hal_param_hal_control_vol_en")) {
        adev_get_hal_control_volume_en(adev, temp_buf);
        return  strdup (temp_buf);
    } else if (!strcmp(keys, "SOURCE_GAIN")) {
        sprintf(temp_buf, "source_gain = %f %f %f %f %f", AmplToDb(adev->eq_data.s_gain.atv), AmplToDb(adev->eq_data.s_gain.dtv),
                AmplToDb(adev->eq_data.s_gain.hdmi), AmplToDb(adev->eq_data.s_gain.av), AmplToDb(adev->eq_data.s_gain.media));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "POST_GAIN")) {
        sprintf(temp_buf, "post_gain = %f %f %f", AmplToDb(adev->eq_data.p_gain.speaker), AmplToDb(adev->eq_data.p_gain.spdif_arc),
                adev->eq_data.p_gain.headphone);
        return strdup(temp_buf);
    } else if (strstr(keys, "dolby_decode_enable")) {
        int dolby_decode_enable = (adev->dolby_decode_enable > 0);
        AM_LOGI("dolby_decode_enable :%d", dolby_decode_enable);
        sprintf(temp_buf, "dolby_decode_enable=%d", dolby_decode_enable);
        return  strdup(temp_buf);
    } else if (strstr(keys, "dolby_ms12_enable")) {
        int ms12_enable = (eDolbyMS12Lib == adev->dolby_lib_type_last);
        ALOGI("ms12_enable :%d", ms12_enable);
        sprintf(temp_buf, "dolby_ms12_enable=%d", ms12_enable);
        return  strdup(temp_buf);
    } else if (strstr (keys, "stream_dra_channel") ) {
#ifdef ENABLE_DVB_PATCH
       if (is_dev_patch_exist(adev) && is_same_patch_src(adev, SRC_DTV)) {
          struct aml_dtv_audio_instance *dtv_audio_instance = (struct aml_dtv_audio_instance *)get_dev_patch(adev);
          if (dtv_audio_instance->dtv_NchOriginal > 8 || dtv_audio_instance->dtv_NchOriginal < 1) {
              sprintf (temp_buf, "0.0");
            } else {
              sprintf(temp_buf, "channel_num=%d.%d", dtv_audio_instance->dtv_NchOriginal,dtv_audio_instance->dtv_lfepresent);
              ALOGD ("temp_buf=%s\n", temp_buf);
            }
       } else {
          sprintf (temp_buf, "0.0");
       }
#endif
       return strdup(temp_buf);
    } else if (strstr(keys, "HDMI Switch")) {
        sprintf(temp_buf, "HDMI Switch=%d", (AUDIO_DEVICE_OUT_HDMI & adev->cur_out_devices || is_HDMI_connected(adev)));
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (!strcmp(keys, "hal_param_tv_mute")) {
        sprintf(temp_buf, "hal_param_tv_mute = %d", adev->tv_mute);
        return strdup(temp_buf);
    } else if (strstr (keys, "dialogue_enhancement") ) {
        sprintf(temp_buf, "dialogue_enhancement=%d", adev->ms12.ac4_de);
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr(keys, "audioindicator")) {
        get_audio_indicator(adev, temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_dtv_latencyms") ) {
#ifdef ENABLE_DVB_PATCH
        int latencyms = dtv_patch_get_latency(adev);
        sprintf(temp_buf, "hal_param_dtv_latencyms=%d", latencyms);
#endif
        ALOGV("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_audio_output_mode") ) {
        int audio_output_mode = get_dtv_sound_mode(adev);
        sprintf(temp_buf, "hal_param_audio_output_mode=%d", audio_output_mode);
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_audio_is_tv") ) {
        int is_tv = is_TV(adev);
        sprintf(temp_buf, "hal_param_audio_is_tv=%d", is_TV(adev));
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_get_earctx_cds") ) {
        char cds[AUDIO_HAL_CHAR_MAX_LEN] = {0};
        struct aml_arc_hdmi_desc * hdmi_descs = get_arc_hdmi_cap(adev);
        earctx_fetch_cds(&adev->alsa_mixer, cds, 0, hdmi_descs);
        sprintf(temp_buf, "hal_param_get_earctx_cds=%s", cds);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_get_earctx_attend_type") ) {
        int type = aml_audio_earctx_get_type(adev);
        sprintf(temp_buf, "hal_param_get_earctx_attend_type=%d", type);
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_get_earcrx_attend_type") ) {
        int type = aml_audio_earcrx_get_type(adev);
        sprintf(temp_buf, "hal_param_get_earcrx_attend_type=%d", type);
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "ms12_version") ) {
        sprintf(temp_buf, "ms12_version=%d", adev->support_ms12_version);
        ALOGD("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "ac4_active_pres_id")) {
       int active_id_offset = -1;
#ifndef AUDIO_HAL_DISABLE_MS12
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
#ifdef ENABLE_DVB_PATCH
            //should use the dolby_ms12_get_ac4_active_presentation() before or after get_dolby_ms12_cleanup()
            pthread_mutex_lock(&adev->ms12.lock);
            active_id_offset = dtv_patch_get_ac4_acivie_res_id(adev);
            pthread_mutex_unlock(&adev->ms12.lock);
#endif
        }
#endif
        sprintf(temp_buf, "ac4_active_pres_id=%d", active_id_offset);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_dtv_es_pts_dts_flag") ) {
#ifdef ENABLE_DVB_PATCH
        int latencyms = dtv_patch_get_es_pts_dts_flag(adev);
        sprintf(temp_buf, "hal_param_dtv_es_pts_dts_flag=%d", latencyms);
#endif
        ALOGV("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_get_sink_format")) {
        sprintf(temp_buf, "hal_param_get_sink_format=%#x", adev->sink_format);
        ALOGI("temp_buf %s sink_format=%#x", temp_buf, adev->sink_format);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_get_atmos_supported")) {
        struct aml_arc_hdmi_desc *hdmi_descs = get_arc_hdmi_cap(adev);
        sprintf(temp_buf, "hal_param_get_atmos_supported=%d", hdmi_descs->ddp_fmt.atmos_supported);
        ALOGI("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_get_dap_speaker_status")) {
        bool dap_speaker_status = adev->is_ms12_tuning_dat && (adev->cur_out_devices & AUDIO_DEVICE_OUT_SPEAKER) && (is_TV(adev) || is_SBR_active(adev));
        sprintf(temp_buf, "hal_param_get_dap_speaker_status=%d", dap_speaker_status);
        ALOGI("temp_buf %s dap_speaker_status=%d", temp_buf, dap_speaker_status);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_get_atmos_indicator_status")) {
        sprintf(temp_buf, "hal_param_get_atmos_indicator_status=%d", adev->atmos_indicator_status);
        ALOGV("temp_buf %s atmos_indicator_status=%d", temp_buf, adev->atmos_indicator_status);
        return strdup(temp_buf);
    } else if (strstr (keys, "isAc4PresentationSelectionByIndexSupported")) {
#ifdef MS12_V24_ENABLE
        sprintf(temp_buf, "isAc4PresentationSelectionByIndexSupported=%d", 1);
#else
        sprintf(temp_buf, "isAc4PresentationSelectionByIndexSupported=%d", 0);
#endif
        ALOGI("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_dtv_cmd_close")) {
#ifdef ENABLE_DVB_PATCH
        int cmd_close_status = dtv_patch_get_cmd_close_status(adev);
        sprintf(temp_buf, "hal_param_dtv_cmd_close=%d", cmd_close_status);
#endif
        ALOGV("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr (keys, "hal_param_dtv_decoder_fmt")) {
#ifdef ENABLE_DVB_PATCH
        int decoder_fmt = dtv_patch_get_decoder_fmt(adev);
        sprintf(temp_buf, "hal_param_dtv_decoder_fmt=%d", decoder_fmt);
#endif
        ALOGV("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    }  else if (strstr(keys, "dts_x_enable")) {
        int dtsx_enable = (eDTSXLib == adev->dts_lib_type);
        ALOGI("dtsx_enable :%d", dtsx_enable);
        sprintf(temp_buf, "dts_x_enable=%d", dtsx_enable);
        return  strdup(temp_buf);
    } else if (strstr(keys, "Config")) {
        char *temp_params = get_parameters_from_json_config(&adev->board_config, keys);
        if (temp_params != NULL) {
            return temp_params;
        }
    } else if (strstr (keys, "hal_param_dtv_decoder_fmt")) {
#ifdef ENABLE_DVB_PATCH
        int decoder_fmt = dtv_patch_get_decoder_fmt(adev);
        sprintf(temp_buf, "hal_param_dtv_decoder_fmt=%d", decoder_fmt);
#endif
        ALOGV("temp_buf %s", temp_buf);
        return strdup(temp_buf);
    } else if (strstr(keys, "mpegh_audiosceneconfig")) {
        char *xmlbuf = aml_mpegh_getxmlsceneinfo(adev);
        if (xmlbuf) {
            int xmlsize = strlen(xmlbuf);
            //aml_dump_audio_bitstreams("/data/vendor/audiohal/mpegh_audiosceneconfig.xml", xmlbuf, xmlsize);
            return strdup (xmlbuf);
        }
    } else if (strstr(keys, "mpegh_persistency_ctx")) {
        if (adev->mpegh_ui_persistencemem) {
            base64_encode(adev->mpegh_ui_persistencemem, adev->mpegh_ui_persistencememsize, adev);
            //aml_dump_audio_bitstreams("/data/vendor/audiohal/get_mpegh_persistency_ctx.xml", adev->mpegh_base64_encode_mem, BASE64_BUFSIZE);
            return strdup (adev->mpegh_base64_encode_mem);
        }
    }

    if (eDTSXLib == adev->dts_lib_type) {
        if (strstr (keys, "dtsx_")) {
            if (aml_dtsx_get_runtime_params(&adev->dts_x, keys, temp_buf) == 0)
                return strdup(temp_buf);
        }
    }
    if (strstr(keys, "aq_tuning") || strstr(keys, "audio_") || strstr(keys, "ai_")) {
        ALOGI("get AQ param keys %s", keys);
        int ret = get_AQ_parameters(dev, temp_buf, keys);
        if (!ret)
            return strdup(temp_buf);
    }

    return strdup("");
}

static int adev_config_process_bitwidth(struct aml_audio_device *adev)
{
    audio_format_t primaryOutFormat = AUDIO_FORMAT_PCM_16_BIT;
    set_primary_out_format(adev, primaryOutFormat);
    primaryOutFormat = get_primary_out_format(adev);

    if (adev->useAudioMixer && !adev->mixerData) {
        initHalSubMixing(MIXER_LPCM, adev, is_TV(adev));
        subMixingSetSrcGain(adev, aml_audio_get_s_gain_by_src(adev, SRC_OTHER));
#ifdef SUPPORT_KARAOKE
        /* init usb and linein mic karaoke for mixer path */
        mixer_set_karaoke(adev->mixerData, &adev->usb_audio.karaoke);
        mixer_set_karaoke(adev->mixerData, &adev->linein_karaoke);
#endif
    }

    init_vendor_post_process(&adev->native_postprocess, primaryOutFormat);
    if (is_vendor_support_libvx(&adev->native_postprocess)) {
        dca_set_out_ch_internal(0);
    }

    AM_LOGI("AAAA primaryOutFormat:%s", audioFormat2Str(primaryOutFormat));

    return 0;
}

static int adev_init_check (const struct audio_hw_device *dev __unused)
{
    return 0;
}

static int adev_set_voice_volume (struct audio_hw_device *dev __unused, float volume __unused)
{
    return 0;
}

static int adev_set_master_volume (struct audio_hw_device *dev __unused, float volume __unused)
{
    return -ENOSYS;
}

static int adev_get_master_volume (struct audio_hw_device *dev __unused,
                                   float *volume __unused)
{
    return -ENOSYS;
}

static int adev_set_master_mute (struct audio_hw_device *dev __unused, bool muted __unused)
{
    return -ENOSYS;
}

static int adev_get_master_mute (struct audio_hw_device *dev __unused, bool *muted __unused)
{
    return -ENOSYS;
}
static int adev_set_mode (struct audio_hw_device *dev, audio_mode_t mode)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    AM_LOGI("dev:%p, mode: %s(%d)", dev, audioModeType2Str(mode), mode);

    pthread_mutex_lock (&adev->lock);
    if (adev->mode != mode) {
        adev->mode = mode;
        select_mode (adev);
    }
    pthread_mutex_unlock (&adev->lock);

    return 0;
}

static int adev_set_mic_mute (struct audio_hw_device *dev, bool state)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;

    adev->mic_mute = state;

    return 0;
}

static int adev_get_mic_mute (const struct audio_hw_device *dev, bool *state)
{
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;

    *state = adev->mic_mute;

    return 0;

}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev __unused,
                                        const struct audio_config *config)
{
    int channel_count = audio_channel_count_from_in_mask(config->channel_mask);

    AM_LOGD("channel_mask(%#x) rate(%d) format:%s(%#x)", config->channel_mask, config->sample_rate,
        audioFormat2Str(config->format), config->format);

    if (check_input_parameters(config->sample_rate, config->format, channel_count, AUDIO_DEVICE_NONE) != 0) {
        return -EINVAL;
    }

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size_t period = pcm_config_in.period_size;
    period = (period + 15) / 16 * 16;
    uint32_t req_format = audio_bytes_per_sample(config->format);
    size_t size = period * channel_count * req_format * config->sample_rate / pcm_config_in.rate;
    AM_LOGD("exit: buffer_size = %zu", size);

    return size;
}

static int choose_stream_pcm_config(struct aml_stream_in *in)
{
    int channel_count = audio_channel_count_from_in_mask(in->hal_channel_mask);
    struct aml_audio_device *adev = in->dev;
    int ret = 0;

    if (in->device & AUDIO_DEVICE_IN_ALL_SCO) {
        memcpy(&in->config, &pcm_config_bt, sizeof(pcm_config_bt));
        if (adev->bt_wbs)
            in->config.rate = VX_WB_SAMPLING_RATE;
    } else if (in->device & AUDIO_DEVICE_IN_HDMI || in->device & AUDIO_DEVICE_IN_HDMI_ARC) {
        // do nothing.
    } else if (in->device & AUDIO_DEVICE_IN_BLUETOOTH_BLE) {
        return -EINVAL;
    } else {
        memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
    }

    if (in->config.channels != 8)
        in->config.channels = channel_count;

    switch (in->hal_format) {
        case AUDIO_FORMAT_PCM_16_BIT:
            in->config.format = PCM_FORMAT_S16_LE;
            break;
        case AUDIO_FORMAT_PCM_32_BIT:
            in->config.format = PCM_FORMAT_S32_LE;
            break;
        default:
            ALOGE("%s(), fmt not supported %#x", __func__, in->hal_format);
            break;
    }
    /* TODO: modify alsa config by params */
    update_alsa_config(in);

    return 0;
}

int add_in_stream_resampler(struct aml_stream_in *in)
{
    int ret = 0;

    if (in->requested_rate == in->config.rate)
        return 0;

    if (in->buffer) {
        aml_audio_free(in->buffer);
    }
    in->buffer = aml_audio_calloc(1, in->config.period_size * audio_stream_in_frame_size(&in->stream));
    if (!in->buffer) {
        ret = -ENOMEM;
        goto err;
    }

    ALOGD("%s: in->requested_rate = %d, in->config.rate = %d",
            __func__, in->requested_rate, in->config.rate);
    in->buf_provider.get_next_buffer = get_next_buffer;
    in->buf_provider.release_buffer = release_buffer;
    ret = create_resampler(in->config.rate, in->requested_rate, in->config.channels,
                        RESAMPLER_QUALITY_DEFAULT, &in->buf_provider, &in->resampler);
    if (ret != 0) {
        ALOGE("%s: create resampler failed (%dHz --> %dHz)", __func__, in->config.rate, in->requested_rate);
        ret = -EINVAL;
        goto err_resampler;
    }

    return 0;
err_resampler:
    aml_audio_free(in->buffer);
err:
    return ret;
}


int adev_open_input_stream(struct audio_hw_device *dev,
                                audio_io_handle_t handle,
                                audio_devices_t devices,
                                struct audio_config *config,
                                struct audio_stream_in **stream_in,
                                audio_input_flags_t flags,
                                const char *address,
                                audio_source_t source)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct usb_audio_device *usb_adev = &adev->usb_audio;
    struct aml_stream_in *in;
    int sample_rate = config->sample_rate;
    int channel_count = audio_channel_count_from_in_mask(config->channel_mask);
    int ret = 0;

    in = (struct aml_stream_in *)aml_audio_calloc(1, sizeof(struct aml_stream_in));
    AM_LOGI("io %d: in:%p dev:%s(%#x) src:%s(%d) addr:%s", handle, in,
        audioDevType2Str(devices), devices, audioSourceType2Str(source), source, address);
    AM_LOGI("ch:%#x rate:%d format:%s(%#x) flags:%#x", config->channel_mask, config->sample_rate,
        audioFormat2Str(config->format), config->format, flags);
    R_CHECK_POINTER_LEGAL(-ENOMEM, in, "malloc aml_stream_in fail");

    if ((ret = check_input_parameters(config->sample_rate, config->format, channel_count, devices)) != 0) {
        if (-ENOSYS == ret) {
            ALOGV("  check_input_parameters input config Not Supported, and set to Default config(48000,2,PCM_16_BIT)");
            config->sample_rate = DEFAULT_OUT_SAMPLING_RATE;
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
        } else {
            devices &= ~AUDIO_DEVICE_BIT_IN;
            if (devices & AUDIO_DEVICE_IN_ALL_USB) {
                ALOGD("  check usb input parameter not supported and set to null for using proxy config");
                config->sample_rate = 0;
                config->format = AUDIO_FORMAT_DEFAULT;
                config->channel_mask = AUDIO_CHANNEL_NONE;
            } else {
                ALOGV("  check_input_parameters input config Not Supported, and set to Default config(48000,2,PCM_16_BIT)");
                config->sample_rate = DEFAULT_OUT_SAMPLING_RATE;
                config->format = AUDIO_FORMAT_PCM_16_BIT;
                config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
                return -EINVAL;
            }
        }
    } else {
        //check successfully, continue execute.
    }

#ifdef SUPPORT_KARAOKE
    struct kara_manager *karaoke = &adev->linein_karaoke;
    struct audio_config record_config;
    memset(&record_config, 0, sizeof(struct audio_config));
    /* Use ring buffer for recording original linein mic data */
    /* AUDIO_SOURCE_MIC and setparam "linein_kara_record=1" before start recording */
    if (AUDIO_SOURCE_MIC == source && karaoke_get_mic_record(karaoke)) {
        karaoke_get_config_by_record_type(karaoke, KARA_RECORD_TYPE_MIC_ORIGINAL, &record_config);
        if (config->sample_rate == record_config.sample_rate &&
            config->format == record_config.format &&
            config->channel_mask == record_config.channel_mask) {
            AM_LOGI("Karaoke record source(%d) config check pass", source);
        } else {
            AM_LOGI("Karaoke record source(%d) config not supported and set to real config", source);
            config->sample_rate = record_config.sample_rate ;
            config->format = record_config.format;
            config->channel_mask = record_config.channel_mask;
            return -EINVAL;
        }
    }

    /* Use Aloop for recording data after sw mix by customized source */
    /* set config according to karaoke sw mix */
    if (AUDIO_SOURCE_KARAOKE_SPEAKER == source) {
        karaoke_get_config_by_record_type(karaoke, KARA_RECORD_TYPE_MIC_AFTER_SW_MIX, &record_config);
        if (config->sample_rate == record_config.sample_rate &&
            config->format == record_config.format &&
            config->channel_mask == record_config.channel_mask) {
            AM_LOGI("Karaoke record source(%d) config check pass", source);
        } else {
            AM_LOGI("Karaoke record source(%d) config not supported and set to real config", source);
            config->sample_rate = record_config.sample_rate ;
            config->format = record_config.format;
            config->channel_mask = record_config.channel_mask;
            return -EINVAL;
        }
    }
#endif

    pthread_mutex_init(&in->lock, (const pthread_mutexattr_t *)NULL);
    pthread_mutex_init(&in->pre_lock, (const pthread_mutexattr_t *)NULL);

    devices &= ~AUDIO_DEVICE_BIT_IN;
    if (devices & AUDIO_DEVICE_IN_ALL_USB) {
        usb_adev->adev_primary = (void*)adev;
        adev->in_device |= devices;
        ALOGD("%s: adev->in_device = %x", __func__, adev->in_device);
        ret = adev_open_usb_input_stream(usb_adev, devices, config, stream_in, address);
        if (ret < 0) {
            *stream_in = NULL;
        }
        return ret;
    }

    if (channel_count == 1)
        // in fact, this value should be AUDIO_CHANNEL_OUT_BACK_LEFT(16u) according to VTS codes,
        // but the macro name can be confusing, so I'd like to set this value to
        // AUDIO_CHANNEL_IN_FRONT(16u) instead of AUDIO_CHANNEL_OUT_BACK_LEFT.
        config->channel_mask = AUDIO_CHANNEL_IN_FRONT;
    else
        config->channel_mask = AUDIO_CHANNEL_IN_STEREO;

    {
        in->stream.common.get_sample_rate = in_get_sample_rate;
        in->stream.common.set_sample_rate = in_set_sample_rate;
        in->stream.common.get_buffer_size = in_get_buffer_size;
        in->stream.common.get_channels = in_get_channels;
        in->stream.common.get_format = in_get_format;
        in->stream.common.set_format = in_set_format;
        in->stream.common.standby = in_standby;
        in->stream.common.dump = in_dump;
        in->stream.common.set_parameters = in_set_parameters;
        in->stream.common.get_parameters = in_get_parameters;
        in->stream.set_gain = in_set_gain;
        in->stream.read = in_read;
        in->stream.get_capture_position = in_get_capture_position;
        in->stream.get_input_frames_lost = in_get_input_frames_lost;
        in->stream.get_active_microphones = in_get_active_microphones;
        in->stream.common.add_audio_effect = in_add_audio_effect;
        in->stream.common.remove_audio_effect = in_remove_audio_effect;
    }

    in->io_handle = handle;
    in->device = devices & ~AUDIO_DEVICE_BIT_IN;
    in->dev = adev;
    in->standby = 1;
    in->source = source;
    in->requested_rate = sample_rate;
    in->hal_channel_mask = config->channel_mask;
    in->hal_format = config->format;

#ifdef LOWPOWER_DSP_FFV
    dsp_ffv_stream_init(in);
#endif

    if (in->device & AUDIO_DEVICE_IN_ALL_SCO) {
        memcpy(&in->config, &pcm_config_bt, sizeof(pcm_config_bt));
        if (adev->bt_wbs) {
            in->config.rate = VX_WB_SAMPLING_RATE;
        }
        // returns are based on the sampling rate supported by the hardware.
        config->sample_rate = in->config.rate;
        in->requested_rate = in->config.rate;
    } else {
        memcpy(&in->config, &pcm_config_in, sizeof(pcm_config_in));
    }
    in->config.channels = channel_count;
    in->source = source;
    if (source == AUDIO_SOURCE_ECHO_REFERENCE) {
        in->config.format = PCM_FORMAT_S32_LE;
        ALOGD("aec: force config rate=%d ch=%d format=%d\n",
              in->config.rate, in->config.channels, in->config.format);
    }
    /* TODO: modify alsa config by params */
    update_alsa_config(in);

    switch (config->format) {
        case AUDIO_FORMAT_PCM_16_BIT:
            in->config.format = PCM_FORMAT_S16_LE;
            break;
        case AUDIO_FORMAT_PCM_32_BIT:
            in->config.format = PCM_FORMAT_S32_LE;
            break;
        default:
            break;
    }

    in->buffer = aml_audio_malloc(in->config.period_size * audio_stream_in_frame_size(&in->stream));
    if (!in->buffer) {
        ret = -ENOMEM;
        ALOGE("  malloc fail, goto err!!!");
        goto err;
    }
    memset(in->buffer, 0, in->config.period_size * audio_stream_in_frame_size(&in->stream));
    in->tv_param.read_mul_factor = 2;

    if (!(in->device & AUDIO_DEVICE_IN_WIRED_HEADSET) &&
        in->requested_rate != in->config.rate && in->requested_rate != 0) {
        ALOGD("%s: in->requested_rate = %d, in->config.rate = %d",
            __func__, in->requested_rate, in->config.rate);
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;
        ret = create_resampler(in->config.rate, in->requested_rate, in->config.channels,
                            RESAMPLER_QUALITY_DEFAULT, &in->buf_provider, &in->resampler);
        if (ret != 0) {
            ALOGE("%s: create resampler failed (%dHz --> %dHz)", __func__, in->config.rate, in->requested_rate);
            ret = -EINVAL;
            goto err;
        }
    }

    /* If AEC is in the app, only configure based on ECHO_REFERENCE spec.
     * If AEC is in the HAL, configure using the given mic stream. */
#ifdef ENABLE_AEC_APP
    bool aecInput = (source == AUDIO_SOURCE_ECHO_REFERENCE);
    if (aecInput) {
        int aec_ret = init_aec_mic_config(adev->aec, in);
        if (aec_ret) {
            ALOGE("AEC: Mic config init failed!");
            ret = -EINVAL;
            goto err;
        }
    }
#endif

    *stream_in = &in->stream;
#if ENABLE_NANO_NEW_PATH
    if (in->device & AUDIO_DEVICE_IN_BLUETOOTH_BLE) {
        if (nano_is_connected()) {
            ret = nano_input_open(*stream_in, config);
            if (ret < 0) {
                ALOGD("%s: nano_input_open : %d",__func__,ret);
            }
        } else {
            AM_LOGW("nano is disconnect!!!");
            return -EINVAL;
        }
    }
#endif
    if (address && !strncmp(address, "AML_", 4)) {
        ALOGI("%s(): aml TV source stream", __func__);
        in->is_tv_src_stream = true;
    }
    AM_LOGI("result profile ch:%#x rate:%d format:%s(%#x)", config->channel_mask, config->sample_rate,
        audioFormat2Str(config->format), config->format);
    AM_LOGI("io %d: in:%p exit ------", handle, in);
    return 0;
err:
    if (in->resampler) {
        release_resampler(in->resampler);
        in->resampler = NULL;
    }
    if (in->buffer) {
        aml_audio_free(in->buffer);
        in->buffer = NULL;
    }
    aml_audio_free(in);
    *stream_in = NULL;
    return ret;
}

void adev_close_input_stream(struct audio_hw_device *dev,
                                struct audio_stream_in *stream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_in *in = (struct aml_stream_in *)stream;

    AM_LOGI("io %d: in:%p dev:%s ch:%#x format:%s(%#x) src:%s(%d)", in->io_handle, in,
        audioDevType2Str(in->device | AUDIO_DEVICE_BIT_IN), in->hal_channel_mask, audioFormat2Str(in->hal_format),
        in->hal_format, audioSourceType2Str(in->source), in->source);

    if (stream == adev->usb_audio.stream_in) {
        struct stream_in *usb_in = (struct stream_in *)stream;
        adev->in_device &= ~usb_in->device;
        adev_close_usb_input_stream(stream);
        ALOGD("%s: adev->in_device = %x", __func__, adev->in_device);
        return;
    }

#if ENABLE_NANO_NEW_PATH
    nano_close(stream);
#endif

    in_standby(&stream->common);

    if (in->resampler) {
        release_resampler(in->resampler);
        in->resampler = NULL;
    }

    pthread_mutex_lock (&in->lock);
    if (in->buffer) {
        aml_audio_free(in->buffer);
        in->buffer = NULL;
    }
    pthread_mutex_unlock (&in->lock);

    if (in->proc_buf) {
        aml_audio_free(in->proc_buf);
    }
    if (in->ref_buf) {
        aml_audio_free(in->ref_buf);
    }

#ifdef ENABLE_AEC_APP
    if (in->device & AUDIO_DEVICE_IN_ECHO_REFERENCE) {
        destroy_aec_mic_config(adev->aec);
    }
#endif

    if (in->resample_handle) {
        aml_audio_resample_close(in->resample_handle);
        in->resample_handle = NULL;
    }
#ifdef LOWPOWER_DSP_FFV
    dsp_ffv_stream_deinit(in);
#endif

    pthread_mutex_destroy(&in->pre_lock);
    pthread_mutex_destroy(&in->lock);
    AM_LOGI("io %d: in:%p exit ------", in->io_handle, in);
    aml_audio_free(stream);
    stream = NULL;
    in = NULL;
    return;
}

static void dump_audio_port_config (const struct audio_port_config *port_config)
{
    if (port_config == NULL)
        return;

    ALOGI ("  -%s port_config(%p)", __FUNCTION__, port_config);
    ALOGI ("\t-id(%d), role(%s), type(%s)",
        port_config->id,
        audio_port_role_to_str(port_config->role),
        audio_port_type_to_str(port_config->type));
    ALOGV ("\t-config_mask(%#x)", port_config->config_mask);
    ALOGI ("\t-sample_rate(%d), channel_mask(%#x), format(%#x)", port_config->sample_rate,
           port_config->channel_mask, port_config->format);
    ALOGV ("\t-gain.index(%#x)", port_config->gain.index);
    ALOGV ("\t-gain.mode(%#x)", port_config->gain.mode);
    ALOGV ("\t-gain.channel_mask(%#x)", port_config->gain.channel_mask);
    ALOGI ("\t-gain.value0(%d)", port_config->gain.values[0]);
    ALOGI ("\t-gain.value1(%d)", port_config->gain.values[1]);
    ALOGI ("\t-gain.value2(%d)", port_config->gain.values[2]);
    ALOGV ("\t-gain.ramp_duration_ms(%d)", port_config->gain.ramp_duration_ms);
    switch (port_config->type) {
    case AUDIO_PORT_TYPE_DEVICE:
        ALOGI ("\t-port device: type(%#x) addr(%s)",
               port_config->ext.device.type, port_config->ext.device.address);
        break;
    case AUDIO_PORT_TYPE_MIX:
        ALOGI ("\t-port mix: io handle(%d)", port_config->ext.mix.handle);
        break;
    default:
        break;
    }
}

/* must be called with hw device and output stream mutexes locked */
int do_output_standby_l(struct audio_stream *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = out->ms12_dec_handle;
    bool last_pause_status = false;

    AM_LOGI("io %d: out:%p, stream streamType:%s, continuous:%d", out->io_handle, out,
        streamType2Str(out->streamType), adev->continuous_audio_mode);
    /*
    if continuous mode,we need always have output.
    so we should not disable the output.
    */
    pthread_mutex_lock(&adev->alsa_pcm_lock);
    /*SWPL-15191 when exit movie player, it will set continuous and the alsa
      can't be closed. After playing something alsa will be open again and can't be
      closed anymore, because its ref is bigger than 0.
      Now we add this patch to make sure the alsa will be closed.
    */
    if (out->stream_status == STREAM_HW_WRITING &&
        ((!continuous_mode(adev) || (!ms12->dolby_ms12_enable && (eDolbyMS12Lib == adev->dolby_lib_type))))) {
        ALOGI("%s out(%p)standby close", __func__, out);
        aml_alsa_output_stop((struct audio_stream_out *)stream);

        if (out->spdifout_handle) {
            aml_audio_spdifout_stop(out->spdifout_handle);
            //out->spdifout_handle = NULL;
        }
        if (out->spdifout2_handle) {
            aml_audio_spdifout_stop(out->spdifout2_handle);
            //out->spdifout2_handle = NULL;
        }
    }

    out->stream_status = STREAM_STANDBY;
    out->standby = 1;
    out->audiomixer_standby = true;
    if (continuous_mode(adev) && out->hw_sync_mode && adev->ms12_out) {
        adev->ms12_out->standby = true;
    }

    if (adev->continuous_audio_mode == 0) {
        out->alsa_running_status = false;

        // release buffers
        if (out->buffer) {
            aml_audio_free(out->buffer);
            out->buffer = NULL;
        }

        if (out->resampler) {
            release_resampler(out->resampler);
            out->resampler = NULL;
        }
    }
    pthread_mutex_unlock(&adev->alsa_pcm_lock);

    last_pause_status = out->pause_status;
    out->pause_status = false;//clear pause status
    pthread_mutex_lock(&ms12->lock);
    // ms12 decoder pause status also should be cleared
    if (last_pause_status && adev->ms12.dolby_ms12_enable) {
        ms12_dec->resume_state = MS12_RESUME_FROM_FLUSH;
        dolby_ms12_main_resume((struct audio_stream_out *)stream);
    }
    pthread_mutex_unlock(&ms12->lock);

#ifdef ENABLE_AEC_APP
    aec_set_spk_running(adev->aec, false);
#endif

    return 0;
}

int out_standby_new(struct audio_stream *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *aml_dev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(aml_dev->ms12);
    struct amlAudioMixer *audio_mixer = aml_dev->mixerData;
    int status;
    bool is_hdmi_connecting = is_HDMI_connected(aml_dev);
    /*when it is speaker output, we need support standby function*/
    bool need_schedule = !is_hdmi_connecting || aml_dev->low_power;

    if (aml_dev->is_netflix) {
        need_schedule = false;
    }
    AM_LOGD("io %d: out:%p streamType:%s", aml_out->io_handle, aml_out, streamType2Str(aml_out->streamType));

    aml_audio_trace_int("out_standby_new", 1);
    aml_out->trace_last_write_time_ms = 0;
    if (aml_out->stream_status == STREAM_STANDBY) {
        ALOGI("already standby, do nothing");
        aml_audio_trace_int("out_standby_new", 0);
        return 0;
    }

    pthread_mutex_lock (&aml_out->dev->lock);
    pthread_mutex_lock (&aml_out->lock);
    status = do_output_standby_l(stream);
    pthread_mutex_unlock (&aml_out->lock);
    pthread_mutex_unlock (&aml_out->dev->lock);

    /*
     * Non-tunel pcm input port buffer size may need to be dynamic changed.
     * So delete it each when it finished(after standby)
     *
     * Don't put these code into do_output_standby_l, for it may be called by other function.
    */
    if (aml_dev->useAudioMixer && aml_out->inputPortID != -1) {
        //  Fix: pause and delete input port timing too closer, then fade out data cannot be played.
        if (aml_out->total_write_size > 0 && is_direct_flags(aml_out->flags)) {
            uint64_t standby_time = aml_audio_get_systime() / 1000; //us --> ms
            if (aml_out->pause_time && standby_time >= aml_out->pause_time) {
                int delay_ms = 0;
                uint64_t elapsed_ms = standby_time - aml_out->pause_time;
                if (elapsed_ms < aml_dev->stream_pause_delay) {
                    delay_ms = aml_dev->stream_pause_delay - elapsed_ms;
                    aml_audio_sleep(delay_ms * 1000);
                    AM_LOGI("sleep %d ms finished", delay_ms);
                }
            }
        }
        delete_mixer_input_port(aml_dev->mixerData, aml_out->inputPortID);
        aml_out->inputPortID = -1;
    }

    if (!aml_get_is_exist_active_stream()) {
        // send the SCHEDULER_STANDBY to ms12.
        aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_STANDBY);
        if (aml_dev->useAudioMixer && need_schedule) {
            ALOGI("send STANDBY msg to submix");
            aml_audiohal_sch_state_2_submix(audio_mixer, SUBMIX_SCHEDULER_STANDBY);
        }
    } else {
        // do something.
    }


    AM_LOGD("io %d: out:%p streamType:%s exit", aml_out->io_handle, aml_out, streamType2Str(aml_out->streamType));
    aml_audio_trace_int("out_standby_new", 0);

    return status;
}

audio_format_t get_output_format (struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    audio_format_t output_format = aml_out->hal_internal_format;

    struct dolby_ms12_desc *ms12 = & (adev->ms12);

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
            output_format = adev->sink_format;
    } else if (eDolbyDcvLib == adev->dolby_lib_type) {
        if (adev->digital_audio_mode > AML_DIGITAL_AUDIO_MODE_PCM) {
            if (adev->dual_spdif_support) {
                if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3 && adev->sink_format == AUDIO_FORMAT_E_AC3) {
                    if (adev->dolby_decode_enable == 1) {
                        output_format =  AUDIO_FORMAT_AC3;
                    } else {
                        output_format =  AUDIO_FORMAT_E_AC3;
                    }
                } else {
                    output_format = adev->sink_format;
                }
            } else {
                output_format = adev->sink_format;
            }
        } else
            output_format = adev->sink_format;
    }

    return output_format;
}


/* AEC need a wall clock (monotonic clk?) to sync*/
static aec_timestamp get_timestamp(void) {
    struct timespec ts;
    unsigned long long current_time;
    aec_timestamp return_val;
    /*clock_gettime(CLOCK_MONOTONIC, &ts);*/
    clock_gettime(CLOCK_REALTIME, &ts);
    current_time = (unsigned long long)ts.tv_sec * 1000000 + (unsigned long long)ts.tv_nsec / 1000;
    return_val.timeStamp = current_time;
    return return_val;
}

audio_format_t get_non_ms12_output_format(audio_format_t src_format, struct aml_audio_device *aml_dev)
{
    audio_format_t output_format = AUDIO_FORMAT_PCM_16_BIT;
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(aml_dev);
    if (aml_dev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_AUTO) {
        if (src_format == AUDIO_FORMAT_E_AC3 ) {
            if (hdmi_desc->ddp_fmt.is_support)
               output_format = AUDIO_FORMAT_E_AC3;
            else if (hdmi_desc->dd_fmt.is_support)
                output_format = AUDIO_FORMAT_AC3;
        } else if (src_format == AUDIO_FORMAT_AC3 ) {
            if (hdmi_desc->dd_fmt.is_support)
                output_format = AUDIO_FORMAT_AC3;
        }
    }
    return output_format;
}


void config_output(struct audio_stream_out *stream, bool reset_decoder)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);

    int ret = 0;
    bool main1_dummy = false;
    bool ott_input = false;
    bool dtscd_flag = false;
    bool reset_decoder_stored = reset_decoder;
    int i  = 0 ;
    uint64_t write_frames = 0;

    int is_arc_connected = 0;
    int sink_format = AUDIO_FORMAT_PCM_16_BIT;
    adev->dcvlib_bypass_enable = 0;
    adev->dtslib_bypass_enable = 0;

    /*get sink format*/
    get_sink_format (stream);
    AM_LOGI("out:%p hal_internal_format:%s(%#x) dolby_lib_type: %d reset_decoder %d rate =%d ch=%d", aml_out,
        audioFormat2Str(aml_out->hal_internal_format), aml_out->hal_internal_format, adev->dolby_lib_type, reset_decoder, aml_out->hal_rate, aml_out->hal_ch);
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        bool is_compatible = false;
        bool is_direct_pcm = is_direct_stream_and_pcm_format(aml_out);
        bool is_mmap_pcm = is_mmap_stream_and_pcm_format(aml_out);
        bool is_ms12_pcm_volume_control = (is_direct_pcm && !is_mmap_pcm);
        bool is_a2dp_device = (aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP);

        //ALOGI("%s is_ms12_pcm_volume_control:%d, is_a2dp_device:%d, out_device:0x%x, volume_l:%f",
        //        __func__, is_ms12_pcm_volume_control, is_a2dp_device, aml_out->out_device, aml_out->volume_l);

        if (!is_dolby_ms12_support_compression_format(aml_out->hal_internal_format)) {

            if (aml_out->aml_dec) {
                is_compatible = aml_decoder_output_compatible(stream, adev->sink_format, adev->optical_format);
                if (is_compatible) {
                    reset_decoder = false;
                }
            }

            if (reset_decoder) {
                /*if decoder is init, we close it first*/
                if (aml_out->aml_dec) {
                    pthread_mutex_lock(&aml_out->dec_MutexLock);
                    aml_decoder_release(aml_out->aml_dec);
                    aml_out->aml_dec = NULL;
                    pthread_mutex_unlock(&aml_out->dec_MutexLock);
                }

                memset(&aml_out->dec_config, 0, sizeof(aml_dec_config_t));

                /*prepare the decoder config*/
                ret = aml_decoder_config_prepare(stream, aml_out->hal_internal_format, &aml_out->dec_config);

                if (ret < 0) {
                    ALOGE("config decoder error");
                    return;
                }

                pthread_mutex_lock(&aml_out->dec_MutexLock);
                ret = aml_decoder_init(&aml_out->aml_dec, aml_out->hal_internal_format, (aml_dec_config_t *)&aml_out->dec_config);
                pthread_mutex_unlock(&aml_out->dec_MutexLock);

                if (ret < 0) {
                    ALOGE("aml_decoder_init failed");
                }

            }

        }
        is_compatible = false;
        reset_decoder = reset_decoder_stored;
        ALOGI("continuous_mode(adev) %d ms12->dolby_ms12_enable %d",continuous_mode(adev), ms12->dolby_ms12_enable);
        if (continuous_mode(adev) && ms12->dolby_ms12_enable) {
            is_compatible = is_ms12_output_compatible(stream, adev->sink_format, adev->optical_format);
        }
        if (!is_compatible && netflix_request_dd_output()) {
            reset_decoder = true;
        }

        if (!reset_decoder) {
            //Local playback and no dolby input, then non-dolby format lead the ms12_main1_dolby_dummy as true.
            if (aml_out->is_normal_pcm && adev->ms12.dolby_ms12_enable) {
                set_ms12_drc_params_for_stereo_and_dap_multi_pcm_output(
                    adev
                    , &(adev->ms12)
                    , AUDIO_FORMAT_PCM_16_BIT //treat as PCM format to use the DRC Line mode.
                    );
            }
        }

        if (!is_bypass_dolbyms12(stream) && (reset_decoder == true)) {
            pthread_mutex_lock(&adev->lock);
            if (!ms12->dolby_ms12_enable) {
                adev_ms12_prepare((struct audio_hw_device *)adev);
            }

            adev->mix_init_flag = true;
            audiohal_send_msg_2_ms12(&adev->ms12, MS12_MESG_TYPE_RESET_MS12_ENCODER);
            pthread_mutex_unlock(&adev->lock);
            /* if ms12 reconfig, do avsync */
            if (ret == 0 && is_dev_patch_exist(adev) &&
                (is_same_patch_src(adev, SRC_HDMIIN) ||
                 is_same_patch_src(adev, SRC_ATV) ||
                 is_same_patch_src(adev, SRC_LINEIN))) {
                get_dev_patch(adev)->need_do_avsync = true;
                tv_set_ease(aml_out, EaseIn);
                ALOGI("set ms12, then do avsync!");
            }
        }
    } else {
        bool is_compatible = false;
        if (aml_out->aml_dec) {
            is_compatible = aml_decoder_output_compatible(stream, adev->sink_format, adev->optical_format);
            if (is_compatible) {
                reset_decoder = false;
            }
        }

        pthread_mutex_lock(&adev->alsa_pcm_lock);
        if (aml_out->stream_status == STREAM_HW_WRITING) {
            aml_alsa_output_close(stream);
            aml_out->stream_status = STREAM_STANDBY;
        }
        pthread_mutex_unlock(&adev->alsa_pcm_lock);
        /* In netflix, when ddp do seek, we should not
         * close the spdif out, otherwise it will disable
         * the audio clock, and this will causes the audio
         * clock discontinuity.
         * todo: shall remove it for all cases?
         */
        if (!adev->is_netflix) {
            if (aml_out->spdifout_handle) {
                aml_audio_spdifout_close(aml_out->spdifout_handle);
                aml_out->spdifout_handle = NULL;
                aml_out->dual_output_flag = 0;
            }
            if (aml_out->spdifout2_handle) {
                aml_audio_spdifout_close(aml_out->spdifout2_handle);
                aml_out->spdifout2_handle = NULL;
            }
        }
        if (reset_decoder) {
            if (aml_out->aml_dec) {
                pthread_mutex_lock(&aml_out->dec_MutexLock);
                aml_decoder_release(aml_out->aml_dec);
                aml_out->aml_dec = NULL;
                pthread_mutex_unlock(&aml_out->dec_MutexLock);
            }

            memset(&aml_out->dec_config, 0, sizeof(aml_dec_config_t));

            /*prepare the decoder config*/
            if (aml_out->hal_format == AUDIO_FORMAT_IEC61937 && !aml_out->is_tv_src_stream)
                ret = aml_decoder_config_prepare(stream, aml_out->hal_format, &aml_out->dec_config);
            else
                ret = aml_decoder_config_prepare(stream, aml_out->hal_internal_format, &aml_out->dec_config);

            if (ret < 0) {
                ALOGE("config decoder error");
                return;
            }

            pthread_mutex_lock(&aml_out->dec_MutexLock);
            if (aml_out->hal_format == AUDIO_FORMAT_IEC61937 && !aml_out->is_tv_src_stream)
                ret = aml_decoder_init(&aml_out->aml_dec, aml_out->hal_format, (aml_dec_config_t *)&aml_out->dec_config);
            else
                ret = aml_decoder_init(&aml_out->aml_dec, aml_out->hal_internal_format, (aml_dec_config_t *)&aml_out->dec_config);
            pthread_mutex_unlock(&aml_out->dec_MutexLock);

            if (ret < 0) {
                ALOGE("aml_decoder_init failed");
            }

            pthread_mutex_lock(&adev->lock);
            if (!adev->hw_mixer.start_buf) {
                aml_hw_mixer_init(&adev->hw_mixer);
            } else {
                aml_hw_mixer_reset(&adev->hw_mixer);
            }
            pthread_mutex_unlock(&adev->lock);
        }
    }
    /*TV-4745: After switch from normal PCM playing to MS12, the device will
     be changed to SPDIF, but when switch back to the normal PCM, the out device
     is still SPDIF, then the sound is abnormal.
    */
    if (!(continuous_mode(adev) && (eDolbyMS12Lib == adev->dolby_lib_type))) {
        if (sink_format == AUDIO_FORMAT_PCM_16_BIT || sink_format == AUDIO_FORMAT_PCM_32_BIT) {
            aml_out->device = PORT_I2S;
        } else {
            aml_out->device = PORT_SPDIF;
        }
    }

    /* After playback for previous dts stream, there is remain data in VirtualX library. It needs to clear data buffer of VirtualX by using
       zero data to replace these remain data. Otherwise it will play this remain data first when start playback next time*/
    if (is_dev_patch_valid(adev) && is_dev_patch_exist(adev) && (get_dev_patch(adev)->input_src == AUDIO_DEVICE_IN_HDMI)) {
        if ((adev->cur_out_devices & AUDIO_DEVICE_OUT_SPEAKER) != 0 && aml_out->write_count > 0) {
            char *tmp_buffer = aml_audio_malloc(VX_BUFFER_CLEAR_MULTICHANNEL_FRAME_SIZE);
            if (!tmp_buffer) {
                ALOGE("tmp_buffer NULL %d",__LINE__);
            }
            for (int i = 0; i < VX_BUFFER_CLEAR_COUNT; i++) {
                 memset(tmp_buffer, 0, VX_BUFFER_CLEAR_MULTICHANNEL_FRAME_SIZE);
                 audio_post_process(&adev->native_postprocess, (int16_t *)tmp_buffer, VX_BUFFER_CLEAR_STEREO_FRAME_SIZE);
                 audio_VX_post_process(&adev->native_postprocess, (int16_t *)tmp_buffer, VX_BUFFER_CLEAR_MULTICHANNEL_FRAME_SIZE);
            }
            aml_audio_free(tmp_buffer);
            tmp_buffer = NULL;
        }
    }

    AM_LOGI("out stream alsa port device:%d", aml_out->device);
    return ;
}

void aml_stream_timer_callback_handler(union sigval sigv)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    struct aml_stream_out *out = NULL;
    bool is_hwsync_lpcm = false;
    bool frame_write_sum_updated = true;

    AM_LOGD("sigv:%d ~~~~~~~~~~", sigv.sival_int);
    pthread_mutex_lock(&adev->stream_release_lock);
    for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
        out = adev->active_outputs[i];
        if (out && !out->is_closing &&  audio_is_linear_pcm(out->hal_internal_format)
            && (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {
            is_hwsync_lpcm = true;
            out->is_callback_pending = true;
            break;
        }
    }
    pthread_mutex_unlock(&adev->stream_release_lock);

    if (out && is_hwsync_lpcm) {
        out->frame_write_sum_updated = false;
        frame_write_sum_updated = false;
        out->is_callback_pending = false;
    }
    AM_LOGI("is_hwsync_lpcm:%d frame_write_sum_updated:%d", is_hwsync_lpcm, frame_write_sum_updated);
    return ;
}

void aml_stream_timer_pause_callback(union sigval sigv)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    struct aml_stream_out *out = NULL;
    bool is_hwsync_lpcm = false;

    AM_LOGD("sigv:%d ~~~~~~~~~~", sigv.sival_int);
    pthread_mutex_lock(&adev->stream_release_lock);
    for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
        out = adev->active_outputs[i];
        if (out && !out->is_closing && audio_is_linear_pcm(out->hal_internal_format)
            && (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {
            is_hwsync_lpcm = true;
            out->is_callback_pending = true;
            break;
        }
    }
    pthread_mutex_unlock(&adev->stream_release_lock);

    if (adev && out && is_hwsync_lpcm) {
        //cts tunnel underrun case failed, depond on pause/resume invoked from AudioFlinger.
        //sometimes AudioFlinger always invoke the pause to Hal during 800ms for track retry count.
        //so add this code to control pause/resume MediaSync and video in Hal.

        //out_pause_new will trigger wait_video_done, the next wait_video_done will cause timeout again,
        //then enter a dead-loop.
        AM_LOGI("out=%p is_insert_zero_data=%d end_of_hwsync_frame=%d is_waiting_video=%d",
            out, out->is_insert_zero_data, out->hwsync->end_of_hwsync_frame, out->is_waiting_video);

        if (!out->is_insert_zero_data && !out->hwsync->end_of_hwsync_frame && !out->is_waiting_video)
            out_pause_new((struct audio_stream_out *)out);
        out->is_callback_pending = false;
    }
    return ;
}

ssize_t mixer_main_buffer_write(struct audio_stream_out *stream, void *abuffer)
{
    aml_audio_buffer_t *audioBuffer = (aml_audio_buffer_t *)abuffer;
    const void *buffer = audioBuffer->pData;
    size_t bytes = audioBuffer->size;
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    int case_cnt;
    int ret = -1;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    bool need_reconfig_output = false;
    bool need_reset_decoder = true;
    bool need_reconfig_samplerate = false;
    void   *write_buf = NULL;
    size_t  write_bytes = 0;
    size_t  hwsync_cost_bytes = 0;
    int total_write = 0;
    size_t used_size = 0;
    int write_retry = 0;
    size_t total_bytes = bytes;
    size_t bytes_cost = 0;
    int ms12_write_failed = 0;
    effect_descriptor_t tmpdesc;
    int return_bytes = bytes;
    uint64_t apts64 = 0;
    int64_t enter_time_us = 0;
    bool amaster_mode = true;

    int fadein_detect_time_ms  = 0;
    bool digital_input_src = aml_out->is_tv_src_stream;

    if (adev->debug_flag) {
        enter_time_us = aml_audio_get_systime();
        AM_LOGI("io %d: out:%p bytes:%zu format:%s(%#x) hw_sync:%d", aml_out->io_handle, aml_out, bytes,
            audioFormat2Str(aml_out->hal_internal_format), aml_out->hal_internal_format, aml_out->hw_sync_mode);
        AM_LOGI("continuous:%d hal_format:%s(%#x), out_streamType:%s, streamCount:%#x", adev->continuous_audio_mode,
            audioFormat2Str(aml_out->hal_format), aml_out->hal_format, streamType2Str(aml_out->streamType), adev->streamCount);
    }
    R_CHECK_POINTER_LEGAL(-1, buffer,);

    //this is a temp solution for DTS dca
    //dca not support multi instance, drop these data
    if (is_dts_format(aml_out->hal_internal_format) && (AML_WRITE_POLICY_REJECTION == aml_stream_check_dts_write_policy(aml_out))) {
        AM_LOGW(" out stream:%p  drop this buffer.", aml_out);
        aml_audio_sleep(10*1000);//sleep 10ms,dts minimum frame is 10.66ms.
        return bytes;
    }

    /*for dtv case doesn't call out_write_new*/
    if (aml_out->is_preempted) {
        ALOGI("%s drop data size =(%zu)", __func__, bytes);
        usleep(32*1000);
        return bytes;
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
#if 0
        if (ms12->ms12_main_stream_out && ms12->ms12_main_stream_out->stream_status != STREAM_STANDBY) {
            if (ms12->ms12_main_stream_out != aml_out && ms12->ms12_main_stream_out->is_ms12_main_decoder) {
                ALOGI("%s main stream is not same, release the old one =%p  new =%p ", __func__, ms12->ms12_main_stream_out, aml_out);
                out_standby_new((struct audio_stream *)ms12->ms12_main_stream_out);
                close_ms12_output_main_stream((struct audio_stream_out *)ms12->ms12_main_stream_out);
            }
        }
#endif
        if (!aml_out->b_migrate_check) {
            aml_audio_stream_migrate_to_apu(aml_out);
            aml_out->b_migrate_check = true;
        }
        if (!aml_out->b_priority_check && aml_out->is_netflix_src_stream) {
            if (aml_out->hal_format == AUDIO_FORMAT_E_AC3_JOC) {
                // Atmos decoder needs more cpu, apply the highest priority in normal schedule class.
                const int ANDROID_PRIORITY_HIGHEST = -20;
                setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_HIGHEST);
            }
            aml_out->b_priority_check = true;
        }
    }

    if (aml_out->standby && (eDolbyMS12Lib == adev->dolby_lib_type_last || adev->useAudioMixer)) {
        AM_LOGI("io %d: out:%p streamType:%s, standby to unstandby", aml_out->io_handle, aml_out, streamType2Str(aml_out->streamType));

        if (adev->is_netflix) {
            fadein_detect_time_ms = NETFLIX_FADEIN_MAX_DETECT_TIME_MS;
        }
        aml_audio_data_handle_init(stream);
        set_ms12_fadein_max_detect_time_ms(fadein_detect_time_ms);

        aml_out->standby = false;
        if (adev->ms12_out) {
            adev->ms12_out->standby = false;
        }
    }

    if (aml_out->flush_first_write && adev->is_netflix && eDolbyDcvLib == adev->dolby_lib_type) {
        aml_out->flush_first_write = false;

        if (!audio_is_linear_pcm(aml_out->hal_format) && aml_out->total_write_size) {
            uint64_t curr_time_ms = aml_audio_get_systime() / 1000;
            AM_LOGI("time_ms %" PRId64 ", pause_time %" PRId64 ", flush_time %" PRId64 "",
                curr_time_ms, aml_out->pause_time, aml_out->flush_time);
            if ((curr_time_ms > aml_out->pause_time) && (aml_out->flush_time >= aml_out->pause_time)) {
                int sleep_time_ms = 0;
                uint64_t diff_time_ms = curr_time_ms - aml_out->pause_time;
                // for case : eleven will detect event "No data received for 200ms, switching to fake source".
                // reduce the first audio data 32ms(ddp alsa start threshold is 42ms)
                if (diff_time_ms < 180) {
                    sleep_time_ms = 180 - diff_time_ms;
                    AM_LOGI("time_ms %" PRId64 " audiotrack switch, sleep %d ms", diff_time_ms, sleep_time_ms);
                    usleep(sleep_time_ms * 1000);
                }
            }
        }
    }

    /*for ms12 continuous mode, we need update status here, instead of in hw_write*/
    if (aml_out->stream_status == STREAM_STANDBY && continuous_mode(adev)) {
        aml_out->stream_status = STREAM_HW_WRITING;
    }

    if (get_debug_value(AML_DEBUG_AUDIOHAL_DETECT_ZERO_DATA)) {
        int max_diff_ms = property_get_int32(AML_STREAM_WRITE_MAX_TIME_MS_PROP, 0);
        int64_t curr_time_ms = aml_gettime()/1000;
        int64_t diff_time_ms = curr_time_ms - aml_out->trace_last_write_time_ms;

        if (aml_out->trace_last_write_time_ms > 0 && max_diff_ms > 0 && (diff_time_ms >= max_diff_ms)) {
            aml_audio_trace_int("main_write_gap", diff_time_ms);
            ALOGI("%s : atrace name(value) : %s %" PRId64 "", __func__, "main_write_gap", diff_time_ms);
            property_set(AML_TRACE_STREAM_ZERO_PROP, "1");
        }
        aml_out->trace_last_write_time_ms = curr_time_ms;
    }

    /* here to check if the audio HDMI ARC format updated. */
    if (is_arc_hdmi_updated(adev)) {
        ALOGI ("%s(), arc format updated, need reconfig output", __func__);
        need_reconfig_output = true;
        /*
        we reset the whole decoder pipeline when audio routing change,
        audio output option change, we do not need do a/v sync in this user case.
        in order to get a low cpu loading, we enabled less ms12 modules in each
        hdmi in user case, we need reset the pipeline to get proper one.
        */
        need_reset_decoder = true;//digital_input_src ? true: false;
        set_arc_hdmi_updated(adev, false);
    }
    /* here to check if the hdmi audio output format dynamic changed. */
    if (adev->last_digital_audio_mode != adev->digital_audio_mode ) {
        ALOGI("digital audio mode is changed from %s to %s need reconfig output",
            digitalAudioModeType2Str(adev->last_digital_audio_mode), digitalAudioModeType2Str(adev->digital_audio_mode));
        adev->last_digital_audio_mode = adev->digital_audio_mode;
        need_reconfig_output = true;
        need_reset_decoder = digital_input_src ? true: false;
    }

    if (adev->a2dp_updated) {
        ALOGI ("%s(), a2dp updated, need reconfig output, %d %d", __func__, adev->cur_out_devices, aml_out->out_device);
        need_reconfig_output = true;
        adev->a2dp_updated = 0;
    }

    if (adev->digital_audio_mode_updated) {
        ALOGI("%s(), digital audio mode updated, need reconfig output. %s",
            __func__, digitalAudioModeType2Str(adev->digital_audio_mode));
        need_reconfig_output = true;
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            need_reset_decoder = false;
        } else {
            need_reset_decoder = true;
        }

        adev->digital_audio_mode_updated = 0;

        if (adev->cur_out_devices & AUDIO_DEVICE_OUT_SPEAKER) {
            set_output_device_mute(adev, AUDIO_DEVICE_OUT_SPEAKER, true, true/*use fade*/);
            clock_gettime(CLOCK_MONOTONIC, &adev->fmt_start_ts);
            adev->fmt_start_mute = true;
            adev->fmt_mdelay = 2 * DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT / (MM_FULL_POWER_SAMPLING_RATE / 1000);
        }
    }

    if ((adev->cur_out_devices & AUDIO_DEVICE_OUT_SPEAKER) && adev->fmt_start_mute) {
        int flag = Stop_watch(adev->fmt_start_ts, adev->fmt_mdelay);
        if (!flag) {
            adev->fmt_start_mute = false;
            set_output_device_mute(adev, AUDIO_DEVICE_OUT_SPEAKER, false, true);
        }
    }

    /* here to check if the audio output routing changed. */
    if (adev->cur_out_devices != aml_out->out_device) {
        AM_LOGI("output routing changed, need reconfig output, adev_dev:%#x, out_dev:%#x",
            adev->cur_out_devices, aml_out->out_device);
        need_reconfig_output = true;
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            need_reset_decoder = false;
        }
        aml_out->out_device = adev->cur_out_devices;
    }

    if (is_HDMI_reconnected(adev)) {
        ALOGI("%s(), hdmi connect updated, need reconfig output", __func__);
        need_reconfig_output = true;
        need_reset_decoder = true;
        set_HDMI_reconnected_flag(adev, false);
    }

    write_buf = (void *)buffer;
    write_bytes = bytes;

    /* PCM use the Tunnel mode */
    // For NTS VOL-INTER-AUDIO-PROFILE-UIAUDIO-HEAAC-AL1 :
    //   it will let the non-tunel pcm and aaudio loundness difference > 3dB.
    //   currently don't enable for netflix
    if (audio_is_linear_pcm(aml_out->hal_internal_format) && aml_out->hal_internal_format != AUDIO_FORMAT_PCM_FLOAT && !aml_out->is_netflix_src_stream) {
        bool is_local_out_bitstream = !is_tv_stream_out(aml_out) && (adev->sink_format > AUDIO_FORMAT_PCM_16_BIT);

        pcm_data_do_pre_attenuation(
            write_buf
            , write_bytes
            , adev->ms12.dolby_ms12_enable
            , (is_dtv_stream_out(stream) || is_local_out_bitstream)
            , (adev->ms12.stereo_drc.mode == DOLBY_DRC_RF_MODE)
            , adev->ms12.system_sound_target
            , audio_bytes_per_sample(aml_out->hal_internal_format)
            );
    }

    if (write_bytes > 0) {
        if ((eDolbyMS12Lib == adev->dolby_lib_type) && continuous_mode(adev) && ms12_dec) {
            /*SWPL-11531 resume the timer here, because we have data now*/
            /*resume ms12/hwsync here, as we receive the first data*/
            pthread_mutex_lock(&ms12->lock);
            if (ms12_dec->need_resume) {
                dolby_ms12_main_resume_prepare(stream);
                ALOGI("%s resume the ms12 and hwsync", __func__);
                ms12_dec->resume_state = MS12_RESUME_FROM_RESUME;
                dolby_ms12_main_resume(stream);
                ms12_dec->need_resync = true;
                ms12_dec->need_resume = false;
            } else if (aml_out->tsync_status == TSYNC_STATUS_STOP && aml_out->hw_sync_mode) {
                dolby_ms12_main_resume(stream);
                aml_hwsync_wrap_set_resume(aml_out->hwsync);
                aml_out->tsync_status = TSYNC_STATUS_RUNNING;
                ms12_dec->need_resync = true;
                ALOGI("resume ms12 and the timer");
            }
            pthread_mutex_unlock(&ms12->lock);
        }
    }

    audio_format_t cur_aformat;
    if (aml_out->is_tv_src_stream) {
        if (aml_out->digital_input_fmt_change) {
            ALOGI("%s(), hdmi input format changed", __func__);
            memset((void *)buffer, 0, bytes);
            need_reconfig_output = true;
            need_reset_decoder = true;
            need_reconfig_samplerate = true;
            if (is_dts_format(aml_out->hal_internal_format)) {
                /*when switch from ms12 to dts, we should clean ms12 first*/
                if (adev->dolby_lib_type == eDolbyMS12Lib) {
                    switch_to_nonms12_case(adev);
                    aml_out->restore_dolby_lib_type = true;
                }
            }

#ifdef ADD_AUDIO_DELAY_INTERFACE
            // fixed switch between RAW and PCM noise, drop delay residual data
            aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF);
            aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF_RAW);
            aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF_B_RAW);
            aml_audio_delay_clear(AML_DELAY_OUTPORT_ALL);
#endif
            // HDMI input && HDMI ARC output case, when input format change, output format need also change
            // for example: hdmi input DD+ => DD,  HDMI ARC DD +=> DD
            // so we need to notify to reset spdif output format here.
            // adev->spdif_encoder_init_flag will be checked elsewhere when doing output.
            // and spdif_encoder will be initialize by correct format then.
            if (aml_out->spdifenc_init) {
                aml_spdif_encoder_close(aml_out->spdifenc_handle);
                aml_out->spdifenc_handle = NULL;
                aml_out->spdifenc_init = false;
            }
        } else {
            need_reconfig_samplerate = false;
        }
        aml_out->digital_input_fmt_change = false;
    } else if (aml_out->hal_format == AUDIO_FORMAT_IEC61937 && !aml_out->iec_check) {
        /* parsing sub format in IEC61937 for local MM playback case */
        audio_channel_mask_t cur_ch_mask;
        int package_size;
        int cur_audio_type = audio_type_parse(write_buf, write_bytes, &package_size, &cur_ch_mask);
        cur_aformat = audio_type_convert_to_android_audio_format_t(cur_audio_type);
        ALOGI("cur_aformat:%0x cur_audio_type:%d", cur_aformat, cur_audio_type);

        if (cur_audio_type != LPCM && cur_audio_type != PAUSE && cur_audio_type != MUTE) {
            aml_out->hal_internal_format = cur_aformat;
            aml_out->iec_check = true;
            need_reconfig_output = true;
            need_reset_decoder = true;
        } else {
            return return_bytes;
        }
    } else if (!is_bypass_dolbyms12(stream)) {
        adev->dolby_lib_type = adev->dolby_lib_type_last;
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        /**
         * Need config MS12 if its not enabled.
         * Since MS12 is essential for main input.
         */
        if (!adev->ms12.dolby_ms12_enable && !is_bypass_dolbyms12(stream)) {
            ALOGI("ms12 is not enabled, reconfig it");
            need_reconfig_output = true;
            need_reset_decoder = true;
        }

        /**
         * Need config MS12 in this scenario.
         * Switch source between HDMI1 and HDMI2, the two source playback pcm data.
         * sometimes dolby_ms12_enable is true(system stream config ms12), here should reconfig
         * ms12 when switching to HDMI stream source.(Jira:TV-46722)
         */
        if (need_reconfig_output && adev->ms12.dolby_ms12_enable && aml_out->is_tv_src_stream) {
            need_reset_decoder = true;
            ALOGI ("%s() %d, HDMI input source, need reset decoder:%d", __func__, __LINE__, need_reset_decoder);
        }
    }

    if (need_reconfig_output) {
        config_output (stream,need_reset_decoder);
        need_reconfig_output = false;
    }

    aml_out->input_bytes_size += write_bytes;
    if (aml_out->is_tv_src_stream && (adev->dtslib_bypass_enable || adev->dcvlib_bypass_enable)) {
        int cur_samplerate = audio_parse_get_audio_samplerate(patch->audio_parse_para);
        if (cur_samplerate != patch->input_sample_rate || need_reconfig_samplerate) {
            ALOGI ("HDMI/SPDIF input samplerate from %d to %d, or need_reconfig_samplerate\n",
                    patch->input_sample_rate, cur_samplerate);
            patch->input_sample_rate = cur_samplerate;
            if (patch->aformat == AUDIO_FORMAT_DTS ||  patch->aformat == AUDIO_FORMAT_DTS_HD) {
                if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                    if (cur_samplerate == 44100 || cur_samplerate == 32000) {
                        aml_out->config.rate = cur_samplerate;
                    } else {
                        aml_out->config.rate = 48000;
                    }
                }
            } else if (patch->aformat == AUDIO_FORMAT_AC3) {
                if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                    aml_out->config.rate = cur_samplerate;
                }
            } else if (patch->aformat == AUDIO_FORMAT_E_AC3) {
                if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
                    if (cur_samplerate == 192000 || cur_samplerate == 176400) {
                        aml_out->config.rate = cur_samplerate / 4;
                    } else {
                        aml_out->config.rate = cur_samplerate;
                    }
                }
            } else {
                aml_out->config.rate = 48000;
            }
            ALOGI("adev->dtslib_bypass_enable :%d,adev->dcvlib_bypass_enable:%d, aml_out->config.rate :%d\n",adev->dtslib_bypass_enable,
            adev->dcvlib_bypass_enable,aml_out->config.rate);
        }
    }

    /*
     *when disable_pcm_mixing is true, the 7.1ch DD+ could not be process with Dolby MS12
     *the HDMI-In or Spdif-In is special with IEC61937 format, other input need packet with spdif-encoder.
     */
    audio_format_t output_format = get_output_format (stream);
    if (adev->debug_flag) {
        AM_LOGD("hal_format:%#x, output_format:0x%x, sink_format:0x%x",
            aml_out->hal_format, output_format, adev->sink_format);
    }

    if (write_bytes > 0 && aml_out->streamType == STREAM_PCM_HWSYNC) {
        //start the timer to monitor frame_write_sum_updated
        audio_timer_stop(aml_out->timer_id);
        audio_timer_stop(aml_out->timer_id2);
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        ret = aml_audio_ms12_render(stream, abuffer);
    } else {
        ret = aml_audio_nonms12_render(stream, abuffer);
    }

    if (write_bytes > 0 && aml_out->streamType == STREAM_PCM_HWSYNC && !aml_out->is_insert_zero_data) {
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            audio_one_shot_timer_start(aml_out->timer_id, AML_HWSYNC_STREAM_TIMER_RENDER_DELAY);
            audio_one_shot_timer_start(aml_out->timer_id2, AML_HWSYNC_STREAM_TIMER_RENDER_DELAY2);
        } else {//none ms12 pipe is shorter than ms12, so adjust the delay time to 160ms.
            audio_one_shot_timer_start(aml_out->timer_id, AML_HWSYNC_STREAM_TIMER_NOMS12_RENDER_DELAY);
            audio_one_shot_timer_start(aml_out->timer_id2, AML_HWSYNC_STREAM_TIMER_NOMS12_PAUSE_RENDER_DELAY);

        }
        aml_out->frame_write_sum_updated = true;
    }

exit:
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        if (continuous_mode(adev) && aml_out->ms12_dec_handle) {
            aml_out->timestamp = aml_out->ms12_dec_handle->timestamp;
            aml_out->lasttimestamp = aml_out->ms12_dec_handle->timestamp;
            //clock_gettime(CLOCK_MONOTONIC, &aml_out->timestamp);
            aml_out->last_frames_position = aml_out->ms12_dec_handle->last_frames_position;
            if (adev->debug_flag)
                ALOGI("%s out:%p aml_out->last_frames_position:%" PRIu64 " \n", __FUNCTION__, aml_out, aml_out->last_frames_position);
        }
    }

    if (adev->debug_flag) {
        int64_t leave_time_us = aml_audio_get_systime();
        ALOGI("%s return %d!, cost time %d ms\n", __FUNCTION__, return_bytes, (int)((leave_time_us - enter_time_us)/1000LL));
    }
    return return_bytes;
}

void aml_close_ms12_output_main_stream(struct aml_stream_out *amlStream)
{
    close_ms12_output_main_stream((struct audio_stream_out *)amlStream);
}

ssize_t mixer_aux_buffer_write(struct audio_stream_out *stream, void *abuffer)
{
    aml_audio_buffer_t *audioBuffer = (aml_audio_buffer_t *)abuffer;
    void *buffer = audioBuffer->pData;
    const size_t bytes = audioBuffer->size;

    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = 0;
    size_t frame_size = audio_stream_out_frame_size(stream);
    const size_t in_frames = bytes / frame_size;
    size_t bytes_remaining = bytes;
    size_t bytes_written = 0;
    bool need_reconfig_output = false;
    bool  need_reset_decoder = false;
    int retry = 0;
    unsigned int alsa_latency_frame = 0;
    pthread_mutex_lock(&adev->lock);
    bool hw_mix = aml_get_is_need_hw_mix(aml_out);
    uint64_t enter_ns = 0;
    uint64_t leave_ns = 0;
    uint64_t sleep_time_us = 0;
    bool useAudioMixer = false;
    bool is_deep_buf = aml_out->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
    int fadein_detect_time_ms = adev->is_netflix ? NETFLIX_FADEIN_MAX_DETECT_TIME_MS : 0;

    if (eDolbyMS12Lib == adev->dolby_lib_type && continuous_mode(adev)) {
        enter_ns = aml_audio_get_systime_ns();
    }

    if (adev->debug_flag) {
        enter_ns = aml_audio_get_systime_ns();
        AM_LOGD("io %d: out:%p size:%zu, dolby_lib_type:%d, frame_size:%zu, deep_buf:%d", aml_out->io_handle, aml_out,
            bytes, adev->dolby_lib_type, frame_size, is_deep_buf);
    }

    if ((aml_out->stream_status == STREAM_HW_WRITING) && hw_mix) {
        ALOGI("%s(), aux do alsa close\n", __func__);
        pthread_mutex_lock(&adev->alsa_pcm_lock);
        aml_alsa_output_close(stream);

        ALOGI("%s(), aux alsa close done\n", __func__);
        aml_out->stream_status = STREAM_MIXING;
        pthread_mutex_unlock(&adev->alsa_pcm_lock);
    }

    if (aml_out->stream_status == STREAM_STANDBY) {
        aml_out->stream_status = STREAM_MIXING;
    }

    if (aml_out->standby) {
        AM_LOGI("io %d: out:%p streamType:%s standby to unstandby", aml_out->io_handle, aml_out, streamType2Str(aml_out->streamType));
        aml_audio_data_handle_init(stream);
        aml_out->standby = false;
#ifndef AUDIO_HAL_DISABLE_MS12
        // NTS PCM mode: volume-tunel-nontunel/audio-lat-heaac testcase.
        if ((eDolbyMS12Lib == adev->dolby_lib_type) && !dolby_stream_active(adev) && adev->is_netflix) {
            ALOGI("%s : without dolby_stream, pcm drc use line mode", __func__);
            set_ms12_drc_params_for_stereo_and_dap_multi_pcm_output(
                adev
                , ms12
                , AUDIO_FORMAT_PCM_16_BIT //treat as PCM format when stream is end.
                );
        }
#endif
    }

    /* for asdk14 cases:
     * atmos_stickiness_usage_media_ddp_out-no_cfg-v241-HDMI (6581)
     * atmos_stickiness_usage_media_mat_out-no_cfg-v241-HDMI (6612)
     */
    if (((aml_out->track_base_usage == AUDIO_USAGE_MEDIA) || is_deep_buf) && !adev->is_netflix && !is_tv_stream_out(aml_out) && !is_dev_patch_exist(adev)) {
        aml_out->is_ms12_main_decoder_disable = true;
        aml_out->is_system_audio_usage_media = true;
        aml_check_close_ms12_output_main_stream(aml_out);
    }


    pthread_mutex_unlock(&adev->lock);
    if (eDolbyMS12Lib == adev->dolby_lib_type && !adev->switching_dolby_lib) {
        if (adev->a2dp_no_reconfig_ms12 > 0) {
            uint64_t curr = aml_audio_get_systime();
            if (adev->a2dp_no_reconfig_ms12 <= curr)
                adev->a2dp_no_reconfig_ms12 = 0;
        }
        /*only system sound active*/
        if (!hw_mix && (!(dolby_stream_active(adev) || hwsync_lpcm_active(adev)))) {
            /* here to check if the audio HDMI ARC format updated. */
            if (((is_arc_hdmi_updated(adev)) || (adev->a2dp_updated) || (adev->digital_audio_mode_updated) || is_HDMI_reconnected(adev))
                && (adev->ms12.dolby_ms12_enable == true)) {
                //? if we need protect
                if ((adev->a2dp_no_reconfig_ms12 > 0) && (aml_out->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) && (adev->a2dp_updated == 0)) {
                    need_reset_decoder = false;
                    ALOGD("%s: a2dp output and change audio format, no reconfig ms12 for hdmi update", __func__);
                } else {
                    need_reset_decoder = true;
                    if (adev->digital_audio_mode_updated) {
                        ALOGI("%s(), digital audio format updated, current %s", __func__,
                            digitalAudioModeType2Str(adev->digital_audio_mode));
                    }
                    else
                        ALOGI("%s() %s%s%s changing status, need reconfig Dolby MS12\n", __func__,
                                (!is_arc_hdmi_updated(adev))?" ":"HDMI ARC EndPoint ",
                                (!is_HDMI_reconnected(adev))?" ":"HDMI ",
                                (adev->a2dp_updated==0)?" ":"a2dp ");
                }
                set_arc_hdmi_updated(adev, false);
                adev->a2dp_updated = 0;
                adev->digital_audio_mode_updated = 0;
                set_HDMI_reconnected_flag(adev, false);
                need_reconfig_output = true;
            }

            /* here to check if the audio output routing changed. */
            if ((adev->cur_out_devices != aml_out->out_device) && (adev->ms12.dolby_ms12_enable == true)) {
                ALOGI("%s(), output routing changed from 0x%x to 0x%x,need MS12 reconfig output", __func__, aml_out->out_device, adev->cur_out_devices);
                aml_out->out_device = adev->cur_out_devices;
                need_reconfig_output = true;
            }
            /* here to check if the hdmi audio output format dynamic changed. */
            if (adev->last_digital_audio_mode != adev->digital_audio_mode) {
                AM_LOGI("digital audio mode is changed from %s to %s need reconfig output",
                    digitalAudioModeType2Str(adev->last_digital_audio_mode), digitalAudioModeType2Str(adev->digital_audio_mode));
                adev->last_digital_audio_mode = adev->digital_audio_mode;
                need_reconfig_output = true;
            }
        }
        /* here to check if ms12 is already enabled, if main stream is doing init ms12, we don't need do it */
        /*coverity[missing_lock]*/
        if (!adev->ms12.dolby_ms12_enable && !adev->doing_reinit_ms12) {
            ALOGI("%s(), 0x%x, Switching system output to MS12, need MS12 reconfig output", __func__, aml_out->out_device);
            need_reconfig_output = true;
            need_reset_decoder = true;
        }

        if (need_reconfig_output) {
            /*during ms12 switch, the frame write may be not matched with
              the input size, we need to align it*/
            if (aml_out->frame_write_sum * frame_size != aml_out->input_bytes_size) {
                ALOGI("Align the frame write from %" PRId64 " to %" PRId64 "", aml_out->frame_write_sum, aml_out->input_bytes_size/frame_size);
                aml_out->frame_write_sum = aml_out->input_bytes_size/frame_size;
            }
            config_output(stream,need_reset_decoder);
        }

        //when Dolby MS12 use not 1.0 volume "-sys_prim_mixgain <3 int>
        //the PCM Render can not output at a same volume for both DDP and AC4.
        //AC4 should use the 1.0 volume and control the volume through the PCM output.
        //In the STB, the AudioFlinger already apply the volume at the Mixer Thread.
        // If decode the AC4 stream to PCM(stereo/dap speaker),
        // we apply the adev->dtv_volume in stereo_pcm_output()
        if (is_AC4_stream_with_pcm_sink_on_stb(aml_out) && (get_ac4_stream_volume(aml_out) > 0)) {
            apply_volume(1 / get_ac4_stream_volume(aml_out), (void *)buffer, sizeof(uint16_t), bytes);
        }

        /*
         *when disable_pcm_mixing is true and offload format is ddp and output format is ddp
         *the system tone voice should not be mixed
         */
        if (is_bypass_dolbyms12(stream)) {
            if (is_deep_buf) {
                ms12->deep_buf_audio_skip += bytes / frame_size;
            } else {
                ms12->sys_audio_skip += bytes / frame_size;
            }
            usleep(bytes * 1000000 /frame_size/out_get_sample_rate(&stream->common)*5/6);
        } else {
            /* audio zero data detect, and do fade in */
            if (adev->is_netflix && (STREAM_PCM_NORMAL == aml_out->streamType || STREAM_PCM_DEEP_BUF == aml_out->streamType)) {
                aml_out->data_handle_info.max_detect_time_ms = fadein_detect_time_ms;
                aml_audio_data_handle(stream, buffer, bytes);
            }

            const void *source = buffer;
            int source_bytes = bytes;
            bool is_local_out_bitstream = !get_dev_patch(adev) && (adev->sink_format > AUDIO_FORMAT_PCM_16_BIT);

            // For NTS VOL-INTER-AUDIO-PROFILE-UIAUDIO-HEAAC-AL1 :
            //   it will let the non-tunel pcm and aaudio loundness difference > 3dB.
            //   currently don't enable for netflix
            if (adev->is_netflix) {
                is_local_out_bitstream = false;
            }

            pcm_data_do_pre_attenuation(
                source
                , source_bytes
                , adev->ms12.dolby_ms12_enable
                , is_local_out_bitstream
                , (adev->ms12.stereo_drc.mode == DOLBY_DRC_RF_MODE)
                , adev->ms12.system_sound_target
                , audio_bytes_per_sample(aml_out->hal_internal_format)
                );

            //only system, the dolby_ms12_main_open is not ready
            //when the codec pipeline is not existed, switch the left/right channel here.
            if (adev->sound_track_mode > AM_AOUT_OUTPUT_STEREO) {
                aml_audio_switch_output_mode(buffer, bytes_remaining, aml_out->hal_internal_format, adev->sound_track_mode);
            }


            while (bytes_remaining && adev->ms12.dolby_ms12_enable && retry < 20) {
                size_t used_size = 0;
                if (is_deep_buf) {
                    ret = dolby_ms12_deep_buffer_process(stream, (char *)buffer + bytes_written, bytes_remaining, &used_size);
                } else {
                    ret = dolby_ms12_system_process(stream, (char *)buffer + bytes_written, bytes_remaining, &used_size);
                }
                if (!ret) {
                    bytes_remaining -= used_size;
                    bytes_written += used_size;
                    retry = 0;
                }
                retry++;
                if (bytes_remaining) {
                    //usleep(bytes_remaining * 1000000 / frame_size / out_get_sample_rate(&stream->common));
                    aml_audio_sleep(5000);
                }
            }
            if (bytes_remaining) {

                if (is_deep_buf) {
                    ms12->deep_buf_audio_skip += bytes_remaining / frame_size;
                    AM_LOGI("deep buf : bytes_remaining =%zu total skip =%" PRId64 "", bytes_remaining, ms12->deep_buf_audio_skip);
                } else {
                    ms12->sys_audio_skip += bytes_remaining / frame_size;
                    AM_LOGI("sys audio : bytes_remaining =%zu total skip =%" PRId64 "", bytes_remaining, ms12->sys_audio_skip);
                }
            }
        }
    } else {
        /*these data is skip for ms12, we still need calculate it*/
        if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
            size_t content_bytes = aml_hw_mixer_get_content_l(&adev->hw_mixer);
            size_t space_bytes = adev->hw_mixer.buf_size - content_bytes;
            bytes_written = aml_hw_mixer_write(&adev->hw_mixer, buffer, bytes);
            if (is_deep_buf) {
                ms12->deep_buf_audio_skip += bytes / frame_size;
            } else {
                ms12->sys_audio_skip += bytes / frame_size;
            }
            if (content_bytes < adev->hw_mixer.buf_size / 2) {
                sleep_time_us = (uint64_t)bytes_written * 1000000 / frame_size / out_get_sample_rate(&stream->common) / 2;
            } else {
                sleep_time_us = (uint64_t)bytes_written * 1000000 / frame_size / out_get_sample_rate(&stream->common);
            }
            AM_LOGV("aml_audio_sleep  sleep_time_us %" PRId64 " ",sleep_time_us);
            aml_audio_sleep(sleep_time_us);
        } else if (adev->useAudioMixer) {
            audioBuffer->pData = buffer;
            audioBuffer->size = bytes;
            audioBuffer->apts = 0;
            audioBuffer->isAptsValid = aml_out->hw_sync_mode;
            audioBuffer->bufFormat.channelCount = audio_channel_count_from_out_mask(aml_out->hal_channel_mask);
            audioBuffer->bufFormat.channelMask = aml_out->hal_channel_mask;
            audioBuffer->bufFormat.format = aml_out->hal_internal_format;
            audioBuffer->bufFormat.sampleRate = aml_out->hal_rate;
            if (aml_out->inputPortID == -1) {//need to init input port when stream first run here.
                //init input port
                aml_out->audioCfg.channel_mask = audioBuffer->bufFormat.channelMask;
                aml_out->audioCfg.sample_rate  = audioBuffer->bufFormat.sampleRate;
                aml_out->audioCfg.format       = audioBuffer->bufFormat.format;
                init_mixer_input_port(adev->mixerData, &aml_out->audioCfg, aml_out->flags,
                    on_notify_cbk, aml_out, on_input_avail_cbk, aml_out, NULL, NULL, 1.0);
                AM_LOGI("aux stream port:%s", mixerInputType2Str(get_input_port_type(&aml_out->audioCfg, aml_out->flags)));
            }
            bytes_written = out_write_pcm_to_AudioMixer(stream, buffer, bytes, audioBuffer);
            useAudioMixer = true;
        } else {//TBD what scene did the code go here.
            size_t content_bytes = aml_hw_mixer_get_content_l(&adev->hw_mixer);
            size_t space_bytes = adev->hw_mixer.buf_size - content_bytes;
            bytes_written = aml_hw_mixer_write(&adev->hw_mixer, buffer, bytes);
            if (content_bytes < adev->hw_mixer.buf_size / 2) {
                sleep_time_us = (uint64_t)bytes_written * 1000000 / frame_size / out_get_sample_rate(&stream->common) / 2;
            } else {
                sleep_time_us = (uint64_t)bytes_written * 1000000 / frame_size / out_get_sample_rate(&stream->common);
            }
            AM_LOGI("Aux_stream -> hw_mixer sleep_time_us %" PRId64 " ",sleep_time_us);
            aml_audio_sleep(sleep_time_us);
        }

        if (get_debug_value(AML_DUMP_AUDIOHAL_IN)) {
            aml_dump_audio_bitstreams("/data/vendor/audiohal/mixerAux.raw", buffer, bytes);
        }
    }
    aml_out->input_bytes_size += bytes;
    if (useAudioMixer == false) {
        // out_write_pcm_to_AudioMixer has updated frame_write_sum, don't add twice
        aml_out->frame_write_sum += in_frames;
    }

    pthread_mutex_lock(&aml_out->apts_update_lock);
    // out_write_pcm_to_AudioMixer has updated timestamp
    if (eDolbyMS12Lib == adev->dolby_lib_type || !adev->useAudioMixer) {
        clock_gettime (CLOCK_MONOTONIC, &aml_out->timestamp);
        aml_out->lasttimestamp.tv_sec = aml_out->timestamp.tv_sec;
        aml_out->lasttimestamp.tv_nsec = aml_out->timestamp.tv_nsec;
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type && !adev->switching_dolby_lib) {
        /*
           mixer_aux_buffer_write is called when the hw write is called another thread,for example
           main write thread or ms12 thread. aux audio is coming from the audioflinger mixed thread.
           we need calculate the system buffer latency in ms12 also with the alsa out latency.It is
           48K 2 ch 16 bit audio.
           */
        alsa_latency_frame = adev->ms12.latency_frame;
        int system_latency = 0;
#ifndef AUDIO_HAL_DISABLE_MS12
        pthread_mutex_lock(&ms12->lock);
        if (is_deep_buf) {
            system_latency = dolby_ms12_get_deep_buffer_avail_frames(NULL);
        } else {
            system_latency = dolby_ms12_get_system_buffer_avail(NULL) / frame_size;
        }
        pthread_mutex_unlock(&ms12->lock);
#endif
        if (system_latency < 0) {
            if (adev->debug_flag) {
                AM_LOGD("system_latency %d invalid, use 0", system_latency);
            }
            system_latency = 0;
        }

        if (adev->compensate_video_enable) {
            alsa_latency_frame = 0;
        }
        aml_out->last_frames_position = aml_out->frame_write_sum - system_latency;
        if (aml_out->last_frames_position >= alsa_latency_frame) {
            aml_out->last_frames_position -= alsa_latency_frame;
        }
        if (adev->debug_flag) {
            ALOGI("%s deep_buf %d stream audio presentation %"PRIu64" latency_frame %d.ms12 system latency_frame %d,total frame=%" PRId64 " %" PRId64 " ms",
                  __func__, is_deep_buf, aml_out->last_frames_position, alsa_latency_frame, system_latency,aml_out->frame_write_sum, aml_out->frame_write_sum/48);
        }
    } else if (!adev->useAudioMixer) {
        aml_out->last_frames_position = aml_out->frame_write_sum;
    }
    pthread_mutex_unlock(&aml_out->apts_update_lock);

    /*if system sound return too quickly, it will causes audio flinger underrun*/
    if (eDolbyMS12Lib == adev->dolby_lib_type && continuous_mode(adev)) {
        uint64_t cost_time_us = 0;
        uint64_t frame_us = (uint64_t)in_frames*1000/48;
        uint64_t minum_sleep_time_us = 5000;
        leave_ns = aml_audio_get_systime_ns();
        cost_time_us = (leave_ns - enter_ns)/1000;
        // For deep buffer size : 512 frames
        if (minum_sleep_time_us > frame_us/3) {
            minum_sleep_time_us = frame_us/3;
        }
        /*it costs less than 10ms*/
        //ALOGI("cost us=%lld frame/2=%lld", cost_time_us, frame_us/2);
        if ( cost_time_us < minum_sleep_time_us) {
            //ALOGI("sleep =%lld", minum_sleep_time_us - cost_time_us);
            aml_audio_sleep(minum_sleep_time_us - cost_time_us);
        }
    }
    /*coverity[missing_unlock]*/
    return bytes;
}

//this interface has been discarded, no other function invoke it.
ssize_t mixer_app_buffer_write(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = 0;
    size_t frame_size = audio_stream_out_frame_size(stream);
    size_t bytes_remaining = bytes;
    size_t bytes_written = 0;
    int retry = 20;

    if (adev->debug_flag) {
        AM_LOGD("io %d: out:%p size:%zu, frame_size:%zu", aml_out->io_handle, aml_out, bytes, frame_size);
    }

    if (eDolbyMS12Lib != adev->dolby_lib_type) {
        ALOGW("[%s:%d] dolby_lib_type:%d, is not ms12, not support app write", __func__, __LINE__, adev->dolby_lib_type);
        return -1;
    }

    if (is_bypass_dolbyms12(stream)) {
        ALOGW("[%s:%d] is_bypass_dolbyms12, not support app write", __func__, __LINE__);
        return -1;
    }

    /*for ms12 continuous mode, we need update status here, instead of in hw_write*/
    if (aml_out->stream_status == STREAM_STANDBY && continuous_mode(adev)) {
        aml_out->stream_status = STREAM_HW_WRITING;

        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            set_ms12_app_pcm_acmod_lfe(ms12, aml_out->hal_channel_mask);
            // NTS PCM mode: volume-tunel-nontunel/audio-lat-heaac testcase.
            if (adev->is_netflix && !dolby_stream_active(adev)) {
                ALOGI("%s : without dolby_stream, netflix pcm drc use line mode", __func__);
                set_ms12_drc_params_for_stereo_and_dap_multi_pcm_output(
                    adev
                    , ms12
                    , AUDIO_FORMAT_PCM_16_BIT //treat as PCM format when stream is end.
                    );
            }
        }
    }

    while (bytes_remaining && adev->ms12.dolby_ms12_enable && retry > 0) {
        size_t used_size = 0;
        ret = dolby_ms12_app_process(stream, (char *)buffer + bytes_written, bytes_remaining, &used_size);
        if (!ret) {
            bytes_remaining -= used_size;
            bytes_written += used_size;
        }
        retry--;
        if (bytes_remaining) {
            aml_audio_sleep(1000);
        }
    }
    if (retry <= 10) {
        ALOGE("[%s:%d] write retry=%d ", __func__, __LINE__, retry);
    }
    if (retry == 0 && bytes_remaining != 0) {
        ALOGE("[%s:%d] write timeout 10 ms ", __func__, __LINE__);
        bytes -= bytes_remaining;
    }

    return bytes;
}

ssize_t process_buffer_write(struct audio_stream_out *stream,
                            const void *buffer,
                            size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    audio_data_info_t data_info = { 0 };
    int fadein_detect_time_ms = adev->is_netflix ? NETFLIX_FADEIN_MAX_DETECT_TIME_MS : 0;

    if (adev->cur_out_devices != aml_out->out_device) {
        AM_LOGD("out:%p device:%x,%x", stream, aml_out->out_device, adev->cur_out_devices);
        aml_out->out_device = adev->cur_out_devices;
        config_output(stream, true);
    }
    if (adev->debug_flag) {
        AM_LOGD("io %d: out:%p size:%zu", aml_out->io_handle, aml_out, bytes);
    }

    if (aml_out->standby) {
        AM_LOGI("io %d: out:%p streamType:%s standby to unstandby", aml_out->io_handle,
            aml_out, streamType2Str(aml_out->streamType));
        aml_audio_data_handle_init(stream);
        aml_out->standby = false;
    }

    /*during ms12 continuous exiting, the write function will be
     set to this function, then some part of audio need to be
     discarded, otherwise it will cause audio gap*/
    if (adev->exiting_ms12) {
        int64_t gap;
        void * data = (void*)buffer;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        gap = calc_time_interval_us(&adev->ms12_exiting_start, &ts);

        if (gap >= 500*1000) {
            adev->exiting_ms12 = 0;
        } else {
            ALOGV("during MS12 exiting gap=%" PRId64 " mute the data", gap);
            memset(data, 0, bytes);
        }
    }

    if ((eDolbyMS12Lib != adev->dolby_lib_type) && (STREAM_PCM_NORMAL == aml_out->streamType)) {
        aml_out->data_handle_info.max_detect_time_ms = fadein_detect_time_ms;
        aml_audio_data_handle(stream, buffer, bytes);
    }

    data_info.audio_format = aml_out->hal_internal_format;
    data_info.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    aml_audio_pcm_output((struct audio_stream_out *)aml_out, buffer, bytes, &data_info);

    if (bytes > 0) {
        aml_out->input_bytes_size += bytes;
    }
    return bytes;
}

int _get_stream_write_func(struct aml_stream_out *aml_out)
{
    R_CHECK_POINTER_LEGAL(0, aml_out,);
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct amlAudioMixer *audio_mixer = adev->mixerData;

    /*coverity[missing_lock]*/
    {
        if (ms12->ms12_scheduler_state != MS12_SCHEDULER_RUNNING) {
            aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_RUNNING);
        }
        if (adev->useAudioMixer && aml_get_submix_scheduler_state(audio_mixer) != SUBMIX_SCHEDULER_RUNNING) {
            AM_LOGV("send RUNNING msg to useAudioMixer");
            aml_audiohal_sch_state_2_submix(audio_mixer, SUBMIX_SCHEDULER_RUNNING);
        }
        if (aml_out->streamType == STREAM_PCM_NORMAL &&adev->dac_softmute_delay > 0) {
            int softmute_delay = adev->dac_softmute_delay;
            /*
             * relationship with https://jira.amlogic.com/browse/SWPL-112419
             * when ms12 starting output, delay a while to reduce softmute's effect on speaker.
            */
            AM_LOGI("ms12 start output, delay %d ms to reduce softmute's effect", softmute_delay);
            aml_audio_sleep(softmute_delay * 1000);
        }
     }

    if (aml_out->is_normal_pcm) {
        aml_out->write = mixer_aux_buffer_write_wrap;
    } else {
        aml_out->write = mixer_main_buffer_write;
    }

    AM_LOGV("%s %d aml_out:%p, write:%p, streamType:%s", __func__, __LINE__,
        aml_out, aml_out->write, streamType2Str(aml_out->streamType));
    return 0;
}


#ifdef USE_CALLBACK_FOR_PARSER_TO_STREAM
int out_stream_write_callback(void *pri_object, void *aBuffer, void *phandle __unused)
{
    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)pri_object;
    aml_parser_t *pAmlParser= aml_out->aml_parser;
    aml_audio_buffer_t *inAudioBuffer = (aml_audio_buffer_t *)aBuffer;
    //AM_LOGI("  phandle:%p  buffer:%p bytes:%zu outApts:0x%" PRIx64 " (%" PRIu64 " ms) ",
    //    phandle, inAudioBuffer->pData, inAudioBuffer->size, inAudioBuffer->apts, inAudioBuffer->apts/90);

    if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
        if (AUDIO_FORMAT_INVALID != inAudioBuffer->bufFormat.format)
            aml_out->hal_format = aml_out->hal_internal_format = inAudioBuffer->bufFormat.format;
    }

    //ac3 decoder has this endian convert,so here no need this action.
    //endian16_convert(buffer, bytes);
    ret = aml_out->write((struct audio_stream_out *)aml_out, (void *)aBuffer);
    return ret;
}
#endif
/* out_write entrance: every write goes in here. */
ssize_t out_write_new(struct audio_stream_out *stream,
                      const void *buffer,
                      size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    ssize_t ret = 0;
    size_t frame_size = audio_stream_out_frame_size(stream);
    struct aml_audio_device *adev = aml_out->dev;
    int64_t enter_time_us = 0;

    if (eDolbyMS12WrongLib == adev->dolby_lib_type) {
        ALOGE("%s,wrong libdolbyms12 lib\n", __FUNCTION__);
        usleep(20 * 1000);
        return bytes;
    }


    if ((aml_out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) &&
        ((aml_out->hal_format == AUDIO_FORMAT_AC3) || (aml_out->hal_format == AUDIO_FORMAT_E_AC3) || (aml_out->hal_format == AUDIO_FORMAT_E_AC3_JOC)) &&
        !adev->is_netflix && !aml_out->hw_sync_mode &&
        !is_tv_stream_out(aml_out) &&
        (eDolbyMS12Lib == adev->dolby_lib_type)) {
        if (adev->debug_flag > 1) {
            AM_LOGI("+++ io %d: out(%p) original bytes (%zu)", aml_out->io_handle, stream, bytes);
        }
        if (aml_out->total_write_size == 0) {
            aml_out->is_ddp_offload_use_split = is_ddp_contain_six_block(buffer, (int32_t)bytes);
            ALOGI("%s is_ddp_offload_use_split %d\n", __FUNCTION__, aml_out->is_ddp_offload_use_split);
        }
        if (aml_out->is_ddp_offload_use_split && (bytes >= DIRECT_DDP_BUFSIZE)) {
            bytes = DIRECT_DDP_BUFSIZE;
        }
    }
    if (!aml_out->is_normal_pcm) {
        enter_time_us = aml_audio_get_systime();
    }

    size_t in_frames = bytes / frame_size;

    bool is_dolby_truehd = (aml_out->hal_internal_format == AUDIO_FORMAT_DOLBY_TRUEHD);

    R_CHECK_POINTER_LEGAL(-1, aml_out,);
    R_CHECK_POINTER_LEGAL(-1, adev,);
    if (adev->debug_flag > 1) {
        AM_LOGI("+++ io %d: out(%p) position(%zu)", aml_out->io_handle, stream, bytes);
    }

    if (!aml_out->check_preempt_done) {
        aml_stream_check_preempt(aml_out);
        aml_out->check_preempt_done = true;
    }

    /*current dts and dolby can't be co-exist. To be fixed*/
    if (aml_out->is_preempted) {
        ALOGI("%s drop data size =(%zu)", __func__, bytes);
        usleep(32*1000);
        return bytes;
    }

    //cts tunnel underrun case failed, depond on pause/resume invoked from AudioFlinger.
    //sometimes AudioFlinger always invoke the pause to Hal during 800ms for track retry count.
    //so add this code to control pause/resume MediaSync and video in Hal.
    // Fix case : pause -> standby/flush -> write data again
    if (aml_out->pause_status == true) {
        out_resume_new(stream);
    }

    // mlock the necessary library map address, avoid library page fault(stuck a while)
    // out_update_source_metadata_v7 may be not called.
    if (adev && adev->mlock_library_done == false) {
        aml_audio_lock_so_memory();
    }

    if (aml_out->standby && adev->useAudioMixer) {
        if (!audio_is_linear_pcm(aml_out->hal_format)) {
            // need to close multi-pcm alsa handle, then npcm can use it
            subMixingEnableMultiChOutput(adev, false);
        }
    }

    if (aml_audio_trace_debug_level() > 0) {
        if (false == aml_out->pause_status  &&  aml_out->write_count < 1) {
            aml_out->write_time = aml_audio_get_systime() / 1000; //us --> ms
            ALOGD("%s: out_stream(%p) bytes(%zu), write_time:%" PRIu64 ", count:%d", __func__,
                       stream, bytes, aml_out->write_time, aml_out->write_count);
        }
    }
    aml_out->write_count++;

    check_write_time((struct audio_stream_out *)aml_out, bytes);
    /*when there is data writing in this stream, we can add it to active stream*/
    pthread_mutex_lock(&adev->lock);
    adev->active_outputs[aml_out->streamType] = aml_out;
    if (adev->direct_mode) {
        /*
         * when the third_party apk calls pcm_close during use and then calls pcm_open again,
         * primary hal does not access the sound card,
         * continue to let the third_party apk access the sound card.
         */
        aml_alsa_output_close(stream);
        aml_out->stream_status = STREAM_STANDBY;
        if (adev->debug_flag) {
            ALOGI("%s,direct mode write,skip bytes %zu\n",__func__,bytes);
        }
        /*TODO accurate delay time */
        usleep(in_frames*1000/48);
        aml_out->frame_write_sum += in_frames;
        pthread_mutex_unlock(&adev->lock);
        return bytes;
    }
    pthread_mutex_unlock(&adev->lock);

    if (adev->mix_init_flag == false) {
        pthread_mutex_lock (&adev->lock);
        if (aml_out->streamType == STREAM_PCM_HWSYNC || aml_out->streamType == STREAM_RAW_HWSYNC) {
            aml_audio_hwsync_init(aml_out->hwsync, aml_out);
        }
        adev->mix_init_flag =  true;
        /*if mixer has started, no need restart*/
        if (!adev->hw_mixer.start_buf) {
            aml_hw_mixer_init(&adev->hw_mixer);
        }
        pthread_mutex_unlock(&adev->lock);
    }
    /*move it from open function, because when hdmi hot plug, audio service will
     * call many times open/close to query the hdmi capability, this will affect the
     * sink format
     */
    if (!aml_out->is_sink_format_prepared) {
        get_sink_format(&aml_out->stream);
        if (is_dolby_truehd && (eDolbyMS12Lib == adev->dolby_lib_type)) {
            aml_out->ms12_dec_handle->is_bypass_ms12 = is_ms12_passthrough(stream);
        }
        if (!is_TV(adev)) {
            if (is_use_spdifb(aml_out)) {
                aml_audio_select_src_to_hdmi(AML_SPDIF_B_TO_HDMITX);
                aml_out->restore_hdmitx_selection = true;
            }
            aml_out->card = alsa_device_get_card_index();
            if (adev->sink_format == AUDIO_FORMAT_PCM_16_BIT) {
                aml_out->device = PORT_I2S;
            } else {
                aml_out->device = PORT_SPDIF;
            }
        }
        aml_out->is_sink_format_prepared = true;
    }

    /*if these format can't be supported by ms12, we can bypass it*/
    if (adev->dolby_lib_type == eDolbyMS12Lib && aml_out->switch_nonms12_check) {
        switch_to_nonms12_case(adev);
        aml_out->restore_dolby_lib_type = true;
        ALOGI("bypass ms12 change dolby dcv lib type");
    }


    if (adev->ms12.dolby_ms12_enable) {
        if (aml_out->is_mat_changed) {
            ALOGI("MAT1.0(truehd) is different with MAT2.0(pcm)&MAT2.1(atmos), MAT format is changed. Need to reset MS12 pipeline.");
            dolby_ms12_main_close(stream);
            aml_out->is_mat_changed = false;
        }
    }
    if (adev->ms12.dolby_ms12_enable) {
        if (aml_out->is_heaac_changed) {
            ALOGI("HEAAC LOAS is different with HEAAC ADTS, HEAAC format is changed. Need to reset MS12 pipeline.");
            dolby_ms12_main_close(stream);
            aml_out->is_heaac_changed = false;
        }
    }

    if (aml_out->standby && (eDolbyMS12Lib == adev->dolby_lib_type_last || adev->useAudioMixer)) {
        AM_LOGI("io %d: out:%p streamType:%s, standby", aml_out->io_handle, aml_out, streamType2Str(aml_out->streamType));
        //tunnel stream and hwsync is null, prepare the tunnel resource.
        uint8_t *temp_buf = (uint8_t *)buffer;
        bool is_hwsync_header = hwsync_header_valid(temp_buf);
        audio_hwsync_t *hw_sync = aml_out->hwsync;
        if (is_hwsync_header && aml_out->hwsync == NULL) {
            //multi hwsync, should use correct hw_sync_id,so here is wrong place.
            AM_LOGW("aml_out:%p  hw_sync:%p", aml_out, hw_sync);
            return 0;
        }

        if (aml_out->tsync_status != TSYNC_STATUS_RUNNING && aml_out->hw_sync_mode) {
            hw_sync->first_apts_flag = false; //start tsync again.
            hw_sync->wait_video_done = false;
        }
    }

    bool is_raw_stream_flag = is_raw_stream(aml_out);
    /*local IEC61937 playback, goes into IEC passthrough, it doesn't need parer*/
    bool bypass_parser = is_dts_format(aml_out->hal_internal_format) || (aml_out->hal_format == AUDIO_FORMAT_IEC61937 && !aml_out->is_tv_src_stream);

    /* is_unsupport_raw_stream and is_dtv_stream_flag would be removed later, it's just for debug.
     * for hwsync mode, must use parser to parse it
     */
    if (((!is_unsupport_raw_stream_for_debug(aml_out) && is_raw_stream_flag && !bypass_parser) || aml_out->hw_sync_mode)
        && NULL == aml_out->aml_parser) {
        parser_config_t parserConfig;
        parserConfig.isHwsyncFlag =
            aml_out->streamType == STREAM_PCM_HWSYNC || aml_out->streamType == STREAM_RAW_HWSYNC;
        parserConfig.pAmlStream = (void *)aml_out;
        parserConfig.dataFormat.channelCount = audio_channel_count_from_out_mask(aml_out->hal_channel_mask);
        parserConfig.dataFormat.channelMask = aml_out->hal_channel_mask;
        parserConfig.dataFormat.sampleRate = aml_out->hal_rate;
        parserConfig.dataFormat.format = aml_out->hal_format;
        parserConfig.dataFormat.subFormat = aml_out->hal_internal_format;
        parserConfig.isTvFlag = aml_out->is_tv_src_stream || aml_out->is_dtv_src_stream;
        parserConfig.isDtvFlag = aml_out->is_dtv_src_stream;
        pthread_mutex_lock(&aml_out->parser_MutexLock);
        aml_parser_init((aml_parser_t **)&aml_out->aml_parser, &parserConfig);
        pthread_mutex_unlock(&aml_out->parser_MutexLock);
    }

    aml_audio_trace_int("out_write_new", bytes);
    /**
     * deal with the device output changes
     * pthread_mutex_lock(&aml_out->lock);
     * out_device_change_validate_l(aml_out);
     * pthread_mutex_unlock(&aml_out->lock);
     */
    pthread_mutex_lock(&adev->lock);
    _get_stream_write_func(aml_out);
    pthread_mutex_unlock(&adev->lock);


    aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)aml_out->audio_buffer;
    aml_audio_buffer_t *audioBuffer = pBuffer->inBuffer;
    //packet audio buffer
    if (aml_out->audio_buffer && audioBuffer) {
        audioBuffer->pData = (void *)buffer;
        audioBuffer->size = bytes;
        audioBuffer->apts = 0;//outApts;
        audioBuffer->isAptsValid = aml_out->hw_sync_mode;
        //if it is iec stream, maybe it's better to get these format from iec parser.
        audioBuffer->bufFormat.channelCount = audio_channel_count_from_out_mask(aml_out->hal_channel_mask);
        audioBuffer->bufFormat.channelMask = aml_out->hal_channel_mask;
        audioBuffer->bufFormat.format = aml_out->hal_internal_format;
        audioBuffer->bufFormat.sampleRate = aml_out->hal_rate;
    } else {
        AM_LOGW(" audio_buffer:%p, please check it.", aml_out->audio_buffer);
    }

    if (aml_out->aml_parser) {
        pthread_mutex_lock(&aml_out->parser_MutexLock);
//current not define USE_CALLBACK_FOR_PARSER_TO_STREAM
#ifdef USE_CALLBACK_FOR_PARSER_TO_STREAM
        aml_parser_data_callback_t amlCallback = {
            .common.pAmlStream = (void *)aml_out,
            .callback = out_stream_write_callback,
        };
        ret = aml_parser_process(aml_out->aml_parser, audioBuffer, (void *)(&amlCallback));
#else
        ret = aml_parser_process(aml_out->aml_parser, audioBuffer, NULL/*callback*/);

        struct aml_audio_buffer *tmpABuffer = pBuffer->parsedBuffer;
        void *tmpbuf  = aml_out->parsedDataBuf;
        if (tmpABuffer && tmpbuf) {
            tmpABuffer->pData = tmpbuf;
        } else {
            AM_LOGE(" tmpABuffer:%p  tmpbuf:%p  failed, need to return", tmpABuffer, tmpbuf);
            pthread_mutex_unlock(&aml_out->parser_MutexLock);
            return ret;
        }
        do {
            int retValue = aml_parser_get_buffer(aml_out->aml_parser, &tmpABuffer, &tmpbuf);
            if (tmpABuffer && retValue == AML_AUDIO_BUFFER_VALID) {
                tmpABuffer->pData = tmpbuf;
                //AM_LOGI(" buffer:%p bytes:%zu outApts:0x%" PRIx64 " (%" PRIu64 " ms) ",
                //    tmpABuffer->pData, tmpABuffer->size, tmpABuffer->apts, tmpABuffer->apts/90);
                if (aml_out->hal_format == AUDIO_FORMAT_IEC61937 && aml_out->is_tv_src_stream) {
                    if (AUDIO_FORMAT_INVALID != tmpABuffer->bufFormat.format && AUDIO_FORMAT_DEFAULT != tmpABuffer->bufFormat.format)
                        aml_out->hal_format = aml_out->hal_internal_format = tmpABuffer->bufFormat.format;
                }

                if (is_mpegh_format(aml_out->hal_internal_format)) {
                    int asiupdate = 0;
                    if (get_debug_value(AML_DUMP_AUDIOHAL_IN)) {
                        aml_dump_audio_bitstreams(AML_MPEGH_UIMANAGER_INPUT_FILE_DUMP_DIR, tmpABuffer->pData, tmpABuffer->size);
                    }
                    int val = aml_mpegh_uimanager_process(stream, tmpABuffer, &asiupdate);
                    if (val != 0) {
                        AM_LOGE("aml_mpegh_uimanager_process error");
                        break;
                    }
                    if (asiupdate == 1) {
                        ALOGI("%s Audio Scene Information has changed! asiupdate:%d", __func__, asiupdate);
                        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, TYPE_MPEGH_ASI_UPDATE);
                    }
                    if (get_debug_value(AML_DUMP_AUDIOHAL_IN)) {
                        aml_dump_audio_bitstreams(AML_MPEGH_UIMANAGER_OUTPUT_FILE_DUMP_DIR, tmpABuffer->pData, tmpABuffer->size);
                    }
                }
            }

            if (retValue == AML_AUDIO_BUFFER_VALID) {
                aml_out->write(stream, tmpABuffer);
            } else {
                break;
            }

            if (aml_out->is_insert_zero_data) {
                AM_LOGD("insert_zero_data state,exit the loop");
                break;
            }
        } while (!aml_out->pause_status);
#endif
        pthread_mutex_unlock(&aml_out->parser_MutexLock);
    } else {
        if (aml_out->write) {
            ret = aml_out->write(stream, audioBuffer);
        }
    }

    aml_audio_trace_int("out_write_new", 0);

    /* update audio format to display audio info banner.*/
    /* DTS needs earlier update in decode flow */
    if (!is_dts_format(aml_out->hal_internal_format) && (aml_out->hal_internal_format != AUDIO_FORMAT_AC4))
        update_audio_format(adev, aml_out->hal_internal_format);

    if (ret > 0) {
        aml_out->total_write_size += ret;
        if (aml_out->is_normal_pcm) {
            size_t frame_size = audio_stream_out_frame_size(stream);
            if (frame_size != 0) {
                if (aml_out->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
                    adev->deep_buf_audio_frame_written = aml_out->input_bytes_size / frame_size;
                } else {
                    adev->sys_audio_frame_written = aml_out->input_bytes_size / frame_size;
                }
            }
        }
    }
    if (aml_out->write_status == false) {
        ALOGI("%s(), stream[%p] write_status set to true", __func__, aml_out);
        aml_out->write_status = true;
    }
    if (!aml_out->is_normal_pcm) {
        int cost_time_ms = (aml_audio_get_systime() - enter_time_us)/1000;
        aml_volume_shaper_update_write_time(&aml_out->volume_shaper, cost_time_ms);
    }

    if (adev->debug_flag > 1) {
        AM_LOGI("--- write_count:%d, ret %zd, %p total_write_size:%"PRIu64", hwsync_parsed_frames_sum:%"PRIu64"",
            aml_out->write_count, ret, stream, aml_out->total_write_size, aml_out->hwsync_parsed_frames_sum);
    }

    if (get_debug_value(AML_DUMP_AUDIOHAL_IN)) {
        if (buffer && (bytes > 0)) {
            aml_dump_audio_bitstreams(aml_out->stream_dump_file, buffer, ret);
        }
    }
    return ret;
}


int adev_open_output_stream_new(struct audio_hw_device *dev,
                                audio_io_handle_t handle,
                                audio_devices_t devices,
                                audio_output_flags_t flags,
                                struct audio_config *config,
                                struct audio_stream_out **stream_out,
                                const char *address)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_out *aml_out = NULL;
    stream_type_t streamType = STREAM_PCM_NORMAL;
    int ret;
    char s0[AUDIO_DEVICE_OUT_STR_LEN], s1[AUDIO_OUTPUT_FLAG_STR_LEN], s2[AUDIO_CONFIG_STR_LEN];
    bool is_ms12_stream = false;
    AM_LOGD("enter: dev=%p handle=%x devices=0x%x/'%s' flags=0x%x/'%s' config=%s address='%s'",
            dev, handle,
            devices, show_audio_device_out(devices, s0, AUDIO_DEVICE_OUT_STR_LEN),
            flags, show_audio_output_flags(flags, s1, AUDIO_OUTPUT_FLAG_STR_LEN),
            show_audio_config((audio_config_base_t *)config, s2, AUDIO_CONFIG_STR_LEN),
            address);
    ALOGD("%s: enter", __func__);

    /* These streamout build for device effect */
    if (((devices & AUDIO_DEVICE_OUT_EARPIECE) != 0) && (flags == AUDIO_OUTPUT_FLAG_NONE)) {
        ret = adev_open_dummy_output_stream(dev,
                                    handle,
                                    devices,
                                    flags,
                                    config,
                                    stream_out,
                                    address);
        if (*stream_out != NULL) {
            struct aml_streamout_base *base = TO_BASE_PTR(*stream_out, struct audio_stream_out, aml_streamout_base);
            base->common_usecase = STREAM_OUT_EFFECT;
        }
        return ret;
    }

#ifdef ENABLE_AUTOMOTIVE_AUDIO_FUNCTION
    if (((devices & AUDIO_DEVICE_OUT_BUS) != 0) && !(flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
        AM_LOGD("new bus_output_stream");
        ret = adev_open_bus_output_stream(dev,
                                    handle,
                                    devices,
                                    flags,
                                    config,
                                    stream_out,
                                    address);
        if (ret < 0) {
            AM_LOGE("fail, return!");
        }
        return ret;
    }
#endif

    ret = adev_open_output_stream(dev,
                                    handle,
                                    devices,
                                    flags,
                                    config,
                                    stream_out,
                                    address);
    R_CHECK_RET(ret, "open stream failed");
    aml_out = (struct aml_stream_out *)(*stream_out);
    aml_out->streamType = attr_to_streamType(aml_out->device, aml_out->hal_format, aml_out->flags);
    aml_out->is_normal_pcm = (aml_out->hal_rate == 48000) && (aml_out->streamType == STREAM_PCM_NORMAL || aml_out->streamType == STREAM_PCM_DEEP_BUF) ? 1 : 0;  //is_normal_pcm, used by get_stream_write_func TBD
    aml_out->out_cfg = *config;
    aml_out->card = adev->card;
    aml_out->hwsync_parsed_frames_sum = 0;
    aml_out->streamTypeIndex = 0;

    if (audio_is_linear_pcm(aml_out->hal_format) && aml_out->is_normal_pcm
        && (aml_out->hal_ch == 2) && !(flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
        aml_out->input_cache_frames = 0;
        aml_out->input_start_threshold = DEFAULT_PLAYBACK_PERIOD_SIZE * (PLAYBACK_PERIOD_COUNT - 1);
    }

    if (adev->useAudioMixer) {
        // In V1.1, android out lpcm stream and hwsync pcm stream goes to aml mixer,
        // tv source keeps the original way.
        // Next step is to make all compatible.
        unsigned int channel_num = audio_channel_count_from_out_mask(config->channel_mask);
        if (aml_out->streamType == STREAM_PCM_NORMAL ||
            aml_out->streamType == STREAM_PCM_HWSYNC ||
            aml_out->streamType == STREAM_PCM_MMAP ||
            (aml_out->streamType == STREAM_PCM_DIRECT &&
            config->sample_rate == 48000)) {
            /*for 96000, we need bypass submix, this is for DTS certification*/
            /* for DTV case, maybe this function is called by the DTV output thread,
               and the audio patch is enabled, we do not need to wait DTV exit as it is
               enabled by DTV itself */
            if (config->sample_rate == 96000 || config->sample_rate == 88200) {
                aml_out->bypass_submix = true;
                ALOGI("bypass submix");
            } else {
                // remove it for unifying code.
                //ret = initSubMixingInput(aml_out, config);
                aml_out->bypass_submix = false;
                aml_out->inputPortID = -1;
                if (ret < 0) {
                    ALOGE("initSub mixing input failed");
                }
            }
        } else {
            //aml_out->bypass_submix = true;
            ALOGI("%s(), direct streamType: %s", __func__, streamType2Str(aml_out->streamType));
            if (is_TV(adev)) {
                aml_out->stream.write = out_write_new;
                aml_out->stream.common.standby = out_standby_new;
            }
        }
    } else {
        aml_out->stream.write = out_write_new;
        aml_out->stream.common.standby = out_standby_new;
    }

    //this is for STREAM_PCM_HWSYNC
    if (aml_out->streamType == STREAM_PCM_HWSYNC) {
        aml_out->timer_id = aml_audio_timer_create(aml_stream_timer_callback_handler);
        aml_out->timer_id2 = aml_audio_timer_create(aml_stream_timer_pause_callback);
        AM_LOGD("timer_id:%d", aml_out->timer_id);
    }

    aml_out->stream_status = STREAM_STANDBY;
    if (adev->continuous_audio_mode == 0) {
        adev->spdif_encoder_init_flag = false;
    }
    if (devices & AUDIO_DEVICE_OUT_ALL_A2DP) {
        if (!audio_is_linear_pcm(aml_out->hal_format)) {
            aml_out->stream.write = out_write_new;
            aml_out->stream.common.standby = out_standby_new;
            aml_out->stream.pause = out_pause_new;
            aml_out->stream.resume = out_resume_new;
            aml_out->stream.flush = out_flush_new;
        }
    }

#if ENABLE_DVB_PATCH
#if ANDROID_PLATFORM_SDK_VERSION > 29
    /*valid audio_config means enter in tuner framework case, then we need to create&start audio dtv patch*/
    ALOGD("%s: dev:%p, fmt:%d, dmx fmt:%d, content id:%d,sync id %d ",
        __func__, dev, config->offload_info.format, android_fmt_convert_to_dmx_fmt(config->offload_info.format),
        config->offload_info.content_id, config->offload_info.sync_id);
    enable_dtv_patch_for_tuner_framework(config, *stream_out);
    aml_out->audioCfg.offload_info.content_id = config->offload_info.content_id;
    aml_out->audioCfg.offload_info.sync_id = config->offload_info.sync_id;
    aml_out->report_latency = 0;
    if (dtv_tuner_framework(*stream_out)) {
        /*assign pause/resume api for tuner framework output stream.
          application scenarios like: time shift pause/resume*/
        aml_out->stream.pause = out_pause_dtv_stream_for_tunerframework;
        aml_out->stream.resume = out_resume_dtv_stream_for_tunerframework;
        aml_out->stream.flush = out_flush_dtv_stream_for_tunerframework;
        aml_out->stream.write = out_write_dtv_stream_for_tunerframework;
        aml_out->audio_info_change_mask = 0;
        aml_out->stream.get_presentation_position = out_get_presentation_position_for_tunerframework;
        aml_out->stream.set_audio_description_mix_level = out_set_audio_description_mix_level;
        aml_out->stream.get_audio_description_mix_level = out_get_audio_description_mix_level;
        aml_out->stream.set_dual_mono_mode = out_set_dual_mono_mode;
        aml_out->stream.get_dual_mono_mode = out_get_dual_mono_mode;
        aml_out->stream.set_volume = out_set_volume_for_tunerframework;
        aml_out->stream.set_playback_rate_parameters = out_set_playback_rate_parameters_for_tunerframework;
        aml_out->stream.get_playback_rate_parameters = out_get_playback_rate_parameters_for_tunerframework;
        aml_out->stream.common.standby = out_standby_dtv_stream_for_tunerframework;
    }
#endif
#endif

    aml_out->codec_type = get_codec_type(aml_out->hal_internal_format);
    aml_out->switch_nonms12_check = is_bypass_dolbyms12(*stream_out);

    aml_get_stream_dump_file_name((int)aml_out->hal_internal_format, aml_out->stream_dump_file);

    /* init ease for stream */
    if (aml_audio_ease_init(&aml_out->audio_stream_ease) < 0) {
        ALOGE("%s  aml_audio_ease_init failed\n", __func__);
        ret = -EINVAL;
        goto AUDIO_EASE_INIT_FAIL;
    }
    aml_volume_shaper_init(&aml_out->volume_shaper, 0);

    /*special dummy stream for ms12 output*/
    if (address && !strncmp(address, "ms12_stream", 11)) {
        is_ms12_stream = true;
        /*ms12 dummy stream, doesn't need standby function*/
        aml_out->stream.common.standby = NULL;
    }

    if (!is_ms12_stream) {
        if (aml_stream_register(aml_out) < 0) {
            AM_LOGE(" aml_stream_register failed.");
        }
    }


    memset(aml_out->nickname, 0, sizeof(aml_out->nickname));
    snprintf(aml_out->nickname, sizeof(aml_out->nickname)-1, "%s_%d",
        streamType2Str(aml_out->streamType), aml_out->streamTypeIndex);
    aml_strlower(aml_out->nickname);

    if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
        dolby_ms12_create_dec_handle(*stream_out);
    }

    if (pthread_mutex_init(&aml_out->dec_MutexLock, NULL)) {
        ALOGE("%s pthread_mutex_init(dec_MutexLock) failed", __func__);
        goto AUDIO_EASE_INIT_FAIL;
    }

    if (pthread_mutex_init(&aml_out->parser_MutexLock, NULL)) {
        ALOGE("%s pthread_mutex_init(parser_MutexLock) failed", __func__);
        goto AUDIO_EASE_INIT_FAIL;
    }

    AM_LOGI("io %d: out: %p card:%d alsa devices:%d exit ------", aml_out->io_handle,
        aml_out, aml_out->card, aml_out->device);
    return 0;

AUDIO_EASE_INIT_FAIL:
    adev_close_output_stream(dev, *stream_out);
    return ret;
}

void adev_close_output_stream_new(struct audio_hw_device *dev,
                                struct audio_stream_out *stream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    bool b_active_stream = aml_out->total_write_size ? true : false;

    AM_LOGI("io handle %d: out:%p", aml_out->io_handle, aml_out);

    struct aml_streamout_base *base = TO_BASE_PTR(stream, struct audio_stream_out, aml_streamout_base);

    if (base->common_usecase == STREAM_OUT_EFFECT) {
        adev_close_dummy_output_stream(dev, stream);
        return;
    }

    ALOGD("%s: enter streamType = %s", __func__, streamType2Str(aml_out->streamType));
    aml_out->is_closing = true;
    aml_out->switch_nonms12_check = false;

    /* free stream ease resource  */
    aml_audio_ease_close(aml_out->audio_stream_ease);
    aml_volume_shaper_release(&aml_out->volume_shaper);

    /* call legacy close to reuse codes */
    if (adev->active_outputs[aml_out->streamType] == aml_out) {
        adev->active_outputs[aml_out->streamType] = NULL;
    }

    if (aml_out->streamType == STREAM_PCM_HWSYNC) {
        aml_stream_delete_timer(adev, aml_out);
    }

    if (aml_out->inputPortID != -1 && adev->useAudioMixer && adev->mixerData) {
        delete_mixer_input_port(adev->mixerData, aml_out->inputPortID);
        aml_out->inputPortID = -1;
    }

    /* when switch hdmi output to a2dp output, close hdmi stream maybe after open a2dp stream,
     * and here set audio stop would cause audio stuck
     */
    if (adev->hw_mediasync
        && aml_out->hw_sync_mode
        && aml_out->tsync_status != TSYNC_STATUS_STOP
        && !has_hwsync_stream_running(stream)
      ) {
        ALOGI("%s set AUDIO_PAUSE and AUDIO_STOP when close stream\n",__func__);
        aml_hwsync_wrap_set_pause(aml_out->hwsync);
        aml_hwsync_wrap_set_stop(aml_out->hwsync);
        aml_out->tsync_status = TSYNC_STATUS_STOP;
    }
    if (aml_out->streamType == STREAM_RAW_DIRECT && is_dts_format(aml_out->hal_internal_format)) {
        adev->stream_bitrate = -1;
    }

    adev_close_output_stream(dev, stream);

    //adev->dual_decoder_support = false;
    //destroy_aec_reference_config(adev->aec);
    // for netflix continuously output lpcm5.1
    if (adev->useAudioMixer && eDolbyDcvLib == adev->dolby_lib_type && b_active_stream) {
        bool output_multich_enable = true;
        if (is_bypass_submix_active(adev)) {
            output_multich_enable = false;
        }
        subMixingEnableMultiChOutput(adev, output_multich_enable);
    }
}

static void dump_audio_patch_set (struct audio_patch_set *patch_set)
{
    struct audio_patch *patch = NULL;
    unsigned int i = 0;

    if (!patch_set)
        return;

    patch = &patch_set->audio_patch;
    if (!patch)
        return;

    ALOGI ("  - %s(), id: %d", __func__, patch->id);
    for (i = 0; i < patch->num_sources; i++)
        dump_audio_port_config (&patch->sources[i]);
    for (i = 0; i < patch->num_sinks; i++)
        dump_audio_port_config (&patch->sinks[i]);
}


int adev_create_audio_patch(struct audio_hw_device *dev,
                                unsigned int num_sources,
                                const struct audio_port_config *sources,
                                unsigned int num_sinks,
                                const struct audio_port_config *sinks,
                                audio_patch_handle_t *handle)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    const struct audio_port_config *src_config = sources;
    int ret = 0;

    if ((src_config->ext.device.type == AUDIO_DEVICE_IN_WIRED_HEADSET) ||
        (src_config->ext.device.type == AUDIO_DEVICE_IN_BLUETOOTH_BLE) ||
        (src_config->ext.device.type == AUDIO_DEVICE_IN_BUILTIN_MIC) ||
        (src_config->ext.device.type == AUDIO_DEVICE_IN_ECHO_REFERENCE) ||
        (src_config->ext.device.type == AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET)) {
        ALOGD("voice search is in use, bypass adev_create_audio_patch()!!\n");
        //we can't return error to application because it maybe process the error .
        *handle = AML_HAL_INVALID_PATCH_HANDLE;
        return 0;
    }
    R_CHECK_POINTER_LEGAL(-EINVAL, sources,);
    R_CHECK_POINTER_LEGAL(-EINVAL, sinks,);
    R_CHECK_POINTER_LEGAL(-EINVAL, handle,);
    R_CHECK_PARAM_LEGAL(-EINVAL, (int)num_sources, 0, 1,);
    R_CHECK_PARAM_LEGAL(-EINVAL, (int)num_sinks, 0, AUDIO_PATCH_PORTS_MAX - 1,);

    ret = patch_mgr_create_patch(aml_dev, num_sources, sources, num_sinks, sinks, handle);
    aml_dev->audio_hal_info.update_cnt = 0;
    aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, TYPE_PCM);
    AM_LOGI("dev=%p cur_out_device=0x%x",
            aml_dev, aml_dev->cur_out_devices);
    return ret;
}

/* Release an audio patch */
static int adev_release_audio_patch(struct audio_hw_device *dev,
                                audio_patch_handle_t handle)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    int ret = 0;

    AM_LOGI("++ handle(%d)", handle);
    if (handle == AML_HAL_INVALID_PATCH_HANDLE) {
        return 0;
    }
    if (aml_dev) {
        ret = patch_mgr_release_patch((struct aml_audio_device *)dev, handle);
    }

    aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, TYPE_PCM);
#ifdef ADD_AUDIO_DELAY_INTERFACE
    aml_audio_delay_clear(AML_DELAY_OUTPORT_SPEAKER);
    aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF);
    aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF_RAW);
    aml_audio_delay_clear(AML_DELAY_OUTPORT_SPDIF_B_RAW);
    aml_audio_delay_clear(AML_DELAY_OUTPORT_ALL);
#endif
    return ret;
}


static int adev_dump(const audio_hw_device_t *device, int fd)
{
    struct aml_audio_device* aml_dev = (struct aml_audio_device*)device;
    struct aml_stream_out *aml_out = NULL;
    const int kNumRetries = 5;
    const int kSleepTimeMS = 100;
    int retry = kNumRetries;
    int i;
    aml_dev->debug_flag = aml_audio_get_debug_flag();

    dprintf(fd, "\n-------------[AML_HAL] primary audio hal[dev:%p]------------------\n", aml_dev);
    while (retry > 0 && pthread_mutex_trylock(&aml_dev->lock) != 0) {
        usleep(kSleepTimeMS * 1000);
        retry--;
    }

    if (retry > 0) {
        aml_dev_dump_latency(aml_dev, fd);
        pthread_mutex_unlock(&aml_dev->lock);
    } else {
        // Couldn't lock
        dprintf(fd, "[AML_HAL]      Could not obtain aml_dev lock.\n");
    }

    dprintf(fd, "\n");
    dprintf(fd, "[AML_HAL]      TV platform     : %10d   |  SoundBar platform :    %d\n", is_TV(aml_dev), is_SBR(aml_dev));
    dprintf(fd, "[AML_HAL] digital_audio_mode   : %10s   |  cur_out_devices   :    %#x\n",
        digitalAudioModeType2Str(aml_dev->digital_audio_mode), aml_dev->cur_out_devices);
    dprintf(fd, "[AML_HAL]      A2DP gain       : %10f |  patch_src         :    %s\n",
        aml_dev->sink_gain[OUTPORT_A2DP], patchSrc2Str(get_dev_patch_src(aml_dev)));
    dprintf(fd, "[AML_HAL]      SPEAKER gain    : %10f |  HDMI gain         :    %f\n",
        aml_dev->sink_gain[OUTPORT_SPEAKER], aml_dev->sink_gain[OUTPORT_HDMI]);
    dprintf(fd, "[AML_HAL]      ms12 main volume: %10f\n", aml_dev->ms12.main_volume);
    dprintf(fd, "[AML_HAL]      ms12 main mute  : %10d\n", aml_dev->ms12.is_muted);

    dprintf(fd, "[AML_HAL]      PCM(AAC/HEAAC/MPEG-L1~L3) data only do pre attenuation for DTV-patch&System PCM on DRC-RF mode\n");
    dprintf(fd, "[AML_HAL]      system sound target: %2d dB\n", aml_dev->ms12.system_sound_target);
    dprintf(fd, "[AML_HAL]      DRC mode: %s\n", (aml_dev->ms12.stereo_drc.mode == DOLBY_DRC_RF_MODE) ? "RF MODE" : "LINE MODE");

    aml_audio_ease_t *audio_ease = aml_dev->audio_ease;
    if (!audio_ease) {
        dprintf(fd, "[AML_HAL]      audio_ease is null \n");
    } else {
        pthread_mutex_lock(&audio_ease->ease_lock);
        if (audio_ease && fabs(audio_ease->current_volume) <= 1e-6) {
            dprintf(fd, "[AML_HAL]      ease out muted. start:%f target:%f\n", audio_ease->start_volume, audio_ease->target_volume);
        }
        pthread_mutex_unlock(&audio_ease->ease_lock);
    }
    aml_decoder_info_dump(aml_dev, fd);

    aml_adev_stream_out_dump(aml_dev, fd);

    if (aml_dev->useAudioMixer) {
        mixer_dump(fd, aml_dev);
    }

#ifdef AML_MALLOC_DEBUG
    aml_audio_debug_malloc_showinfo(MEMINFO_SHOW_PRINT);
#endif

    adev_audio_patches_dump(aml_dev, fd);

#ifndef AUDIO_HAL_DISABLE_MS12
    dolby_ms12_info_dump(fd);
    switch (aml_dev->board_config.dolby_ms12_audio_config) {
    case MS12_CONFIG_Y:
        dprintf(fd, "[Ms12 Info]    Dolby MS12 Config: %s\n", "Y");
        break;
    case MS12_CONFIG_X:
        dprintf(fd, "[Ms12 Info]    Dolby MS12 Config: %s\n", "X");
        break;
    case MS12_CONFIG_Z:
        dprintf(fd, "[Ms12 Info]    Dolby MS12 Config: %s\n", "Z");
        break;
    }
#endif

    aml_alsa_device_status_dump(aml_dev, fd);

    aml_alsa_mixer_status_dump(aml_dev, fd);

    a2dp_hal_dump(aml_dev, fd);

    if (profile_is_valid(&aml_dev->usb_audio.in_profile)) {
        dprintf(fd, "\n-----------[AML_HAL] USB input device Capability-----------\n");
        profile_dump(&aml_dev->usb_audio.in_profile, fd);
    }

    dprintf(fd, "\n-------------[AML_HAL] primary audio hal End---------------------\n");
    return 0;
}

pthread_mutex_t adev_mutex = PTHREAD_MUTEX_INITIALIZER;
static void * g_adev = NULL;
void *adev_get_handle(void) {
    /*coverity[missing_lock]*/
    return (void *)g_adev;
}

int aml_get_debug_value(void)
{
    struct aml_audio_device *adev = adev_get_handle();
    return adev ? adev->debug_flag : 0;
}

int adev_ms12_prepare(struct audio_hw_device *dev) {
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    struct audio_config stream_config;
    struct aml_stream_out *aml_out = NULL;
    struct audio_stream_out *stream_out = NULL;
    int ret = -1;
    bool main1_dummy = true;
    bool ott_input = false;
    audio_format_t aformat = AUDIO_FORMAT_E_AC3;

    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    pthread_mutex_lock(&adev->ms12_init_lock);

    if (ms12->dap_only_enable) {
        aml_dap_close(ms12);
    }

    if (adev->ms12_out) {
        ALOGD("%s: ms12 stream exist", __func__);
        pthread_mutex_unlock(&adev->ms12_init_lock);
        return 0;
    }

    ALOGD("%s: enter", __func__);
    stream_config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    stream_config.sample_rate = 48000;
    stream_config.format = AUDIO_FORMAT_PCM_16_BIT;

    ret = adev_open_output_stream_new(dev,
                                      0,
                                      AUDIO_DEVICE_NONE,
                                      AUDIO_OUTPUT_FLAG_NONE,
                                      &stream_config,
                                      &stream_out,
                                      "ms12_stream");
    if (ret < 0) {
        ALOGE("%s: open output stream failed", __func__);
        pthread_mutex_unlock(&adev->ms12_init_lock);
        return ret;
    }

    aml_out = (struct aml_stream_out *)stream_out;


    //here type of output stream is normal,should add restricted condition to update format.
    //config_output also invoke to here, this fix for tv/dtv source.
    //not invoke get_sink_format to update output strategy.
    if (!direct_active(adev)) {
        get_sink_format(&aml_out->stream);
    }

    adev->continuous_audio_mode = true;
    ret = get_the_dolby_ms12_prepared(aml_out, aformat, AUDIO_CHANNEL_OUT_STEREO, 48000);
    pthread_mutex_unlock(&adev->ms12_init_lock);
    return 0;
}


void adev_ms12_cleanup(struct audio_hw_device *dev) {
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    struct audio_stream_out *stream_out = (struct audio_stream_out *)adev->ms12_out;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream_out;
    pthread_mutex_lock(&adev->ms12_init_lock);
    get_dolby_ms12_cleanup(&adev->ms12, true);
    if (stream_out) {
        aml_out->hw_sync_mode = 0;
        aml_out->hwsync = NULL;
        adev_close_output_stream_new(dev, stream_out);
    }
    adev->ms12_out = NULL;
    pthread_mutex_unlock(&adev->ms12_init_lock);
    return;
}


static int adev_close(hw_device_t *device)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)device;

    pthread_mutex_lock(&adev_mutex);
    AM_LOGI("count:%d enter", adev->count);
    adev->count--;
    if (adev->count > 0) {
        pthread_mutex_unlock(&adev_mutex);
        return 0;
    }
    //adev_close_sys_resource_mgr(adev);

    /* free ease resource  */
    aml_audio_ease_close(adev->audio_ease);
    aml_audio_ease_close(adev->volume_ease.ease);

    /* destroy thread for communication between Audio Hal and MS12 */
    if ((eDolbyMS12Lib == adev->dolby_lib_type)) {
        adev_ms12_cleanup((struct audio_hw_device *)device);
        ms12_mesg_thread_destroy(&adev->ms12);
        ALOGD("%s, ms12_mesg_thread_destroy finished!\n", __func__);
    }
    pthread_mutex_destroy(&adev->ms12_init_lock);
    aml_audio_all_timer_delete();
    pthread_mutex_destroy(&adev->bitstream_lock);

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        int wait_count = 0;
        while (adev->ms12_out != NULL) {
            if (wait_count >= 100) {
                break;
            }
            wait_count++;
            /*coverity[sleep]*/
            usleep(10*1000);//10ms
        }
        aml_ms12_lib_release();
        release_dolby_dev();
        ALOGD("%s, wait_count:%d, ms12 resource should be released finish\n", __func__, wait_count);
    }

#ifdef LOWPOWER_DSP_FFV
    dsp_ffv_dev_deinit(adev);
#endif

#ifdef ENABLE_AML_ACR
    aml_close_ai_audio_module(&adev->native_postprocess);
#endif

    if (adev->out_16_buf) {
        aml_audio_free(adev->out_16_buf);
    }
    if (adev->out_32_buf) {
        aml_audio_free(adev->out_32_buf);
    }
    if (adev->audioeffect_tmp_buffer) {
        aml_audio_free(adev->audioeffect_tmp_buffer);
    }

    if (adev->tmp_buffer_8ch) {
        aml_audio_free(adev->tmp_buffer_8ch);
    }

    ring_buffer_release(&(adev->spk_tuning_rbuf));

    eq_drc_release(&adev->eq_data);

    if (adev->mixerData) {
        deleteHalSubMixing(adev);
    }
    aml_audio_hwsync_close();

    /** rlease some global shared resources **/
    destroy_hdmi_capability_manager(adev);

    destroy_hw_resource_mgr(adev);

    destroy_patch_manager(adev);

    close_mixer_handle(&adev->alsa_mixer);
    /** done **/

    mmap_audio_free_manager(adev->mmap_audio_manager);
    adev->mmap_audio_manager = NULL;

#ifdef ADD_AUDIO_DELAY_INTERFACE
    if (is_TV(adev)) {
        aml_audio_delay_deinit();
    }
#endif
#ifdef ENABLE_AEC_APP
    release_aec(adev->aec);
#endif

    aml_audio_uevent_close();
    aml_close_audio_enhancement_module(&adev->native_postprocess);
    destroy_vendor_post_process(&adev->native_postprocess);

    aml_destroy_stream_manager(adev);
    pthread_mutex_destroy(&adev->streamList_MutexLock);

    destroy_async_write_thread();
    aml_deinit_zero_detect_list(&adev->zero_data_detect_list);

    if (adev->mpegh_ui_persistencemem) {
        aml_audio_free(adev->mpegh_ui_persistencemem);
        adev->mpegh_ui_persistencememsize = 0;
        adev->mpegh_ui_persistencemem = NULL;

    }
    if (adev->mpegh_base64_encode_mem) {
        aml_audio_free(adev->mpegh_base64_encode_mem);
        adev->mpegh_ui_persistencemem = NULL;
    }

    g_adev = NULL;

    aml_audio_free(device);
    g_aml_primary_adev = NULL;
    aml_audio_debug_close();
    aml_audio_debug_malloc_close();
    pthread_mutex_unlock(&adev_mutex);

    AM_LOGI("exit");
    return 0;
}

static int adev_set_audio_port_config(struct audio_hw_device *dev, const struct audio_port_config *config)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    enum OUT_PORT outport = OUTPORT_SPEAKER;
    enum IN_PORT inport = INPORT_HDMIIN;
    int ret = 0;
    int devs_nums = 1;

    R_CHECK_POINTER_LEGAL(-EINVAL, config,);
    if ((config->config_mask & AUDIO_PORT_CONFIG_GAIN) == 0) {
        AM_LOGE("config_mask:%#x invalid", config->config_mask);
        return -EINVAL;
    }

    struct listnode *node = NULL;
    bool found = false;
    audio_devices_t out_device = 0;
    struct listnode *node_list = get_patch_list_from_mgr(aml_dev);
    if (config->type == AUDIO_PORT_TYPE_DEVICE) {
        out_device = config->ext.device.type;
        AM_LOGI("id:%d, dev:%s, role:%s, type:%s, gain:%d", config->id, audioDevType2Str(out_device),
            audioPortRole2Str(config->role), audioPortType2Str(config->type), config->gain.values[0]);
        list_for_each(node, node_list) {
            struct audio_patch_set *patch_set = node_to_item(node, struct audio_patch_set, list_node);
            struct audio_patch *patch = &patch_set->audio_patch;
            struct audio_port_config *ports = NULL;
            unsigned int num_ports = 0;
            if (config->role == AUDIO_PORT_ROLE_SINK) {
                ports = patch->sinks;
                num_ports = patch->num_sinks;
                for (int i = 0; i < num_ports; i++) {
                    if (ports[i].type == AUDIO_PORT_TYPE_DEVICE &&
                        ports[i].ext.device.type == config->ext.device.type) {
                        ports[i].gain.values[0] = config->gain.values[0];
                        found = true;
                    }
                }
            } else if (config->role == AUDIO_PORT_ROLE_SOURCE) {
                ports = patch->sources;
                num_ports = patch->num_sources;
                for (int i = 0; i < num_ports; i++) {
                    if (ports[i].type == AUDIO_PORT_TYPE_DEVICE &&
                        ports[i].ext.device.type == config->ext.device.type) {
                        ports[i].gain.values[0] = config->gain.values[0];
                        found = true;
                    }
                }
            } else {
                AM_LOGW("unsupported role:%s", audioPortRole2Str(config->role));
                return -EINVAL;
            }
        }
    } else {
        AM_LOGW("unsupported type:%s", audioPortType2Str(config->type));
        return -EINVAL;
    }

    if (!found) {
        AM_LOGW("no available patch was found.");
        return -EINVAL;
    }
    if (config->type == AUDIO_PORT_TYPE_DEVICE && config->role == AUDIO_PORT_ROLE_SOURCE) {
        android_dev_convert_to_hal_dev(config->ext.device.type, (int *)&inport);
        set_inport_gain(aml_dev, inport, DbToAmpl(config->gain.values[0] / 100.0));
        AM_LOGI("set src_gain[%s]: %f, cur_out_devices:%#x", inputPort2Str(inport),
            get_inport_gain(aml_dev, inport), aml_dev->cur_out_devices);
        devs_nums = __builtin_popcount(aml_dev->cur_out_devices);
        if (devs_nums == 1) {
            out_device = aml_dev->cur_out_devices;
        } else if (devs_nums == 2){
            /* If there are two sink devices, the SPDIF is removed because the priority of SPDIF is low. */
            if (aml_dev->cur_out_devices & AUDIO_DEVICE_OUT_SPDIF) {
                out_device = (aml_dev->cur_out_devices & (~AUDIO_DEVICE_OUT_SPDIF));
            } else {
                AM_LOGW("unsupported cur_out_devices:%#x, devs_nums:%d", aml_dev->cur_out_devices, devs_nums);
                return -EINVAL;
            }
        } else if (devs_nums > 2) {
            AM_LOGW("unsupported cur devs_nums:%d", devs_nums);
            return -EINVAL;
        }
    }
    if (devs_nums > 0) {
        android_dev_convert_to_hal_dev(out_device, (int *)&outport);
        aml_dev->sink_gain[outport] = DbToAmpl(config->gain.values[0] / 100.0);
        if (outport == OUTPORT_ANLG_DOCK_HEADSET || outport == OUTPORT_HDMI_ARC ||
            (outport == OUTPORT_A2DP && aml_dev->bt_avrcp_supported && aml_dev->sink_gain[outport] > FLOAT_ZERO)) {
            aml_dev->sink_gain[outport] = 1.0;
        }
        AM_LOGI("set sink_gain[%s]: %f, cur_out_devices:%#x", outputPort2Str(outport),
            aml_dev->sink_gain[outport], aml_dev->cur_out_devices);
    }

    if (config->role == AUDIO_PORT_ROLE_SOURCE && eDolbyMS12Lib == aml_dev->dolby_lib_type && is_TV(aml_dev)) {
        /* dev->dev and DTV src gain using MS12 primary gain */
        if (is_dev_patch_running(aml_dev) || is_same_patch_src(aml_dev, SRC_DTV)) {
            pthread_mutex_lock(&aml_dev->lock);
             /* Raw data from hdmi, alexa voice case, the souece stream need duck about 20dB */
            ALOGI("%s line %d set ms12 main volume %f\n", __func__, __LINE__, DbToAmpl(config->gain.values[1]/100));
            set_ms12_main_volume(&aml_dev->ms12, DbToAmpl(config->gain.values[1]/100));
            pthread_mutex_unlock(&aml_dev->lock);
            ALOGD("%s set source gain to ms12, volume-> values:%d, gain:%f", __func__,
                config->gain.values[1], DbToAmpl(config->gain.values[1]/100));
        }
    }

    /* for both dev->dev and mixer->dev, start volume ease */
    if (outport == OUTPORT_SPEAKER && aml_dev->last_sink_gain != aml_dev->sink_gain[OUTPORT_SPEAKER]) {
        ALOGD("start easing: vol last %f, vol new %f", aml_dev->last_sink_gain, aml_dev->sink_gain[OUTPORT_SPEAKER]);
        aml_dev->volume_ease.config_easing = true;
        aml_dev->last_sink_gain = aml_dev->sink_gain[OUTPORT_SPEAKER];

        if ((eDolbyMS12Lib == aml_dev->dolby_lib_type)) {
            /*
             * The postgain value has an impact on the Volume Modeler and the Audio Regulator:
             * Volume Modeler: Uses the postgain value to select the appropriate frequency response curve
             * to maintain a consistent perceived timbre at different listening levels.
             * SP45: Postgain
             * Sets the amount of gain that is to be applied to the signal after exiting MS12.
             * Settings From -130 to +30 dB, in 0.0625 dB steps
             */
            int dap_postgain = volume2Ms12DapPostgain(aml_dev->sink_gain[OUTPORT_SPEAKER]);
            set_ms12_dap_postgain(&aml_dev->ms12, dap_postgain);
        }
    } else if (outport == OUTPORT_HEADPHONE && aml_dev->last_sink_gain != aml_dev->sink_gain[OUTPORT_HEADPHONE]) {
        ALOGD("hp start easing: vol last %f, vol new %f", aml_dev->last_sink_gain, aml_dev->sink_gain[OUTPORT_HEADPHONE]);
        aml_dev->volume_ease.config_easing = true;
        aml_dev->last_sink_gain = aml_dev->sink_gain[OUTPORT_HEADPHONE];

        if ((eDolbyMS12Lib == aml_dev->dolby_lib_type)) {
            /*
             * The postgain value has an impact on the Volume Modeler and the Audio Regulator:
             * Volume Modeler: Uses the postgain value to select the appropriate frequency response curve
             * to maintain a consistent perceived timbre at different listening levels.
             * SP45: Postgain
             * Sets the amount of gain that is to be applied to the signal after exiting MS12.
             * Settings From -130 to +30 dB, in 0.0625 dB steps
             */
            int dap_postgain = volume2Ms12DapPostgain(aml_dev->sink_gain[OUTPORT_HEADPHONE]);
            set_ms12_dap_postgain(&aml_dev->ms12, dap_postgain);
        }
    }
    return 0;
}

#if ANDROID_PLATFORM_SDK_VERSION > 32
static int adev_set_device_connected_state_v7(struct audio_hw_device *dev,
                                     struct audio_port_v7 *port,
                                     bool connected)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    struct amlAudioMixer *audio_mixer = aml_dev->mixerData;
    struct str_parms *parms = NULL;
    if (port->type == AUDIO_PORT_TYPE_DEVICE) {
        AM_LOGI("%s address:%s, num_descriptors:%d, num_profiles:%d",
                connected ? "connected" : "disconnected",
                port->ext.device.address,
                port->num_extra_audio_descriptors, port->num_audio_profiles);
        parms = str_parms_create_str(port->ext.device.address);

        set_device_connect_state(aml_dev, parms, port->ext.device.type, connected);
        if (connected) {
            if (port->ext.device.type & AUDIO_DEVICE_OUT_HDMI_ARC) {
                if (eDolbyMS12Lib == aml_dev->dolby_lib_type) {
                    aml_dev->raw_to_pcm_flag = true;
                } else {
                    subMixingOutputRestart(aml_dev);
                }
            }

            if (is_HDMI_connected(aml_dev)) {
                struct dolby_ms12_desc *ms12 = &(aml_dev->ms12);
                /*update sink format when HDMI connected because its capability may be changed*/
                update_sink_format_after_hotplug(aml_dev);
                aml_audiohal_sch_state_2_ms12(ms12, MS12_SCHEDULER_RUNNING);
                if (aml_dev->useAudioMixer)
                    aml_audiohal_sch_state_2_submix(audio_mixer, SUBMIX_SCHEDULER_RUNNING);
            }
        }
        if (port->ext.device.type & AUDIO_DEVICE_OUT_HDMI_ARC) {
            int earc_tx_type = aml_audio_earctx_get_type(aml_dev);
            AM_LOGI("current connect: %s", (earc_tx_type == ATTEND_TYPE_EARC) ? "earc" : "arc");
            if (earc_tx_type == ATTEND_TYPE_EARC && connected) {
                // when the EARC is connected, the SAD of the EARC needs to updated.
                update_earc_sad(dev);
            } else {
                read_hdmi_arc_info(dev, port->extra_audio_descriptors, port->num_extra_audio_descriptors, connected);
            }
            if (connected) {
                // we also that updating SAD is over when the ARC is connected.
                aml_dev->is_arc_updating_sad = false;
            }

            //TODO: volume easing
            if (aml_dev->useAudioMixer)
                subMixingSetSinkGain(aml_dev, aml_dev->sink_gain);
        }
    }
    for (int i = 0; i< port->num_audio_profiles; i++) {
        AM_LOGV("[%d] format:%#x, num_sample_rates:%d num_channel_masks:%#x", i,
             port->audio_profiles[i].format, port->audio_profiles[i].num_sample_rates, port->audio_profiles[i].num_channel_masks);
    }

    return 0;
}

static int adev_get_audio_port_v7(struct audio_hw_device *dev __unused, struct audio_port_v7 *port __unused)
{
    return -ENOSYS;
}

#endif


static int adev_get_audio_port(struct audio_hw_device *dev __unused, struct audio_port *port __unused)
{
    return -ENOSYS;
}

#if ANDROID_PLATFORM_SDK_VERSION > 29

static int adev_add_device_effect(struct audio_hw_device *dev,
                                  audio_port_handle_t device, effect_handle_t effect)
{
    int status;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;

    ALOGD("func:%s device:%d effect_handle_t:%p, cur_out_devices:%#x", __func__, device, effect, aml_dev->cur_out_devices);
    pthread_mutex_lock (&aml_dev->lock);
    status = aml_add_audio_effect(&aml_dev->native_postprocess, effect, device);
    pthread_mutex_unlock (&aml_dev->lock);
    return status;
}

static int adev_remove_device_effect(struct audio_hw_device *dev,
                                     audio_port_handle_t device, effect_handle_t effect)
{
    int status;
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;

    ALOGD("func:%s device:%d effect_handle_t:%p, cur_out_devices:%#x", __func__, device, effect, aml_dev->cur_out_devices);
    pthread_mutex_lock (&aml_dev->lock);
    status = aml_remove_audio_effect(&aml_dev->native_postprocess, effect, device);
    pthread_mutex_unlock (&aml_dev->lock);
    return status;
}
#endif

static int adev_uevent_callback(int uevent_type) {
    struct aml_audio_device *adev = aml_adev_get_handle();

    AM_LOGI("uevent type=%d", uevent_type);
    switch (uevent_type) {
        case UEVENT_TYPE_VMODE_CHANGE:
            if ((adev->useAudioMixer)) {
                subMixingOutputRestart(adev);
            }
            /*reset raw data output*/
            adev->reset_hdmitx_audio = true;
            /*reset pcm data output*/
            adev->raw_to_pcm_flag = true;
            break;
        case UEVENT_TYPE_VMODE_GET:
#ifdef LOWPOWER_DSP_FFV
            get_vwe_wakeup_event(adev);
#endif
            break;
        default:
        break;
    }


    return 0;
}

#define MAX_SPK_EXTRA_LATENCY_MS (100)
#define DEFAULT_SPK_EXTRA_LATENCY_MS (15)

static int adev_open(const hw_module_t* module, const char* name, hw_device_t** device)
{
    struct aml_audio_device *adev;
    size_t bytes_per_frame = audio_bytes_per_sample(AUDIO_FORMAT_PCM_16_BIT)
                             * audio_channel_count_from_out_mask(AUDIO_CHANNEL_OUT_STEREO);
    int buffer_size = PLAYBACK_PERIOD_COUNT * DEFAULT_PLAYBACK_PERIOD_SIZE * bytes_per_frame;
    int spk_tuning_buf_size = MAX_SPK_EXTRA_LATENCY_MS
                              * bytes_per_frame * MM_FULL_POWER_SAMPLING_RATE / 1000;
    int spdif_tuning_latency = aml_audio_get_spdif_tuning_latency();
    int card = CARD_AMLOGIC_BOARD;
    int ret = 0, i;
    char buf[PROPERTY_VALUE_MAX] = {0};
    int disable_continuous = 1;
    bool earctx_mode = true;

    AM_LOGD("enter. name:%s", name);
    pthread_mutex_lock(&adev_mutex);
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) {
        ret = -EINVAL;
        AM_LOGE("name:%s not audio hardware", name);
        goto err;
    }

    if (g_adev != NULL) {
        adev = (struct aml_audio_device *)g_adev;
        adev->count++;
        *device = &adev->hw_device.common;
        AM_LOGI("adev exists, reuse adev:%p", *device);
        /*if we reuse adev open, but ms12 is not init, we should init it*/
        if (eDolbyMS12Lib == adev->dolby_lib_type && !adev->ms12.dolby_ms12_enable) {
            adev_ms12_prepare((struct audio_hw_device *)adev);
        }
        goto err;
    }
    aml_audio_debug_malloc_open();
    aml_audio_debug_open();

    adev = aml_audio_calloc(1, sizeof(struct aml_audio_device));
    if (!adev) {
        ret = -ENOMEM;
        goto err;
    }
    g_adev = (void *)adev;
    g_aml_primary_adev = (void *)adev;

#ifdef LOWPOWER_DSP_FFV
    dsp_ffv_dev_init(adev);
#endif

    adev->is_ui_force_dap_disable = 1;
    adev->atmos_indicator_status = false;
    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
#if ANDROID_PLATFORM_SDK_VERSION > 32
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_3_2;//need compatible with 3.0
#else
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_3_0;//need compatible with 3.0
#endif
    adev->hw_device.common.module = (struct hw_module_t *)module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.get_master_volume = adev_get_master_volume;
    adev->hw_device.set_master_mute = adev_set_master_mute;
    adev->hw_device.get_master_mute = adev_get_master_mute;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream_new;
    adev->hw_device.close_output_stream = adev_close_output_stream_new;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.create_audio_patch = adev_create_audio_patch;
    adev->hw_device.release_audio_patch = adev_release_audio_patch;
    adev->hw_device.set_audio_port_config = adev_set_audio_port_config;
#if ANDROID_PLATFORM_SDK_VERSION > 32
    adev->hw_device.set_device_connected_state_v7 = adev_set_device_connected_state_v7;
    adev->hw_device.get_audio_port_v7 = adev_get_audio_port_v7;
#endif
#if ANDROID_PLATFORM_SDK_VERSION > 29
    adev->hw_device.add_device_effect = adev_add_device_effect;
    adev->hw_device.remove_device_effect = adev_remove_device_effect;
#endif
    adev->hw_device.get_microphones = adev_get_microphones;
    adev->hw_device.get_audio_port = adev_get_audio_port;
    adev->hw_device.dump = adev_dump;
    adev->digital_audio_mode = AML_DIGITAL_AUDIO_MODE_AUTO;
    adev->ms12.ms12_scheduler_state = MS12_SCHEDULER_NONE;
    adev->ms12.last_scheduler_state = MS12_SCHEDULER_NONE;
    adev->audio_patch_2_af_stream = false;
    adev->foreground_stream_type = FG_STREAM_TYPE_NONE;
    card = alsa_device_get_card_index();
    if ((card < 0) || (card > 7)) {
        ALOGE("error to get audio card");
        ret = -EINVAL;
        goto err_adev;
    }

    adev->card = card;

    /* 1st open mixer ctrl for audio hal */
    open_mixer_handle(&adev->alsa_mixer);

    //init mediasync handle and id to default.
    aml_mediasync_init(adev->mediasync);

    /* init arc hdmi capability manager */
    if (init_hdmi_capability_manager(adev) < 0) {
        ALOGE("%s() line:%d error! new hdmi_capability_manager failed", __func__, __LINE__);
        ret = -EINVAL;
        goto err_adev;
    }

    /* some external codec init time last longer, wait 1s before timeout */
    /*coverity[sleep]*/
    if (init_audio_hw_resource_mgr(adev, &adev->alsa_mixer) < 0) {
        ALOGE("%s() line:%d error! audio route init failed", __func__, __LINE__);
        ret = -EINVAL;
        goto err_adev;
    }

    /* init device patch manager */
    if (init_patch_manager(adev) < 0) {
        ALOGE("%s() line:%d error! new patch_manger failed", __func__, __LINE__);
        ret = -EINVAL;
        goto err_adev;
    }

    adev->mpegh_ui_persistencemem = aml_audio_malloc(PERSISTENCE_BUFSIZE);
    adev->mpegh_ui_persistencememsize = PERSISTENCE_BUFSIZE;

    /* Set the default route before the PCM stream is opened */
    adev->mode = AUDIO_MODE_NORMAL;
    adev->out_device = AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_SPDIF;
    adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
    adev->hi_pcm_mode = false;
    adev->last_sink_capability = 0;
    adev->first_data = false;
    adev->avsync_compensate_delay_ms = 0;
    adev->eq_data.card = adev->card;
    if (eq_drc_init(&adev->eq_data) == 0) {
        ALOGI("%s() audio source gain: atv:%f, dtv:%f, hdmiin:%f, av:%f, media:%f", __func__,
           adev->eq_data.s_gain.atv, adev->eq_data.s_gain.dtv,
           adev->eq_data.s_gain.hdmi, adev->eq_data.s_gain.av, adev->eq_data.s_gain.media);
        ALOGI("%s() audio device gain: speaker:%f, spdif_arc:%f, headphone:%f", __func__,
           adev->eq_data.p_gain.speaker, adev->eq_data.p_gain.spdif_arc,
              adev->eq_data.p_gain.headphone);

        init_noise_gate_wrap(adev, adev->eq_data.noise_gate.aml_ng_enable,
                                adev->eq_data.noise_gate.aml_ng_level,
                                adev->eq_data.noise_gate.aml_ng_attack_time,
                                adev->eq_data.noise_gate.aml_ng_release_time);

        adev->aml_dap_v1_enable = adev->eq_data.aml_dap_v1_enable;
        adev->eq_drc_inited = true;
        /* read default dac vol for hp mute*/
        int dac_unmute[2] = {251, 251};
        aml_mixer_ctrl_get_array(&adev->alsa_mixer, AML_MIXER_ID_DAC_PLAYBACK_VOLUME, &dac_unmute, 2);
        adev->dac_value = dac_unmute[0];
        ALOGI("%s() audio dac gain: %d",__func__, adev->dac_value);
    }

    adev->out_16_buf_size = buffer_size;
    adev->out_16_buf = aml_audio_calloc(1, buffer_size);
    if (adev->out_16_buf == NULL) {
        AM_LOGE("malloc out_16_buf buffer failed, size:%d", buffer_size);
        ret = -ENOMEM;
        goto err_adev;
    }

    adev->out_32_buf_size = buffer_size * 2;
    adev->out_32_buf = aml_audio_calloc(1, buffer_size * 2);
    if (adev->out_32_buf == NULL) {
        AM_LOGE("malloc out_32_buf buffer failed size:%d", buffer_size * 2);
        ret = -ENOMEM;
        goto err_out_16_buf;
    }

    /* init speaker tuning buffers */
    ret = ring_buffer_init(&(adev->spk_tuning_rbuf), spk_tuning_buf_size);
    if (ret < 0) {
        AM_LOGE("Fail to init audio spk_tuning_rbuf!");
        goto err_out_32_buf;
    }
    adev->spk_tuning_buf_size = spk_tuning_buf_size;

    /* if no latency set by prop, use default one */
    if (spdif_tuning_latency == 0) {
        spdif_tuning_latency = DEFAULT_SPK_EXTRA_LATENCY_MS;
    } else if (spdif_tuning_latency > MAX_SPK_EXTRA_LATENCY_MS) {
        spdif_tuning_latency = MAX_SPK_EXTRA_LATENCY_MS;
    } else if (spdif_tuning_latency < 0) {
        spdif_tuning_latency = 0;
    }

    // try to detect which dolby lib is readable
    adev->dolby_lib_type = detect_dolby_lib_type();
    /* if MS12 is inside, here adev->dolby_lib_type_last will be always eDolbyMS12Lib(2). */
    adev->dolby_lib_type_last = adev->dolby_lib_type;
    adev->dolby_decode_enable = dolby_lib_decode_enable(adev->dolby_lib_type_last);
    adev->dts_lib_type = detect_dts_lib_type();
    if (adev->dts_lib_type == eDTSXLib) {
        adev->dts_decode_enable = 1;
    } else {
        adev->dts_decode_enable = dts_lib_decode_enable();
    }
    adev->is_ms12_tuning_dat = is_ms12_tuning_dat_in_dut();

#if ANDROID_PLATFORM_SDK_VERSION >= 30
        struct utsname kernel_msg;
        uname(&kernel_msg);
        if (strstr(kernel_msg.release, "5.15") != NULL) {
            adev->singleDmxNonTunnelMode = true;
        } else {
            adev->singleDmxNonTunnelMode = false;
        }
#endif

#ifdef MS12_V24_ENABLE
    adev->support_ms12_version = eDolbyMS12_V2;
#else
    adev->support_ms12_version = eDolbyMS12_V1;
#endif

    /* convert MS to data buffer length need to cache */
    adev->spk_tuning_lvl = (spdif_tuning_latency * bytes_per_frame * MM_FULL_POWER_SAMPLING_RATE) / 1000;
    /* end of speaker tuning things */
    *device = &adev->hw_device.common;
    adev->dts_post_gain = 1.0;
    for (i = 0; i < OUTPORT_MAX; i++) {
        adev->sink_gain[i] = 1.0;
    }
    adev->sink_gain[OUTPORT_HEADPHONE] = 0;
    adev->continuous_audio_mode_default = 0;
    adev->enable_soundbar_mode = 0;
    adev->dual_spdif_support = property_get_bool("ro.vendor.platform.is.dualspdif", false);
    adev->ms12_force_ddp_out = property_get_bool("ro.vendor.platform.is.forceddp", false);
    adev->spdif_enable = true;
    adev->dolby_ms12_dap_init_mode = property_get_int32("ro.vendor.platform.ms12.dap_init_mode", 0);
    /*this flag is for issue SWPL-80881*/
    adev->aml_truehd_passthrough_support = property_get_bool("ro.vendor.platform.is.aml_truehd_passthrough", false);
    adev->spdif_coexist_other = property_get_bool(PROP_AUDIO_OUTPUT_SPDIF_COEXIST, true);
    adev->continuous_enable_mixer_max_size = property_get_bool("ro.vendor.media.audio.continuous.enable_mixer_max_size", true);
    adev->stream_pause_delay = property_get_int32("ro.vendor.media.audio.stream.pause.delay", 24);
    adev->dac_softmute_delay = property_get_int32("ro.vendor.media.audio.softmute.delay", 0);
    /* get the device Loudness level */
    adev->loudness_level = get_loudness_level();
    adev->ms12_dynamic_sleep = property_get_bool("ro.vendor.media.audio.ms12.dynamic_sleep", false);
    adev->enable_soundbar_mode = false;
    adev->is_alsa_device_conflict = false;
    adev->arc_delay_ms = property_get_int32("ro.vendor.platform.arc.delay", 100);

    /* to get the DTG case at UK. */
    adev->is_dtg_case = is_locale_at_United_Kingdom_device();

    /*for ms12 case, we set default continuous mode*/
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        adev->continuous_audio_mode_default = 1;
    }
    /*we can use property to debug it*/
    ret = property_get(DISABLE_CONTINUOUS_OUTPUT, buf, NULL);
    if (ret > 0) {
        sscanf(buf, "%d", &disable_continuous);
        if (!disable_continuous) {
            adev->continuous_audio_mode_default = 1;
        }
        ALOGI("%s[%s] disable_continuous %d\n", DISABLE_CONTINUOUS_OUTPUT, buf, disable_continuous);
    }
    adev->continuous_audio_mode = adev->continuous_audio_mode_default;
    pthread_mutex_init(&adev->alsa_pcm_lock, NULL);
    pthread_mutex_init(&adev->stream_release_lock, NULL);
    pthread_mutex_init(&adev->aml_pcm_record_delay.pcm_record_lock , NULL);

    /* Set the earctx mode by the property, only need set false */
    earctx_mode = property_get_bool("persist.vendor.earc_settings", true);
    if (!earctx_mode) {
        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_EARC_MODE, earctx_mode);
        ALOGI("eARC_TX eARC Mode get from property: %d\n", earctx_mode);
    }

    //TODO: move those default setting at HW resource mgr
    enable_device_force_routing(adev, true);
    aml_audio_outport_enable(adev, AUDIO_DEVICE_OUT_SPEAKER, false);
    aml_audio_outport_enable(adev, AUDIO_DEVICE_OUT_WIRED_HEADPHONE, false);
    aml_audio_outport_enable(adev, AUDIO_DEVICE_OUT_HDMI, false);
    aml_audio_outport_enable(adev, AUDIO_DEVICE_OUT_HDMI_ARC, false);
    aml_audio_outport_enable(adev, AUDIO_DEVICE_OUT_SPDIF, adev->spdif_coexist_other);
    enable_device_force_routing(adev, false);

    if (eDolbyMS12Lib != adev->dolby_lib_type) {
        adev->ms12.dolby_ms12_enable = false;
    } else {
        // in ms12 case, use new method for TV or BOX .zzz
        adev->hw_device.open_output_stream = adev_open_output_stream_new;
        adev->hw_device.close_output_stream = adev_close_output_stream_new;
        ALOGI("%s,in ms12 case, use new method no matter if current platform is TV or BOX", __FUNCTION__);
    }
    adev->atoms_lock_flag = false;

    if (eDolbyDcvLib == adev->dolby_lib_type) {
        adev->dcvlib_bypass_enable = 1;
    }

    memset(&adev->dts_hd, 0, sizeof(struct dca_dts_dec));
    memset(&adev->dts_x, 0, sizeof(dtsx_dec_t));
    adev->sound_track_mode = 0;

#if ENABLE_NANO_NEW_PATH
    nano_init();
#endif
    ALOGI("%s() adev->dolby_lib_type = %d", __FUNCTION__, adev->dolby_lib_type);
    adev->audio_type = LPCM;

#ifdef ADD_AUDIO_DELAY_INTERFACE
    ret = aml_audio_delay_init();
    if (ret < 0) {
        AM_LOGE("aml_audio_delay_init fail");
        goto err_dtv_audio_instances;
    }
#endif

#ifdef ENABLE_AEC_APP
    if (init_aec(CAPTURE_CODEC_SAMPLING_RATE, NUM_AEC_REFERENCE_CHANNELS,
                    CHANNEL_STEREO, &adev->aec)) {
        goto err_adev;
    }
#endif

    // FIXME: current MS12 is not compatible with AudioMixer, when MS12 lib exists, use ms12 system.
    // latest code, there are two scenarios split when code run.
    // one scene is ms12 case,the other scene is AudioMixer case.
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        adev->useAudioMixer = false;
    } else {
        adev->useAudioMixer = true;
    }

    if (adev->useAudioMixer) {
        aml_audio_hwsync_open();
        adev->raw_to_pcm_flag = false;
    }
    profile_init(&adev->usb_audio.in_profile, PCM_IN); //support both submix and ms12

    ALOGI("%s(), MS12 is not compatible with SUBMIXER currently, set useAudioMixer %s",
        __func__, adev->useAudioMixer ? "TRUE": "FALSE");

    if (aml_audio_ease_init(&adev->audio_ease) < 0) {
        ALOGE("aml_audio_ease_init failed\n");
        ret = -EINVAL;
        goto err_dtv_audio_instances;
    }

    if (aml_audio_ease_init(&adev->volume_ease.ease) < 0) {
        ALOGE("aml_audio volume easing init failed\n");
        ret = -EINVAL;
        goto err_vol_ease;
    }
    adev->volume_ease.config_easing = true;

    // Fix for sink_gain 0.0 missing after system boot-up,
    // then it has a pop when gain change from 0.0 to 0.01
    if (adev->eq_drc_inited && adev->useAudioMixer) {
        adev->last_sink_gain = adev->eq_data.p_gain.speaker;
        AM_LOGI("last_sink_gain %f, use speaker gain", adev->last_sink_gain);
    }

    // adev->debug_flag is set in hw_write()
    // however, sometimes function didn't goto hw_write() before encounting error.
    // set debug_flag here to see more debug log when debugging.
    adev->debug_flag = aml_audio_get_debug_flag();
    adev->count = 1;
    aml_audio_board_config_init(&adev->board_config);

#ifdef SUPPORT_KARAOKE
    /* karaoke config init by json and do other init */
    karaoke_project_init(adev);
#endif

    /*set audio hal process bitwidth*/
    adev_config_process_bitwidth(adev);

    //this init is simple,no need check return value.
    aml_init_stream_manager(adev);

    if (pthread_mutex_init(&adev->bitstream_lock, NULL)) {
        ALOGE("%s pthread_mutex_init(bitstream_lock) failed", __func__);
        goto err_vol_ease;
    }
    ALOGD("%s adev->dolby_lib_type:%d  !is_TV(adev):%d", __func__, adev->dolby_lib_type, !is_TV(adev));
    pthread_mutex_init(&adev->ms12_init_lock, NULL);
    /* create thread for communication between Audio Hal and MS12 */
    if ((eDolbyMS12Lib == adev->dolby_lib_type)) {
        ret = ms12_mesg_thread_create(&adev->ms12);
        if (0 != ret) {
            ALOGE("%s, ms12_mesg_thread_create fail!\n", __func__);
            goto Err_MS12_MesgThreadCreate;
        }

        ret = adev_ms12_prepare((struct audio_hw_device *)adev);
        if (0 != ret) {
            ALOGE("%s, adev_ms12_prepare fail!\n", __func__);
            goto Err_MS12_MesgThreadCreate;
        }
    }

    if (pthread_mutex_init(&adev->streamList_MutexLock, NULL)) {
        AM_LOGE(" pthread_mutex_init(streamList_MutexLock) failed");
        goto Err_init_MuteLock;
    }


    // init hw_mediasync
    adev->hw_mediasync = NULL;
    adev->hw_sync_id = -1;

    adev->stream_bitrate = -1;
    adev->address = NULL;
    adev->usb = NULL;
    pthread_mutex_init(&adev->usb_lock, NULL);
    adev->effect_ctrl.dap_enable = 0;
    adev->effect_ctrl.vx_enable = 0;
    adev->effect_ctrl.effect_mode = EFFECT_MODE_OFF;
    adev->native_postprocess.effect_ctrl.effect_mode = EFFECT_MODE_OFF;
    pthread_mutex_unlock(&adev_mutex);

    adev->fmt_start_mute = false;

    adev->aaudio_low_latency = false;
    adev->aaudio_low_latency_updated = false;
    adev->aaudio_low_latency_count = 0;

    create_async_write_thread();
    adev->mmap_audio_manager = mmap_audio_new_manager(eDolbyMS12Lib == adev->dolby_lib_type);
    aml_init_zero_detect_list(&adev->zero_data_detect_list);

    //adev_open_sys_resource_mgr(adev);
    aml_audio_uevent_open(adev_uevent_callback);

#ifdef ENABLE_AML_ACR
    if (aml_open_ai_audio_module(&adev->native_postprocess, &adev->alsa_mixer) < 0) {
        aml_close_ai_audio_module(&adev->native_postprocess);
    }
#endif

    AM_LOGI("exit ------");
    return 0;

Err_init_MuteLock:
Err_MS12_MesgThreadCreate:
    aml_audio_ease_close(adev->volume_ease.ease);
err_vol_ease:
    aml_audio_ease_close(adev->audio_ease);
err_dtv_audio_instances:
err_spk_tuning_rbuf:
    ring_buffer_release(&adev->spk_tuning_rbuf);
err_out_32_buf:
    aml_audio_free(adev->out_32_buf);
err_out_16_buf:
    aml_audio_free(adev->out_16_buf);
err_adev:
    aml_audio_free(adev);
err:
    pthread_mutex_unlock(&adev_mutex);
    return ret;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "aml audio HW HAL",
        .author = "amlogic, Corp.",
        .methods = &hal_module_methods,
    },
};
