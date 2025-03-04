/*
 * hardware/amlogictv/t962x3/audio/TvAudio/aml_audio_ms12.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This source code is subject to the terms and conditions defined in the
 * file 'LICENSE' which is part of this source code package.
 *
 */
#ifndef __AML_AUDIO_MS12_H__
#define __AML_AUDIO_MS12_H__


#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <semaphore.h>
#include <system/audio.h>
#include <cutils/list.h>
#include "dolby_ms12.h"
#include "dolby_ms12_config_params.h"
#include "dolby_ms12_status.h"

#include "aml_ringbuffer.h"



#define DOLBY_SAMPLE_SIZE 4//2ch x 2bytes(16bits) = 4 bytes

/*
 *@brief define enum for MS12 Scheduler state
 */
typedef enum MS12_SCHEDULER_STATE {
    MS12_SCHEDULER_NONE = -1,
    MS12_SCHEDULER_RUNNING =  0,
    MS12_SCHEDULER_STANDBY =  1,

    MS12_SCHEDULER_MAX,
} ms12_scheduler_state_t;

typedef enum MS12_RESUME_STATE {
    MS12_RESUME_NONE = -1,
    MS12_RESUME_FROM_RESUME =  0,
    MS12_RESUME_FROM_FLUSH =  1,
    MS12_RESUME_FROM_CLOSE =  2,
    MS12_RESUME_FROM_DTV_PAUSE = 3,

    MS12_RESUME_MAX,
} ms12_resume_state_t;

enum {
    BITSTREAM_OUTPUT_A,
    BITSTREAM_OUTPUT_B,
    BITSTREAM_OUTPUT_C,  /*multi channel pcm*/
    BITSTREAM_OUTPUT_CNT
};

typedef enum  {
    MS12_CODEC_PARAMETER_CMD = 0,
    MS12_CODEC_PARAMETER_PAUSE = 1,
    MS12_CODEC_PARAMETER_FLUSH = 2,
    MS12_CODEC_PARAMETER_XA = 3,
    MS12_CODEC_PARAMETER_XU = 4,
    MS12_CODEC_PARAMETER_MAIN1_MIXGAIN = 5,
    MS12_CODEC_PARAMETER_MAIN2_MIXGAIN = 6,
    MS12_CODEC_PARAMETER_AD_FADE_PAN = 7,
    MS12_CODEC_PARAMETER_AC4_LANG = 8,
    MS12_CODEC_PARAMETER_AC4_LANG2 = 9,
    MS12_CODEC_PARAMETER_AT = 10,
    MS12_CODEC_PARAMETER_PAT = 11,
    MS12_CODEC_PARAMETER_AC4_DE = 12,
    MS12_CODEC_PARAMETER_AC4_PRES_GROUP_IDX = 13,
    MS12_CODEC_PARAMETER_AC4_SHORT_PROG_ID = 14,
    MS12_CODEC_PARAMETER_MAIN_BUFFER_AVAIL = 15,
    MS12_CODEC_PARAMETER_ASSOCIATE_BUFFER_AVAIL = 16,
    MS12_CODEC_PARAMETER_MAIN_CONSUMED = 17,
    MS12_CODEC_PARAMETER_MAIN_PCMOUT_FRAME = 18,
    MS12_CODEC_PARAMETER_ATMOS_PRESENT = 19,
    MS12_CODEC_PARAMETER_REQUEST_FOCUS = 20,
    MS12_CODEC_PARAMETER_AC4DE_ACTIVE_PRESENTATION = 21,
    MS12_CODEC_PARAMETER_AC4DEC_THE_PGI_IS_PRESENT = 22,
    MS12_CODEC_PARAMETER_MAIN_START_THRESHOLD = 23,
    MS12_CODEC_PARAMETER_MAIN_UNDERRUN = 24,
    MS12_CODEC_PARAMETER_DEC_INFO = 25,
    MS12_CODEC_PARAMETER_CONTENT_VOLUME_LEVELER = 26,
    MS12_CODEC_PARAMETER_CONTENT_DIALOGUE_ENHANCER = 27,
    MS12_CODEC_PARAMETER_MAX,
}ms12_codec_parameter_type_t;

typedef enum  {
    MS12_CODEC_CALLBACK_SYNC,
    MS12_CODEC_CALLBACK_TEMPO,
    MS12_CODEC_CALLBACK_PROCESS,
    MS12_CODEC_CALLBACK_MAX,
}ms12_codec_callback_type_t;

enum MS12_PROCESS_CALLBACK_PCM_TYPE {
    PCM_INT16 = 0,
    PCM_INT32 = 1,
    PCM_FLOAT32 = 2,
};

typedef struct ms12_pcminfo {
    int sample_rate;
    int sample_bytes;
    int channel_num;
}ms12_pcminfo_t;

struct bitstream_out_desc {
    audio_format_t audio_format;
    audio_format_t sub_format;
    int sample_rate;
    void *spdifout_handle;
    int  need_drop_frame;
    bool is_bypass_ms12;
};

typedef struct drc_param {
    int mode;
    int cut;
    int boost;
} drc_param_t;

typedef struct Aml_MS12_ProcessInfo_s {
    int s32SampleRate;
    int s32Channel;
    int s32InFrameType;
    char *pu8InBuffer;
    unsigned int u32InBufferSize;
    int s32OutFrameType;
    char *pu8OutBuffer;
    unsigned int u32OutBufferSize;
    int as32Acmod[2];
} Aml_MS12_ProcessInfo_t;
typedef struct Aml_MS12_DecInfo_s {
    int s32SampleRate;
    int s32ChannelAcmod;
    int s32LfePresent;
    int s32AacProfile;
    int reserved[4];
} Aml_MS12_DecInfo_t;

typedef struct AML_MS12_CodecInfo_s
{
    unsigned int u32AudioFormat;
    int s32AdInput;
    int s32RestrictedAd;
    int s32EnforceTimeslice;
    int s32HeaacAribMode;
    int s32DapContProc;
} AML_MS12_CodecInfo_t;

struct dolby_ms12_desc {
    bool dolby_ms12_enable;
    bool dolby_ms12_init_flags;
    audio_format_t input_config_format;
    audio_channel_mask_t config_channel_mask;
    int config_sample_rate;
    int output_config;
    int output_samplerate;
    audio_channel_mask_t output_channelmask;
    int ms12_out_bytes;
    //audio_policy_forced_cfg_t force_use;
    int dolby_ms12_init_argc;
    char **dolby_ms12_init_argv;
    int dolby_ms12_runtime_argc;
    char **dolby_ms12_runtime_argv;
    int dolby_ms12_codec_argc;
    char **dolby_ms12_codec_argv;
    int dolby_ms12_enc_argc;
    char **dolby_ms12_enc_argv;
    void *dolby_ms12_ptr;
    int dolby_ms12_out_max_size;
    /*
    there are some risk when aux write thread and direct thread
    access the ms12 module at the same time.
    1) aux thread is writing. direct thread is on standby and clear up the ms12 module.
    2) aux thread is writing. direct thread is preparing the ms12 module.
    */
    pthread_mutex_t lock;
    /*
    for higher efficiency we dot use the the lock for main write
    function,as ms clear up may called by binder  thread
    we need protect the risk situation
    */
    pthread_mutex_t main_lock;
    pthread_mutex_t runtime_lock;
    pthread_t dolby_ms12_threadID;
    bool dolby_ms12_thread_exit;
    int device;//alsa_device_t
    struct timespec timestamp;
    uint64_t last_frames_position;
    uint64_t last_ms12_pcm_out_position;
    bool ms12_position_update;
    /*
    latency frame is maintained by the whole device output.
    whatever what bistream is outputed we need use this latency frames.
    */
    int latency_frame;
    int sys_avail;
    int curDBGain;

    // for DDP stream, the input frame is 768/1537/1792(each 32ms)
    // May change through playback.
    // here to calculate average frame size;
    int avgDdpFramesize;
    // the input signal atmos info
    int input_total_ms;
    int bitstream_cnt;
    void * system_virtual_buf_handle;
    ring_buffer_t spdif_ring_buffer;
    char lang[4];
    char lang2[4];
    int at;
    int pat;
    /*
     *-ac4_de             * <int> [ac4] Dialogue Enhancement gain that will be applied in the decoder
     *                      Range: 0 to 12 dB (in 1 dB steps, default is 0 dB)
     */
    int ac4_de;
    int dap_dialogue_enhancer[2];
    int dap_leveler[2];
    /*
     *-dmx              * <int>   Downmix modes
     *                       0 = Lt/Rt (Default)
     *                       1 = Lo/Ro
     */
    int dmx;
    /*
     *-drc              * <int>   DRC modes (for downmixed output)
     *                       0 = Line (Default)
     *                       1 = RF
     *-bs               * <int>   Scale factor for incoming DRC boost value for 2-channel downmix
     *-cs               * <int>   Scale factor for incoming DRC cut value for 2-channel downmix
     *                       0 - 100; Default = 100
     */
    int drc;
    int bs;
    int cs;
    /*
     *-dap_drc          * <int>   DAP DRC mode (for multichannel and DAP output)
     *                       0 = Line (Default)
     *                       1 = RF
     *-b                * <int>   Scale factor for incoming DRC boost value
     *-c                * <int>   Scale factor for incoming DRC cut value
     *                       0 - 100; Default = 100
     */
    int dap_drc;
    int b;
    int c;
    int nbytes_of_dmx_output_pcm_frame;
    int ms12_digital_audio_format;
    audio_format_t optical_format;
    audio_format_t sink_format;
    struct timespec  sys_audio_timestamp;
    uint64_t sys_audio_frame_pos;
    uint64_t sys_audio_base_pos;
    uint64_t sys_audio_skip;
    uint64_t last_sys_audio_cost_pos;
    int atmos_info_change_cnt;
    bool dual_bitstream_support;
    struct bitstream_out_desc bitstream_out[BITSTREAM_OUTPUT_CNT];
    int dap_bypass_enable;
    float dap_bypassgain;
    /*
     * these variables are used for ms12 message thread.
     */
    pthread_t ms12_mesg_threadID;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool CommThread_ExitFlag;
    struct listnode mesg_list;
    struct aml_stream_out *ms12_app_stream_out; /*Reserve for extension*/
    uint64_t dap_pcm_frames;
    uint64_t stereo_pcm_frames;
    uint64_t master_pcm_frames;
    uint64_t ms12_main_input_size;
    uint64_t ms12_main_consume_bytes;
    bool b_legacy_ddpout;
    void *iec61937_ddp_buf;
    float main_volume;
    uint64_t first_in_frame_pts;
    uint64_t last_synced_frame_pts;
    uint64_t out_synced_frame_count;
    bool debug_synced_frame_pts_flag;
    bool     is_muted;
    bool     do_easing;
    /* MAT Encoder inside ms12, begin */
    unsigned int matenc_maxoutbufsize;
    int b_iec_header;
    int mat_enc_debug_enable;
    void *mat_enc_handle;
    char *mat_enc_out_buffer;
    int mat_enc_out_bytes;
    uint64_t dtv_decoder_offset_base;  /*save the dtv input offset, which is related with PTS*/
    /* MAT Encoder inside ms12, end */
    /* For DAP multi output except the stereo output */
    bool tv_tuning_flag;
    int ms12_scheduler_state;
    int last_scheduler_state;
    uint32_t ms12_timer_id;
    bool sys_data_write2alsa_status;
    void * scaletempo;
    bool enable_mixer_max_size;
    bool b_encoder_reset;
    pthread_mutex_t main_apts_update_lock;
    bool main_input_insert_zero;
    bool aaudio_low_latency;
    bool dap_only_enable;
    int alsa_limit_frame;
    bool scheduler_sleep_enable;
    uint64_t scheduler_run_count;

    drc_param_t stereo_drc;
    drc_param_t multi_dap_drc;
    int system_sound_target;
    float tempo_speed;
    void * deep_buf_virtual_buf_handle;
    struct timespec  deep_buf_audio_timestamp;
    uint64_t deep_buf_audio_frame_pos;
    uint64_t deep_buf_audio_base_pos;
    uint64_t deep_buf_audio_skip;
    uint64_t last_deep_buf_audio_cost_pos;
    bool deep_buf_write2alsa_status;
    int ms12_continuous_state;
    sem_t standby_sem;
    uint64_t measure_last_frame_us;
    uint64_t measure_new_frame_us;
    void *continuous_standby_handle;
    unsigned int focus_audioformat;
    int focus_is_dolby_atmos;
    bool focus_is_paused;
    bool focus_is_bypass_ms12;
    bool last_focus_is_bypass_ms12;
    pthread_mutex_t bypass_lock;
};

struct dolby_ms12_dec_desc {
    int dec_id;
    AML_MS12_CodecInfo_t codec_info;
    uint64_t ms12_main_input_size;
    uint64_t ms12_main_consume_bytes;
    int mat_stream_profile;
    struct timespec timestamp;
    uint64_t last_frames_position;
    uint64_t last_ms12_pcm_out_position;
    bool ms12_position_update;
    bool main_input_insert_zero;
    float tempo_speed;
    pthread_mutex_t main_lock;
    bool is_paused;
    void * ac3_parser_handle;
    void * info_ac3_parser_handle;
    void * spdif_dec_handle;
    void * info_spdif_dec_handle;
    /*ms12 main input information */
    void * ms12_bypass_handle;
    bool is_bypass_ms12;
    int is_dolby_atmos;
    int is_focus;
    bool is_muted;
    int last_post_buffer_frame;
    uint64_t last_frames_position_used;

    int resume_state;
    bool need_resume;
    bool need_resync; /*handle from pause to resume sync*/
};

/*
 *@brief this function is get the ms12 suitable output format
 *       1.input format
 *       2.EDID pcm/dd/dd+
 *       3.system setting
 * TODO, get the suitable format
 */
audio_format_t get_dolby_ms12_suitable_output_format(void);

/*
 *@brief get the ms12 output details, samplerate/formate/channelnum
 */
int get_dolby_ms12_output_details(struct dolby_ms12_desc *ms12_desc);

/*
 *@brief init the dolby ms12
 */
int get_dolby_ms12_init(struct dolby_ms12_desc *ms12_desc, char *dolby_ms12_path);

/*
 *@brief get the dolby ms12 config parameters
 * ms12_desc: ms12 handle
 * ms12_config_format: AUDIO_FORMAT_PCM_16_BIT/AUDIO_FORMAT_PCM_32_BIT/AUDIO_FORMAT_AC3/AUDIO_FORMAT_E_AC3/AUDIO_FORMAT_MAT
 * config_channel_mask: AUDIO_CHANNEL_OUT_STEREO/AUDIO_CHANNEL_OUT_5POINT1/AUDIO_CHANNEL_OUT_7POINT1
 * config_sample_rate: sample rate.
 * output_config: bit mask | of {MS12_OUTPUT_MASK_DD/DDP/MAT/STEREO/SPEAKER}
 */
int aml_ms12_config(struct dolby_ms12_desc *ms12_desc
                    , audio_format_t config_format
                    , audio_channel_mask_t config_channel_mask
                    , int config_sample_rate
                    , int output_config
                    , char *dolby_ms12_path);
/*
 *@brief cleanup the dolby ms12
 */
int aml_ms12_cleanup(struct dolby_ms12_desc *ms12_desc);

int aml_ms12_update_runtime_params(struct dolby_ms12_desc *ms12_desc, char *cmd);

int aml_ms12_update_runtime_params_direct(struct dolby_ms12_desc *ms12_desc
                    , int argc
                    , char **argv);

#if 0
int aml_ms12_update_runtime_params_lite(struct dolby_ms12_desc *ms12_desc);
#endif

int aml_ms12_lib_preload(char *dolby_ms12_path);

int aml_ms12_lib_release();

int aml_ms12_main_decoder_open(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *handle, AML_MS12_CodecInfo_t * codec_info);

int aml_ms12_main_decoder_close(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle);

int aml_ms12_main_decoder_write(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle, void *buffer, int size, void *pcm_info);
int aml_ms12_associate_decoder_write(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle, void *buffer, int size, void *pcm_info);


int aml_ms12_decoder_pause(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle);

int aml_ms12_decoder_resume(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle);

int aml_ms12_decoder_flush(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle);

int aml_ms12_main_decoder_process(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle);

int aml_ms12_main_encoder_reconfig(struct dolby_ms12_desc *ms12_desc, int output_config);

int aml_ms12_decoder_setparameter(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle, int parameter_type, void *parameter, int size);

int aml_ms12_decoder_getparameter(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle, int parameter_type, void *parameter, int size);

int aml_ms12_decoder_register_callback(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle, int callback_type, void *callback, void *priv_data);

int aml_ms12_decoder_unregister_callback(struct dolby_ms12_desc *ms12_desc, struct dolby_ms12_dec_desc *dec_handle, int callback_type);

#endif //end of __AML_AUDIO_MS12_H__
