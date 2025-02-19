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

#ifndef DEVICE_PATCH_H_
#define DEVICE_PATCH_H_

#include <sys/types.h>
#include <system/audio.h>
#include <pthread.h>

#include "../../audio_hal/audio_hw.h"
#include "aml_ringbuffer.h"
#include "component_picture_mode.h"
#include "aml_audio_resampler.h"
#include "aml_audio_heaacparser.h"

typedef void (*dtv_avsync_process_cb)(struct audio_stream_out *stream, size_t bytes, audio_format_t output_format);

enum patch_route_e {
    /*source is device, sink is device*/
    PATCH_ROUTE_DEV_DEV = 0,
     /*source is mix, sink is device*/
    PATCH_ROUTE_MIX_DEV = 1,
     /*source is dev, sink is mix*/
    PATCH_ROUTE_DEV_MIX = 2,
     /*source is mix, sink is mix*/
    PATCH_ROUTE_MIX_MIX = 3,
};

enum patch_type_e
{
    PATCH_TYPE_TV = 1,
    PATCH_TYPE_DTV = 1 << 1,
    PATCH_TYPE_ALL = 1 << 2,
    PATCH_TYPE_INVAL = 1 << 3,
};

/**\brief Audio output mode*/
typedef enum
{
    AM_AOUT_OUTPUT_STEREO,     /**< Stereo output*/
    AM_AOUT_OUTPUT_DUAL_LEFT,  /**< Left audio output to dual channel*/
    AM_AOUT_OUTPUT_DUAL_RIGHT, /**< Right audio output to dual channel*/
    AM_AOUT_OUTPUT_SWAP,       /**< Swap left and right channel*/
    AM_AOUT_OUTPUT_LRMIX,       /**< mix left and right channel*/
    AM_AOUT_OUTPUT_JOINT_STEREO /**< JOINT output*/
} AM_AOUT_OutputMode_t;

/* all latency in unit 'ms' */
struct audio_patch_latency_detail
{
    unsigned int ringbuffer_latency;
    unsigned int user_tune_latency;
    unsigned int alsa_in_latency;
    unsigned int alsa_i2s_out_latency;
    unsigned int alsa_spdif_out_latency;
    unsigned int ms12_latency;
    unsigned int total_latency;
};

struct tv_param_config
{
    /* Mute time process */
    struct timespec mute_start_ts;
    int mute_log_cntr;
    int mute_mdelay;
    bool mute_flag;
    /* Packet type for mute process */
    hdmiin_audio_packet_t last_audio_packet_type;
    /* Channel status of audio package from hdmirx */
    int data_type;
    /* HW parser audio format */
    int spdif_fmt_hw;
    /* Sample rate change for mute process */
    int hdmi_in_samplerate;
    bool change_to_HBR;
    bool change_to_none_HBR;
    /* Temporary variable to save packet type. */
    hdmiin_audio_packet_t audio_packet_type_tmp;
};

struct aml_audio_patch
{
    struct audio_hw_device *dev;
    int patch_id;
    enum patch_src_assort patch_src;
    ring_buffer_t aml_ringbuffer;
    ring_buffer_t tvin_ringbuffer;
    pthread_t audio_input_threadID;
    pthread_t audio_output_threadID;
    pthread_t audio_parse_threadID;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    void *in_buf;
    size_t in_buf_size;
    size_t numDecodedSamples;
    size_t numOutputSamples;
    void *out_buf;
    size_t out_buf_size;
    void *out_tmpbuf;
    size_t out_tmpbuf_size;
    int tvin_buffer_inited;
    int assoc_buffer_inited;
    int cmd_process_thread_exit;
    int input_thread_exit;
    int output_thread_exit;
    void *audio_parse_para;
    audio_devices_t input_src;
    audio_format_t aformat;
    int sample_rate;
    int input_sample_rate;
    audio_channel_mask_t chanmask;
    audio_channel_mask_t in_chanmask;
    int in_sample_rate;
    audio_format_t in_format;
    bool arc_layout_b;
    bool last_layout_b;
    int channel_count;
    int ca;
    enum earc_audio_type earcin_audio_type;
    bool cs_mute;
    bool need_reconfig_mediasync;
    bool reset_input;
    audio_devices_t output_src;
    bool is_dtv_src;
    audio_channel_mask_t out_chanmask;
    int out_sample_rate;
    audio_format_t out_format;

    /* for AVSYNC tuning */
    int vltcy;
    int altcy;
    int average_vltcy;
    int average_altcy;
    int avsync_sample_accumulated;
    int max_video_latency;
    int min_video_latency;
    bool need_do_avsync;
    bool input_signal_stable;
    bool is_avsync_start;
    bool skip_frames;
    int timeout_avsync_cnt;

    struct audio_patch_latency_detail audio_latency;
    /* end of AVSYNC tuning */

    /* user setting picture mode */
    picture_mode_t pic_mode;
    bool IEC61937_format;
    bool mode_reconfig_flag;
    /* user setting picture mode end */
    int sync_offset;
    int read_size;
    struct timespec start_ts;
    int mdelay;
    bool start_mute;
    struct aml_stream_out *output_stream;
    /* source data format change */
    bool format_change;
    bool input_teardown_over;
    bool output_teardown_over;
    int ringbuffer_size;
    struct tv_param_config param_config;
};

void create_tvin_buffer(struct aml_audio_patch *patch);
void release_tvin_buffer(struct aml_audio_patch *patch);
void aml_audio_port_config_dump(struct audio_port_config *port_config, int fd);
void adev_audio_patches_dump(struct aml_audio_device *aml_dev, int fd);
#endif
