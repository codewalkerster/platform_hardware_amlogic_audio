/*
 * Copyright (C) 2018 Amlogic Corporation.
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

#ifndef _DTV_PATCH_H_
#define _DTV_PATCH_H_

#include <cutils/str_parms.h>

#define FORMAT_STABLE_COUNT 10

#include "device_patch.h"
#include "dtv_patch_utils.h"
#include "dtv_patch_hal_avsync.h"
#include "dtv_patch_dtvsync.h"

enum {
    AUDIO_DTV_PATCH_DECODER_STATE_IDLE,
    AUDIO_DTV_PATCH_DECODER_STATE_PREPARED,
    AUDIO_DTV_PATCH_DECODER_STATE_RUNNING,
    AUDIO_DTV_PATCH_DECODER_STATE_PAUSED,
    AUDIO_DTV_PATCH_DECODER_STATE_RESUME,
    AUDIO_DTV_PATCH_DECODER_STATE_RELEASE,
};

#define  DVB_DEMUX_ID_BASE 25
#define  DVB_DEMUX_SUPPORT_MAX_NUM 6

#define DTVSYNC_INIT_PTS     (-10000)
#define DTVSYNC_INVALID_PTS   (-20000)


#define DTVSYNC_APTS_THRESHOLD  (-5000)
typedef enum {
   DTV_AUDIO_PATCH = 0,
   DTV_TUNER_FRAMEWORK,
} dtv_audio_scene;


typedef struct aml_dtv_stream_out {
    struct aml_stream_out *stream_out;
} aml_dtv_stream_out;

typedef struct aml_dtv_audio_instance {

    struct aml_audio_patch audio_patch_base;
    struct aml_dtv_stream_out dtv_stream_out;
    pthread_t audio_input_threadID;
    pthread_t audio_output_threadID;
    unsigned int input_thread_created;
    unsigned int output_thread_created;
    pthread_mutex_t dtv_output_mutex;
    pthread_mutex_t dtv_input_mutex;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int input_thread_exit;
    int output_thread_exit;
    audio_devices_t input_src;
    audio_devices_t output_sink;
    audio_format_t aformat;
    int input_sample_rate;
    audio_channel_mask_t in_chanmask;
    audio_format_t in_format;
    bool is_dtv_src;
    audio_channel_mask_t out_chanmask;
    int out_sample_rate;
    audio_format_t out_format;
    bool ad_substream_checked_flag;

    int dtv_aformat;
    int dtv_has_video;
    bool package_checked_flag;
    unsigned char dtv_NchOriginal;
    unsigned char dtv_lfepresent;
    uint64_t  dtv_pcm_wrote;
    unsigned int dtv_pcm_readed;
    AM_AOUT_OutputMode_t mode;

    void *demux_handle;
    aml_dtv_audiopara_t dtv_audio_info;
    aml_dtvsync_t dtvsync;
    dtv_audio_scene dtv_scene;
    int uio_fd;
    int dtv_audio_state;

    int64_t last_min_pts;
    int64_t last_max_pts;
    int64_t last_checkin_apts;
    void *dtv_package_list;
    int audio_pts_dts_flag;
    int pts_margin;//use for t5d ptsserver lookup
    int in_read_frame_size;
    void *ac3_parser_handle;
    void *ad_ac3_parser_handle;
    void *ad_remain_buf;
    int  ad_remain_size;
    void *heaac_parser_handle;
    void *ad_heaac_parser_handle;
    void *ac4_parser_handle;
    struct heaac_parser_info main_heaac_info;
    struct heaac_parser_info ad_heaac_info;
    int32_t PServerDev;
    int update_stable_count;
} aml_dtv_audio_instance_t;

typedef struct aml_dtv_audio_context {
    int dtv_demux_id;
    aml_dtv_audio_instance_t instances[DVB_DEMUX_SUPPORT_MAX_NUM];
    pthread_t audio_cmd_process_threadID;
    pthread_cond_t dtv_cmd_process_cond;
    pthread_mutex_t dtv_cmd_process_mutex;
    struct cmd_node dtv_cmd_list;
    int cmd_process_thread_exit;
} aml_dtv_audio_context_t;


int create_dtv_patch(struct aml_audio_patch **audio_patch, audio_devices_t input, audio_devices_t output __unused);
int release_dtv_patch(struct aml_audio_patch *audio_patch);
int create_dtv_cmd_process_thread(struct aml_dtv_audio_context *context);
int release_dtv_cmd_process_thread(struct aml_dtv_audio_context *context);

#if ANDROID_PLATFORM_SDK_VERSION > 29
int enable_dtv_patch_for_tuner_framework(struct audio_config *config, struct audio_stream_out *stream);
int disable_dtv_patch_for_tuner_framework(struct audio_stream_out *stream);
int out_pause_dtv_stream_for_tunerframework(struct audio_stream_out *stream);
int out_resume_dtv_stream_for_tunerframework(struct audio_stream_out *stream);
int out_start_dtv_stream_for_tunerframework(struct audio_stream_out *stream);
int out_stop_dtv_stream_for_tunerframework(struct audio_stream_out *stream);
int out_flush_dtv_stream_for_tunerframework(struct audio_stream_out *stream);
int out_standby_dtv_stream_for_tunerframework(struct audio_stream_out *stream);
ssize_t out_write_dtv_stream_for_tunerframework(struct audio_stream_out *stream, const void *buffer, size_t bytes);
int out_get_audio_description_mix_level(struct audio_stream_out *stream, float *leveldB);
int out_set_audio_description_mix_level(struct audio_stream_out *stream, const float leveldB);
int out_get_dual_mono_mode(struct audio_stream_out *stream, audio_dual_mono_mode_t *mode);
int out_set_dual_mono_mode(struct audio_stream_out *stream, audio_dual_mono_mode_t mode);
int out_set_volume_for_tunerframework(struct audio_stream_out *stream, float left, float right);
int out_set_playback_rate_parameters_for_tunerframework(struct audio_stream_out *stream, const audio_playback_rate_t *playbackRate);
int out_get_playback_rate_parameters_for_tunerframework(struct audio_stream_out *stream, audio_playback_rate_t *playbackRate);
int out_get_presentation_position_for_tunerframework (const struct audio_stream_out *stream, uint64_t *frames, struct timespec *timestamp);
int out_set_params_for_tunerframework(struct audio_stream_out *stream,struct str_parms *parms);
#endif
int set_dtv_parameters(struct audio_hw_device *dev, struct str_parms *parms);
int get_dtv_parameters(struct audio_hw_device *dev, const char *keys);
int dtv_patch_get_latency(struct aml_audio_device *aml_dev);
int dtv_patch_get_es_pts_dts_flag(struct aml_audio_device *aml_dev);
int dtv_patch_get_cmd_close_status(struct aml_audio_device *aml_dev);
int dtv_patch_get_decoder_fmt(struct aml_audio_device *aml_dev);
int dtv_patch_get_ac4_acivie_res_id(struct aml_audio_device *aml_dev);
#endif /* _DTV_PATCH_H_ */
