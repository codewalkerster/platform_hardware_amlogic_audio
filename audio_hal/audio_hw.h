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

#ifndef _AUDIO_HW_H_
#define _AUDIO_HW_H_

#include <audio_utils/resampler.h>
#include <hardware/audio.h>
#include <cutils/list.h>
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>
#include <linux/ioctl.h>

/* ALSA cards for AML */
#define CARD_AMLOGIC_BOARD 0
#define CARD_AMLOGIC_DEFAULT CARD_AMLOGIC_BOARD
/* ALSA ports for AML */
#if (ENABLE_HUITONG == 0)
#define PORT_MM 1
#endif

#define ADD_AUDIO_DELAY_INTERFACE
#include "audio_hwsync.h"
#include "../vendor_process/include/audio_post_process.h"
#include "aml_hw_mixer.h"
#include "../aml_aq_hw/audio_eq_drc_compensation.h"
#include "aml_audio_types_def.h"
#include "aml_alsa_mixer.h"
#ifndef MS12_V24_ENABLE
#include "../decoder/libms12_v1/include/aml_audio_ms12.h"
#else
#include "../decoder/libms12_v24/include/aml_audio_ms12.h"
#endif
#include "../input/include/tv_patch_format_parser.h"
#include "../input/include/hdmirx_utils.h"

#include "amlAudioMixer.h"
#include "audio_port.h"
#include "aml_audio_ease.h"
#include "aml_malloc_debug.h"

#ifdef ADD_AUDIO_DELAY_INTERFACE
#include "aml_audio_delay.h"
#endif

#include "aml_audio_resample_manager.h"
#include "aml_audio_resampler.h"
#include "aml_audio_speed_manager.h"
#include "../decoder/include/aml_dec_api.h"
#include "../decoder/include/aml_dtshd_dec_api.h"
#include "../decoder/include/aml_dtsx_dec_api.h"
#include "audio_usb_hal.h"
#include "aml_audio_timer.h"
#include "aml_config_data.h"
#include "audio_hw_resource_def.h"
#include "../input/include/device_patch_mgr.h"
#include "aml_audio_stream_base.h"
#include "aml_volume_shaper.h"

#ifdef ENABLE_AUTOMOTIVE_AUDIO_FUNCTION
#include "../automotive/bus_submix_core.h"
#include "../automotive/bus_stream_out.h"
#endif

/* number of frames per period */
/*
 * change DEFAULT_PERIOD_SIZE from 1024 to 512 for passing CTS
 * test case test4_1MeasurePeakRms(android.media.cts.VisualizerTest)
 */
#define DEFAULT_PLAYBACK_PERIOD_SIZE 512//1024
#define DEFAULT_VIDEO_VALID_LATENCY 1000*1000  //1s
#define DEFAULT_CAPTURE_PERIOD_SIZE  1024
#define DEFAULT_PLAYBACK_PERIOD_CNT 6

// Base on : audioflinger fast mixer enable threshold, netflix ninja11 vsp-al1-23fps-heaac issue
// static const uint32_t kMinNormalSinkBufferSizeMs = 20;
#define NORMAL_MIXER_MIN_BUFFER_FRAMES (20 * 48)

#define LOW_LATENCY_PLAYBACK_PERIOD_SIZE 256
#define LOW_LATENCY_CAPTURE_PERIOD_SIZE  512

#define LOW_LATENCY_PLAYBACK_NETFLIX_PERIOD_SIZE   256
#define LOW_LATENCY_PLAYBACK_NETFLIX_PERIOD_COUNT   8


/* number of ICE61937 format frames per period */
#define DEFAULT_IEC_SIZE 6144
#define PATCH_PERIOD_COUNT  4

/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define RESAMPLER_BUFFER_FRAMES (DEFAULT_PLAYBACK_PERIOD_SIZE * 6)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES)

static unsigned int DEFAULT_OUT_SAMPLING_RATE = 48000;

/* sampling rate when using MM low power port */
#define MM_LOW_POWER_SAMPLING_RATE 44100
/* sampling rate when using MM full power port */
#define MM_FULL_POWER_SAMPLING_RATE 48000
/* sampling rate when using VX port for narrow band */
#define VX_NB_SAMPLING_RATE 8000
#define VX_WB_SAMPLING_RATE 16000

#define AUDIO_PARAMETER_STREAM_EQ "audioeffect_eq"
#define AUDIO_PARAMETER_STREAM_SRS "audioeffect_srs_param"
#define AUDIO_PARAMETER_STREAM_SRS_GAIN "audioeffect_srs_gain"
#define AUDIO_PARAMETER_STREAM_SRS_SWITCH "audioeffect_srs_switch"

/* Get a new HW synchronization source identifier.
 * Return a valid source (positive integer) or AUDIO_HW_SYNC_INVALID if an error occurs
 * or no HW sync is available. */
#define AUDIO_PARAMETER_HW_AV_SYNC "hw_av_sync"

#define AUDIO_PARAMETER_HW_AV_EAC3_SYNC "HwAvSyncEAC3Supported"

#define PROP_AUDIO_OUTPUT_SPDIF_COEXIST                 "persist.vendor.media.audio.spdif.coexist"
#define PROP_AUDIO_OUTPUT_FORCEUSE                      "persist.vendor.media.audio.forceuse"

#define SYS_NODE_EARC           "/sys/class/extcon/earcrx/state"

#define DDP_FRAME_SIZE      (768)
#define EAC3_MULTIPLIER     (4) //EAC3 bitstream in IEC61937
#define HBR_MULTIPLIER      (16) //MAT or DTSHD bitstream in IEC61937
#define JITTER_DURATION_MS  (3)
#define FLOAT_ZERO              (0.00002)   /* the APM mute volume is 0.00001, less than 0.00002 we think is mute. */
#define TV_SPEAKER_OUTPUT_CH_NUM    10

#define OFFLOAD_BUFFER_SIZE_DURATION_MS (85) // Unit:ms. Each decoded data can be played for 85 ms
#define DTS_OFFLOAD_BUFFER_MAX_SIZE     (32768)
#define OFFLOAD_BUFFER_SIZE_ALIGNMENT    (8)

#define TIME_DIFF_THRESHOLD  (10)

#ifndef AUDIO_HAL_DISABLE_MS12
#define DIALOGUE_ENHANCEMENT_OFF    0
#define DIALOGUE_ENHANCEMENT_LOW    1
#define DIALOGUE_ENHANCEMENT_MEDIUM 2
#define DIALOGUE_ENHANCEMENT_HIGH   3

#define SOUND_DMX_MODE_SURROUND  0
#define SOUND_DMX_MODE_STEREO    1
#endif

#ifdef SUPPORT_KARAOKE
#ifndef AUDIO_SOURCE_KARAOKE_SPEAKER
#define AUDIO_SOURCE_KARAOKE_SPEAKER 1001
#endif
#endif

#define BUFF_SIZE_IEC61937_192KHZ_8CH (16 * 4096)
#define BUFF_SIZE_IEC61937_192KHZ_2CH (4 * 4096)
#define BUFF_SIZE_IEC61937            (4 * 1024)

/*the same as "AUDIO HAL FORMAT" in kernel*/
enum audio_hal_format {
    TYPE_PCM = 0,
    TYPE_DTS_EXPRESS = 1,
    TYPE_AC3 = 2,
    TYPE_DTS = 3,
    TYPE_EAC3 = 4,
    TYPE_DTS_HD = 5 ,
    TYPE_MULTI_PCM = 6,
    TYPE_TRUE_HD = 7,
    TYPE_DTS_HD_MA = 8,//should not used after we unify DTS-HD&DTS-HD MA
    TYPE_PCM_HIGH_SR = 9,
    TYPE_AC4 = 10,
    TYPE_MAT = 11,
    TYPE_DDP_ATMOS = 12,
    TYPE_TRUE_HD_ATMOS = 13,
    TYPE_MAT_ATMOS = 14,
    TYPE_AC4_ATMOS = 15,
    TYPE_DTS_HP = 16,
    TYPE_DDP_ATMOS_PROMPT_ON_ATMOS = 17,
    TYPE_TRUE_HD_ATMOS_PROMPT_ON_ATMOS = 18,
    TYPE_MAT_ATMOS_PROMPT_ON_ATMOS = 19,
    TYPE_AC4_ATMOS_PROMPT_ON_ATMOS = 20,
    TYPE_AAC  = 21,
    TYPE_HEAAC = 22,
    TYPE_DTSX = 23,
};
#define FRAMESIZE_16BIT_STEREO 4
#define FRAMESIZE_32BIT_STEREO 8
#define FRAMESIZE_32BIT_3ch 12
#define FRAMESIZE_32BIT_5ch 20
#define FRAMESIZE_32BIT_8ch 32


/* copy from VTS */
/* hardware/interfaces/audio/core/all-versions/default/include/core/default/Util.h */
/*
    0            ->         Result::OK
    -EINVAL      ->         Result::INVALID_ARGUMENTS
    -ENODATA     ->         Result::INVALID_STATE
    -ENODEV      ->         Result::NOT_INITIALIZED
    -ENOSYS      ->         Result::NOT_SUPPORTED
*/
enum Result {
    OK,
    NOT_INITIALIZED,
    INVALID_ARGUMENTS,
    INVALID_STATE,
    NOT_SUPPORTED,
    RESULT_TOO_BIG
};

#define AML_HAL_MIXER_BUF_SIZE  64*1024

#define SYSTEM_APP_SOUND_MIXING_ON 1
#define SYSTEM_APP_SOUND_MIXING_OFF 0

#define AML_HAL_INVALID_PATCH_HANDLE -1

struct audio_patch_set {
    struct listnode list_node;
    struct audio_patch audio_patch;
	void *aml_audio_patch;
};

typedef enum stream_type {
    STREAM_PCM_NORMAL       = 0,
    STREAM_PCM_DIRECT       = 1,
    STREAM_PCM_HWSYNC       = 2,
    STREAM_RAW_DIRECT       = 3,
    STREAM_RAW_HWSYNC       = 4,
    STREAM_PCM_PATCH        = 5,
    STREAM_RAW_PATCH        = 6,
    STREAM_PCM_MMAP         = 7,
    STREAM_PCM_DEEP_BUF     = 8,

    STREAM_TYPE_MAX      = 9,
} stream_type_t;

typedef enum alsa_device {
    I2S_DEVICE = 0,
    DIGITAL_DEVICE, /*for spdifa*/
    TDM_DEVICE,
    EARC_DEVICE,
    DIGITAL_DEVICE2, /*for spdifb*/
    ALSA_DEVICE_CNT
} alsa_device_t;

typedef enum audio_stream_status {
    STREAM_STANDBY  = 0,
    STREAM_HW_WRITING,
    STREAM_MIXING,
    STREAM_PAUSED,
    STREAM_STATUS_MAX
} stream_status_t;

/*foreground stream type for direct or offload,
**it is mainly for marking lastest stream in AudioHal,
**switch streams between patch(HDMI/DTV/CVBS) and audioflinger.
*/
typedef enum fg_stream_type{
    FG_STREAM_TYPE_NONE = -1,
    FG_STREAM_TYPE_AUDIOFLINGER,
    FG_STREAM_TYPE_PATCH,
    FG_STREAM_TYPE_MAX
} fg_stream_type_t;

typedef enum aml_audio_device_out_type {
   /* Android define AUDIO_DEVICE_OUT_DEFAULT   0x40000000u*/
   AML_AUDIO_DEVICE_OUT_EXTERNAL_SPEAKER = AUDIO_DEVICE_OUT_DEFAULT + 1,
} aml_audio_device_out_type_t;

typedef union {
    unsigned long long timeStamp;
    unsigned char tsB[8];
} aec_timestamp;

struct aml_audio_mixer;

typedef struct audio_hal_info{
    audio_format_t format;
    bool is_dolby_atmos;
    int update_type;
    int update_cnt;
    int aml_dap_surround_virtualizer;
} audio_hal_info_t;

struct aml_bt_output {
    pthread_mutex_t lock;
    bool active;
    struct pcm *pcm_bt;
    struct pcm_config cfg;
    char *bt_out_buffer;
    size_t bt_out_frames;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *resampler_buffer;
    size_t resampler_buffer_size_in_frames;
    size_t resampler_in_frames;
};

typedef enum DEVICE_TYPE {
    STB = 0,
    TV  = 1,
    SBR = 2,
    BDS = 3
} device_type_t;

enum ms12_config_leveler {
    MS12_CONFIG_Y = 0, /*not support dap*/
    MS12_CONFIG_X = 1, /*support dap, but not support atmos*/
    MS12_CONFIG_Z = 2, /*support dap, and support atmos*/
};

struct audio_hw_resource_mgr;
struct sys_resource_manager_handler;

typedef struct media_sync_info{
    void *handle;
    int32_t id;

    // Sometimes NTS(fly audio) two output streams use same mediasync id,
    // Can not simply release mediasync instance when close output stream.
    int ref_count;
} media_sync_info_t;


#define HDMI_ARC_MAX_FORMAT  20
struct aml_audio_device {
    struct audio_hw_device hw_device;
    /* see note below on mutex acquisition order */
    pthread_mutex_t lock;
    pthread_mutex_t pcm_write_lock;
    /*
    if dolby ms12 is enabled, the ms12 thread will change the
    stream information depending on the main input format.
    we need protect the risk and this case.
    */
    pthread_mutex_t trans_lock;
    int mode;
    bool enable_hfp;
    audio_devices_t in_device;
    audio_devices_t out_device;
    int in_call;
    struct aml_stream_in *active_input;
    bool mic_mute;
    //when set speaker_off, this param will be used to judge the routing_update,
    //which is equal to user_setting.
    bool speaker_mute_user_setting;
    unsigned int card;
    struct echo_reference_itfe *echo_reference;
    bool low_power;
    struct aml_stream_out *hwsync_output;
    struct pcm *pcm;
    struct aml_bt_output bt_output;
    bool pcm_paused;
    unsigned hdmi_arc_ad[HDMI_ARC_MAX_FORMAT];
    bool hi_pcm_mode;
    /* audio configuration for dolby HDMI/SPDIF output. AML_AUDIO_DIGITAL_MODE_E*/
    int digital_audio_mode;
    int last_digital_audio_mode;
    int spdif_format;
    bool spdif_enable;
    int hdmi_is_pth_active;
    int disable_pcm_mixing;
    bool enable_soundbar_mode;

    int a2dp_updated;
    void * a2dp_hal;
    pthread_mutex_t a2dp_lock;
    bool bt_avrcp_supported;
    int digital_audio_mode_updated;
    struct aml_native_postprocess native_postprocess;

    /* for port config info */
    float sink_gain[OUTPORT_MAX];
    float speaker_volume;
    audio_devices_t cur_out_devices;
    struct aml_stream_out *active_outputs[STREAM_TYPE_MAX];

    /* indicates atv to mixer patch, no need HAL patching  */
    bool dev2mix_patch;
    /* Now only two pcm handle supported: I2S, SPDIF */
    pthread_mutex_t alsa_pcm_lock;
    struct pcm *pcm_handle[ALSA_DEVICE_CNT];
    int pcm_refs[ALSA_DEVICE_CNT];
    bool is_paused[ALSA_DEVICE_CNT];
    struct aml_hw_mixer hw_mixer;
    audio_format_t sink_format;
    bool sink_format_changed;
    bool sink_format_updating;
    unsigned int sink_max_channels;
    audio_format_t optical_format;
    audio_format_t sink_capability;
    audio_format_t last_sink_capability;

    bool dual_spdifenc_inited;

    /* Dolby MS12 lib variable start */
    struct dolby_ms12_desc ms12;
    bool dolby_ms12_status;
    struct pcm_config ms12_config;
    struct pcm_config dcv_config;

    bool ad_switch_enable;
    bool need_reset_for_dual_decoder;
    uint64_t a2dp_no_reconfig_ms12;
    /* Dolby MS12 lib variable end */

    /**
     * enum eDolbyLibType
     * DolbyDcvLib  = dcv dec lib   , libHwAudio_dcvdec.so
     * DolbyMS12Lib = dolby MS12 lib, libdolbyms12.so
     */
    int dolby_lib_type;
    int dolby_lib_type_last;
    int dolby_decode_enable;   /*it can decode dolby, not passthrough lib*/
    int dts_lib_type;
    int dts_decode_enable;
    int support_ms12_version;

    /*used for dtshd decoder*/
    struct dca_dts_dec dts_hd;
    /*used for dtsx decoder*/
    dtsx_dec_t dts_x;
    bool bDVEnable;
    int16_t *out_16_buf;
    size_t out_16_buf_size;
    int32_t *out_32_buf;
    size_t out_32_buf_size;
    int32_t *tmp_buffer_8ch;
    size_t tmp_buffer_8ch_size;
    int16_t *audioeffect_tmp_buffer;
    size_t audioeffect_tmp_buffer_size;
    size_t spk_tuning_lvl;
    /* ringbuffer for tuning latency total buf size */
    size_t spk_tuning_buf_size;
    ring_buffer_t spk_tuning_rbuf;
    bool mix_init_flag;
    struct eq_drc_data eq_data;
    bool eq_drc_inited;
    int aml_dap_v1_enable;
    /*used for high precision A/V from amlogic amadec decoder*/
    unsigned first_apts;
    /*
    first apts flag for alsa hardware prepare,true,need set apts to hw.
    by default it is false as we do not need set the first apts in normal use case.
    */
    bool first_apts_flag;
    size_t frame_trigger_thread;
    void *dev_to_mix_parser;
    int continuous_audio_mode;
    int continuous_audio_mode_default;
    int delay_disable_continuous;
    bool atoms_lock_flag;
    int  exiting_ms12;
    bool doing_reinit_ms12;    /*we are doing reinit ms12*/
    bool switching_dolby_lib;   /*we are switching dolby lib*/
    bool ms12_to_be_cleanup;
    struct timespec ms12_exiting_start;
    int debug_flag;
    int dcvlib_bypass_enable;
    int dtslib_bypass_enable;
    float dts_post_gain;
    bool spdif_encoder_init_flag;
    /*atsc has video in program*/
    bool is_has_video;
    struct aml_stream_out *ms12_out;
    struct aml_stream_out *focus_ms12_stream;
    int system_app_mixing_status;
    int audio_type;
    struct aml_mixer_handle alsa_mixer;
    void *mixerData;
    bool useAudioMixer;
    int tsync_fd;
    bool raw_to_pcm_flag;
    bool is_netflix;

    int need_reset_ringbuffer;
    unsigned int tv_mute;

    bool mute_start;
    bool compensate_video_enable;

    aml_audio_ease_t  *audio_ease;

    int dap_bypass_enable;
    float dap_bypassgain;

    int count;
    int sound_track_mode;

    void *alsa_handle[ALSA_DEVICE_CNT];
    int FactoryChannelReverse;
    bool dual_spdif_support; /*1 means supports spdif_a & spdif_b & spdif interface*/
    bool ms12_force_ddp_out; /*1 force ms12 output ddp*/
    bool spdif_coexist_other; /* spdif coexist other device */

    uint64_t  sys_audio_frame_written;
    uint64_t  deep_buf_audio_frame_written;
    void* hw_mediasync;
    media_sync_info_t mediasync[HW_SYNC_MAX];
    pthread_mutex_t mediasync_lock; // used for create/release
    struct aec_t *aec;
    bool bt_wbs;
    int security_mem_level;
    int dolby_ms12_dap_init_mode;

    /* display audio format on UI, both streaming and hdmiin*/
    audio_hal_info_t audio_hal_info;
    bool is_ms12_tuning_dat; /* a flag to determine the MS12 tuning data file is existing */

    struct volume_ease volume_ease;
    float last_sink_gain;
    struct usb_audio_device usb_audio;
#ifdef SUPPORT_KARAOKE
    /* for linein karaoke mixer */
    struct kara_manager linein_karaoke;
#endif
    //change variable name from hw_mediasync_id to hw_sync_id for more easy to extension.
    int32_t hw_sync_id;

    struct timespec mute_start_ts;

    /* -End- */
    bool arc_connected_reconfig;  /*when arc connected, set it as to true*/
    bool is_arc_updating_sad;  /* earc->arc/arc->earc, update SAD. */

    /*
    for karaoke use case, the apk will access
    the sound card device directly.the apk will
    send the direct mode flag to audio hal. the audio
    hal need by-pass hw access until the apk release flag
    */
    unsigned int direct_mode;
    bool audio_patch_2_af_stream;
    int stream_bitrate; // current offload stream bitrate
    /* Early suspend */
    pthread_mutex_t wake_lock;
    pthread_cond_t wake_cond;
    /*used to restore the continuous_audio_mode after system resume(early suspend case)*/
    int continuous_audio_mode_backup;
    bool aml_truehd_passthrough_support;  /*whether dolby truehd passthrough can be supported*/
    //bool frame_write_sum_updated;

    /* board specific json configs */
    struct audio_board_config board_config;

    bool is_ui_force_dap_disable; //dapv2.4 debug UI on (dap enable), off (dap disable)
    /* board specific json configs */
    int hdmitx_src; /* HDMITX src select for TDM */
    bool spdif_independent;  /*spdif output can be independent with HDMI output*/
    enum AML_SRC_TO_HDMITX hdmitx_multi_ch_src;
    enum AML_SRC_TO_HDMITX hdmitx_hbr_src;
    fg_stream_type_t foreground_stream_type;
    bool continuous_enable_mixer_max_size;
    bool reset_hpd;
    struct timespec fmt_start_ts;
    bool fmt_start_mute;
    int fmt_mdelay;
    float a2dp_vol;
    int dac_value;
    int stream_pause_delay;
    int dac_softmute_delay;

    bool aaudio_low_latency;
    bool aaudio_low_latency_updated;
    int  aaudio_low_latency_count;
    void *mmap_audio_manager;
    /* Modularized shared resource management */
    struct patch_manager *patch_manager;
    struct audio_hw_resource_mgr *hw_resource_mgr;
    struct hdmi_capability_manager *hdmi_cap_mgr;
    struct sys_resource_manager_handler *sys_res_mgr;

    char *address; // for usb
    struct usb_out *usb;
    pthread_mutex_t usb_lock;
    pthread_mutex_t stream_release_lock;
    bool atmos_indicator_status;
    struct aml_post_effect_ctrl effect_ctrl;
    /*A consistent loudness level must be maintained at the PCM output, for the Dolby or other audio formats. */
    /*It is desirable that the bitstream output(over S/PDIF, HDMI, or eARC) should be played back at a consistent level.*/
    int loudness_level;//Specify the loudness level of decoding output

    /* primary streamout config format, juged by policy */
    audio_format_t primary_out_format;
    /* index for submix ringbuffer */
    int port_index;
    pthread_mutex_t bitstream_lock;
    bool singleDmxNonTunnelMode;

    /* if no data write, donot open pcm device and write,
       otherwise bootvideo can't open pcm device and play failed.*/
    bool first_data;
    bool ms12_dynamic_sleep;

    bool mlock_library_done;  /* mlock the necessary library map address, avoid library page fault(stuck a while) */

    int apu_migrate_stream_count; /* number of output stream migrated on audio-cpu */


    pthread_mutex_t ms12_init_lock;  /*this mutex is used for adev_ms12_prepare/adev_ms12_cleanup*/
    int avsync_compensate_delay_ms; /*compensate the avsync audio delay*/
    bool b_ott_tv_arc_connected;    /*the hdmitx connection is ott --> TV  --> ARC AVR/SOUNDBAR*/
    int arc_delay_ms;               /*assume avr/soundbar delay of above connections is 100ms default value*/

    bool is_alsa_device_conflict;
    struct listnode stream_ListHead;
    pthread_mutex_t streamList_MutexLock;
    uint32_t streamCount;
    bool is_main_stream_exist;
    bool reset_hdmitx_audio;
    bool is_dtg_case;//dtg case at the UK
    void *zero_data_detect_list;
};

struct meta_data {
    uint32_t frame_size;
    uint64_t pts;
    uint64_t payload_offset;
};

struct meta_data_list {
    struct listnode list;
    struct meta_data mdata;
};

typedef enum audio_data_handle_state {
    AUDIO_DATA_HANDLE_NONE = 0,
    AUDIO_DATA_HANDLE_START,
    AUDIO_DATA_HANDLE_DETECT,
    AUDIO_DATA_HANDLE_DETECTED,
    AUDIO_DATA_HANDLE_EASE_CONFIG,
    AUDIO_DATA_HANDLE_EASING,
    AUDIO_DATA_HANDLE_FINISHED,

    AUDIO_DATA_HANDLE_MAX
} audio_data_handle_state_t;

typedef struct audio_data_handle_info {
    audio_data_handle_state_t state;
    uint64_t has_detected_bytes;
    int max_detect_time_ms;
    int easing_time;
    int16_t *pcm16_buf;
    size_t pcm16_buf_size;
} audio_data_handle_info_st;

typedef struct aml_stream_speed_info {
    float speed;
    float mPitch;
    float last_speed;
    audio_timestretch_stretch_mode_t  mStretchMode;
    audio_timestretch_fallback_mode_t mFallbackMode;
    aml_audio_speed_t *speed_handle;

    int last_latency_frame;
    bool hwsync_force_update;

    aml_audio_speed_post_delay_t post_delay;
    aml_audio_speed_start_ts_t start_ts;

    // These micro speed adjustment only apply on audio
    aml_audio_speed_apts_gap_t sync_apts_gap;
    aml_audio_speed_apts_gap_ease_t apts_gap_ease;
    int64_t last_out_frame_diff_us;

    // when micro speed adjustment enable, split large data into small piece
    bool split_mode;
    void *local_buf_ptr;
    int local_buf_size;
    int local_buf_used_bytes;
} aml_stream_speed_info_t;

struct aml_stream_out {
    struct audio_stream_out stream;
    struct aml_streamout_base base;
    /* see note below on mutex acquisition order */
    pthread_mutex_t lock;
    struct audio_config audioCfg;
    /* config which set to ALSA device */
    struct pcm_config config;
    audio_format_t    alsa_output_format;
    /* channel mask exposed to AudioFlinger. */
    audio_channel_mask_t hal_channel_mask;
    /* format mask exposed to AudioFlinger. */
    audio_format_t hal_format;
    /* samplerate exposed to AudioFlinger. */
    unsigned int hal_rate;
    unsigned int hal_ch;
    unsigned int hal_frame_size;
    audio_output_flags_t flags;
    audio_devices_t out_device;
    audio_io_handle_t io_handle;
    struct pcm *pcm;
    struct resampler_itfe *resampler;
    char *buffer;
    size_t buffer_frames;
    bool standby;
    bool flush_first_write;
    struct aml_audio_device *dev;
    int write_threshold;
    bool low_power;
    unsigned multich;
    int codec_type;
    uint64_t frame_write_sum;
    uint64_t frame_skip_sum;
    uint64_t last_frames_position;
    uint64_t last_frames_when_paused;   /*Record frames when paused,equal to out->hwsync_parsed_frames_sum*/
    uint64_t spdif_enc_init_frame_write_sum;
    int skip_frame;
    int is_tv_platform;
    bool pause_status;
    bool hw_sync_mode;
    int  tsync_status;
    float volume_l;
    float volume_r;
    float last_volume_l;
    float last_volume_r;
    bool ms12_vol_ctrl;
    int last_codec_type;
    /**
     * as raw audio framesize  is 1 computed by audio_stream_out_frame_size
     * we need divide more when we got 61937 audio package
     */
    int raw_61937_frame_size;
    /* recorded for wraparound print info */
    unsigned last_dsp_frame;
    audio_hwsync_t *hwsync;
    struct timespec timestamp;
    struct timespec lasttimestamp;
    stream_type_t streamType;
    int streamTypeIndex;
    /**
     * flag indicates that this stream need do mixing
     * int is_in_mixing: 1;
     * Normal pcm may not hold alsa pcm device.
     */
    int is_normal_pcm;
    unsigned int card;
    alsa_device_t device;
    ssize_t (*write)(struct audio_stream_out *stream, void *abuffer);
    stream_status_t stream_status;
    audio_format_t hal_internal_format;
    bool dual_output_flag;
    uint64_t input_bytes_size;
    uint64_t continuous_audio_offset;
    bool hwsync_pcm_config;
    bool hwsync_raw_config;
    bool direct_raw_config;
    bool is_device_differ_with_ms12;
    uint64_t total_write_size;
    int  ddp_frame_size;
    int dropped_size;
    unsigned long long mute_bytes;
    bool is_get_mute_bytes;
    bool normal_pcm_mixing_config;
    uint32_t latency_frames;
    int inputPortID;
    pthread_mutex_t cond_lock;
    pthread_cond_t cond;
    struct hw_avsync_header_extractor *hwsync_extractor;
    struct listnode mdata_list;
    pthread_mutex_t mdata_lock;
    bool first_pts_set;
    bool need_first_sync;
    uint64_t last_pts;
    uint64_t last_payload_offset;
    uint64_t last_hwsync_header_pts;
    uint64_t last_dec_out_frame;
    uint64_t last_dec_out_pcm_frame;
    struct audio_config out_cfg;
    uint64_t us_used_last_write;
    bool offload_mute;
    bool need_convert;
    size_t last_payload_used;
    int ddp_frame_nblks;
    uint64_t total_ddp_frame_nblks;
    int framevalid_flag;
    bool bypass_submix;//the variable can be deleted later.
    int need_drop_size;
    int position_update;
    bool spdifenc_init;
    void *spdifenc_handle;
    bool dual_spdif;
    int codec_type2;  /*used for dual bitstream output*/
    struct pcm *pcm2; /*used for dual bitstream output*/
    int pcm2_mute_cnt;
    bool is_tv_src_stream;
    bool is_netflix_src_stream;
	bool is_dtv_src_stream;
	bool is_eos;
    bool last_timestamp_valid;
    uint64_t  last_frame_reported;
    struct timespec  last_timestamp_reported;
    void    *pstMmapAudioParam;    // aml_mmap_audio_param_st (aml_mmap_audio.h)
    bool ac3_parser_init;
    void * ac3_parser_handle;
    struct resample_para aml_resample;
    unsigned char *resample_outbuf;
    bool restore_hdmitx_selection;
    bool restore_continuous;
    bool restore_dolby_lib_type;
    bool switch_nonms12_check;
    void * ac4_parser_handle;
    int64_t last_mmap_nano_second;
    int32_t last_mmap_position;
    uint64_t main_input_ns;
    bool is_sink_format_prepared;
    bool is_ms12_main_decoder;
    aml_dec_config_t  dec_config;               /*store the decode config*/
    aml_dec_t *aml_dec;                        /*store the decoder handle*/
    int ad_substream_supported;
    aml_audio_resample_t *resample_handle;

    /*spdif output related info start*/
    audio_format_t optical_format;
    //audio_format_t spdif_audio_format;
    void *spdifout_handle;
    //audio_format_t spdif2_audio_format;
    void *spdifout2_handle;
    /*spdif output related info end*/
    void * virtual_buf_handle;
    bool is_add2active_output;
    uint32_t alsa_write_cnt;
    uint64_t alsa_write_frames;
    aml_audio_ease_t  *audio_stream_ease;
	aml_audio_ease_config_t ease_config;
    audio_data_handle_info_st data_handle_info;
    float output_speed;
    int dtvsync_enable;

    uint64_t write_time;
    uint64_t pause_time;
    uint64_t flush_time;
    int write_count;
    bool is_dtscd;
    bool iec_check;
    bool alsa_running_status;
    bool alsa_status_changed;

    bool write_status;
    int demux_id;
    struct timespec cbs_cmd_timestamp;
    char stream_dump_file[128];
    bool frame_write_sum_updated;
    bool is_insert_zero_data;
    bool is_waiting_video;
    bool restore_vmaster;
    bool hwsync_header_stripped;
    uint32_t insert_zero_data_ms;
    uint32_t timer_id;
    uint32_t timer_id2;     /*for pause function callback*/
    uint64_t hwsync_parsed_frames_sum;

    pthread_mutex_t apts_update_lock; /*SWPL-88828: Make sure audio timestamps and frame positions are updated synchronously.*/
    struct timespec last_info_timestamp;
    uint64_t last_periodic_print_time_in_ms;
    struct timespec last_avsync_timestamp;
    int64_t jitter_ms;
    int     audio_delay;
    int64_t needs_compensation_timeus;
    uint64_t hwsync_parsed_frames_sum_paused;
    uint32_t last_write_start_time_in_ms; // For checking the writing time
    uint32_t last_write_data_in_byte; // For checking the writing time
    bool is_mat_changed;
    uint64_t frame_offset;
    uint64_t decoded_frame;
    bool b_install_sync_callback;
    bool aaudio_low_latency;
    bool digital_input_fmt_change;
    bool is_closing;
    bool is_callback_pending;
    void *mmap_audio_manager;
    int mmap_audio_client_id;
    int current_digital_audio_format;
    uint64_t audiomixer_sleep_start_us;
    int64_t audiomixer_sleep_time_us;
    bool audiomixer_standby;
    bool is_heaac_changed;
    bool is_ddp_offload_use_split;
    bool is_preempt_system_audio_usage_media_stream;
    int track_base_usage;
    bool is_system_audio_usage_media; //it's a system sound, and the audio usage is media
    bool enable_soundbar_mode;
    bool b_migrate_check;
    bool migrated_on_apu;
    bool b_priority_check;
    pthread_mutex_t dec_MutexLock;
    void *aml_parser;
    pthread_mutex_t parser_MutexLock;
    void *audio_buffer;
    void *parsedDataBuf;
    char nickname[32];

    bool is_ms12_main_decoder_disable;
    bool nts_volume_correction;

    stream_event_callback_t stream_event_callback;
    void *stream_cookie;

    //speed
    aml_stream_speed_info_t speed_info;
    struct dolby_ms12_dec_desc *ms12_dec_handle;
    aml_volume_shaper_t volume_shaper;
    aml_audio_ease_t volume_easing;
    bool first_volume_set;
    bool is_decoder_muted;
    int decoder_mute_duration;
    struct ring_buffer *input_cache_rbuffer;
    int input_cache_frames;
    int input_start_threshold;
    int64_t trace_last_write_time_ms;
    bool check_preempt_done;
    bool is_preempted;
};

#ifdef LOWPOWER_DSP_FFV
struct dsp_ffv_in {
    int fetch_size; /* in the suspend state, the size of data for detecting wake-up words */
    size_t fetched_size; /* record the amount of data obtained by the app from fetch_buffer */
    void* fetch_buffer; /* store keyword data recorded during pending status */
    int sound_trigger_handle; /* the handle returned by sound_trigger_open_for_streaming */
    uint64_t total_read; /* the total number of frames read on the DSP side */
    struct timespec ts; /* get the timestamp on the arm side */
};
#endif

#define MAX_PREPROCESSORS 3 /* maximum one AGC + one NS + one AEC per input stream */

struct tv_stream_param {
    hdmiin_audio_packet_t audio_packet_type;
    hdmiin_audio_packet_t cur_audio_packet_type;
    int read_mul_factor;
    bool is_HBR_stream;
    bool change_to_HBR_stream;
};

struct aml_stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by capture thread */
    struct pcm_config config;
    struct pcm *pcm;
    unsigned int device;
    audio_io_handle_t io_handle;
    audio_channel_mask_t hal_channel_mask;
    audio_format_t hal_format;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t frames_in;
    unsigned int requested_rate;
    uint32_t main_channels;
    bool standby;
    audio_source_t source;
    struct echo_reference_itfe *echo_reference;
    bool need_echo_reference;
    effect_handle_t preprocessors[MAX_PREPROCESSORS];
    int num_preprocessors;
    int16_t *proc_buf;
    size_t proc_buf_size;
    size_t proc_frames_in;
    int16_t *ref_buf;
    size_t ref_buf_size;
    size_t ref_frames_in;
    int read_status;
    /* SW parser audio format */
    audio_format_t spdif_fmt_sw;
    bool mute_flag;
    struct timespec mute_start_ts;
    int mute_log_cntr;
    int mute_mdelay;
    struct aml_audio_device *dev;
    void *input_tmp_buffer;
    size_t input_tmp_buffer_size;
    void *tmp_buffer_8ch;
    size_t tmp_buffer_8ch_size;
    unsigned int frames_read;
    uint64_t timestamp_nsec;
    bool is_tv_src_stream;
    aml_audio_resample_t *resample_handle;
    struct tv_stream_param tv_param;
};
typedef  int (*do_standby_func)(struct aml_stream_out *out);
typedef  int (*do_startup_func)(struct aml_stream_out *out);

inline int continuous_mode(struct aml_audio_device *adev)
{
    return adev->continuous_audio_mode;
}
inline bool direct_continuous(struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    if ((out->flags & AUDIO_OUTPUT_FLAG_DIRECT) && adev->continuous_audio_mode) {
        return true;
    } else {
        return false;
    }
}
inline bool primary_continuous(struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    if ((out->flags & AUDIO_OUTPUT_FLAG_PRIMARY) && adev->continuous_audio_mode) {
        return true;
    } else {
        return false;
    }
}

/* called when adev locked */
static inline int dolby_stream_active(struct aml_audio_device *adev)
{
    int i = 0;
    int is_dolby = 0;
    struct aml_stream_out *out = NULL;
    for (i = 0 ; i < STREAM_TYPE_MAX; i++) {
        out = adev->active_outputs[i];
        if (out && (out->hal_internal_format == AUDIO_FORMAT_AC3
            || out->hal_internal_format == AUDIO_FORMAT_E_AC3
            || out->hal_internal_format == AUDIO_FORMAT_DOLBY_TRUEHD
            || out->hal_internal_format == AUDIO_FORMAT_AC4
            || out->hal_internal_format == AUDIO_FORMAT_MAT
            || out->hal_internal_format == AUDIO_FORMAT_AAC
            || out->hal_internal_format == AUDIO_FORMAT_HE_AAC_V1
            || out->hal_internal_format == AUDIO_FORMAT_HE_AAC_V2
            || out->hal_internal_format == AUDIO_FORMAT_AAC_LATM)) {
            is_dolby = 1;
            break;
        }
    }
    return is_dolby;
}

/* called when adev locked */
static inline int dts_stream_active(struct aml_audio_device *adev)
{
    int i = 0;
    int is_dts = 0;
    struct aml_stream_out *out = NULL;
    for (i = 0 ; i < STREAM_TYPE_MAX; i++) {
        out = adev->active_outputs[i];
        if (out && (out->hal_internal_format == AUDIO_FORMAT_DTS
            || out->hal_internal_format == AUDIO_FORMAT_DTS_HD
            || out->hal_internal_format == AUDIO_FORMAT_DTS_UHD_P2)) {
            is_dts = 1;
            break;
        }
    }
    return is_dts;
}

static inline bool is_dts_stream(struct aml_stream_out *out)
{
    bool is_dts = false;
    switch (out->hal_internal_format) {
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD:
    case AUDIO_FORMAT_DTS_UHD_P2:
        is_dts = true;
        break;
    default :
        is_dts = false;
        break;
    }
    return is_dts;
}

static inline bool is_raw_stream(struct aml_stream_out *out)
{
    bool is_raw_format = false;
    switch (out->hal_format) {
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_AC4:
    case AUDIO_FORMAT_MAT:
    case AUDIO_FORMAT_DOLBY_TRUEHD:
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2:
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD:
    case AUDIO_FORMAT_DTS_UHD_P2:
    case AUDIO_FORMAT_IEC61937:
        is_raw_format = true;
        break;
    default :
        is_raw_format = false;
        break;
    }
    return is_raw_format;
}

//is_unsupport_raw_stream would be removed later, it's just for debug.
//currently not implement mat/truehd/aac parser.
static inline bool is_unsupport_raw_stream_for_debug(struct aml_stream_out *out)
{
    bool is_raw_format = false;
    if (out->hal_format == AUDIO_FORMAT_IEC61937) {
        switch (out->hal_internal_format) {
        case AUDIO_FORMAT_MAT:
        case AUDIO_FORMAT_DOLBY_TRUEHD:
        case AUDIO_FORMAT_AAC:
        case AUDIO_FORMAT_HE_AAC_V1:
        case AUDIO_FORMAT_HE_AAC_V2:
            is_raw_format = true;
            break;
        default :
            is_raw_format = false;
            break;
        }
    } else {
        switch (out->hal_format) {
        case AUDIO_FORMAT_MAT:
        case AUDIO_FORMAT_DOLBY_TRUEHD:
        case AUDIO_FORMAT_AAC:
        case AUDIO_FORMAT_HE_AAC_V1:
        case AUDIO_FORMAT_HE_AAC_V2:
        //case AUDIO_FORMAT_DTS:
        //case AUDIO_FORMAT_DTS_HD:
            is_raw_format = true;
            break;
        default :
            is_raw_format = false;
            break;
        }
    }
    return is_raw_format;
}


/* called when adev locked */
static inline int hwsync_lpcm_active(struct aml_audio_device *adev)
{
    int i = 0;
    int is_hwsync_lpcm = 0;
    struct aml_stream_out *out = NULL;
    for (i = 0 ; i < STREAM_TYPE_MAX; i++) {
        out = adev->active_outputs[i];
        if (out && audio_is_linear_pcm(out->hal_internal_format) && (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {
            is_hwsync_lpcm = 1;
            break;
        }
    }
    return is_hwsync_lpcm;
}

inline struct aml_stream_out *direct_active(struct aml_audio_device *adev)
{
    int i = 0;
    struct aml_stream_out *out = NULL;
    for (i = 0 ; i < STREAM_TYPE_MAX; i++) {
        out = adev->active_outputs[i];
        if (out && (out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
            return out;
        }
    }
    return NULL;
}

static inline bool is_bypass_submix_active(struct aml_audio_device *adev)
{
    int i = 0;
    struct aml_stream_out *out = NULL;
    for (i = 0 ; i < STREAM_TYPE_MAX; i++) {
        out = adev->active_outputs[i];
        if (out && (out->bypass_submix) && !out->pause_status) {
            return true;
        }
    }
    return false;
}

static inline void set_primary_out_format(struct aml_audio_device *adev, audio_format_t format)
{
    adev->primary_out_format = format;
}

static inline audio_format_t get_primary_out_format(struct aml_audio_device *adev)
{
    if (!adev->primary_out_format) {
        return adev->primary_out_format;
    }

    return adev->primary_out_format;
}


/*
 *@brief get_output_format get the output format always return the "sink_format" of adev
 */
audio_format_t get_output_format(struct audio_stream_out *stream);

bool dtv_tuner_framework(struct audio_stream_out *stream);
void aml_audio_output_routing(struct aml_audio_device *adev, audio_devices_t cur_output_device);

int do_output_standby_l(struct audio_stream *stream);

ssize_t out_write_new(struct audio_stream_out *stream,
                      const void *buffer,
                      size_t bytes);
int out_standby_new(struct audio_stream *stream);
//ssize_t mixer_aux_buffer_write(struct audio_stream_out *stream, const void *buffer,
//                               size_t bytes);
int dsp_process_output(struct aml_audio_device *adev, void *in_buffer,
                       size_t bytes);
int release_patch_l(struct aml_audio_device *adev);
enum hwsync_status check_hwsync_status (uint apts_gap);

void config_output(struct audio_stream_out *stream, bool reset_decoder);

int out_standby_direct (struct audio_stream *stream);

void *adev_get_handle();
/*
 *@brief get primary adev handle.
 */
void *aml_adev_get_handle(void);
int _get_stream_write_func(struct aml_stream_out *aml_out);

audio_format_t get_non_ms12_output_format(audio_format_t src_format, struct aml_audio_device *aml_dev);

int start_input_stream(struct aml_stream_in *in);

int do_input_standby (struct aml_stream_in *in);

int get_audio_patch_by_src_dev(struct audio_hw_device *dev, audio_devices_t dev_type, struct audio_patch **p_audio_patch);
int output_stream_hwsync_prepare(struct aml_stream_out *out, int hw_sync_id);
bool aml_get_speaker_mute_status(void);
/* timer callback function */
void aml_stream_timer_callback_handler(union sigval sigv);
bool is_dev_patch_valid(struct aml_audio_device *adev);

int adev_ms12_prepare(struct audio_hw_device *dev);

void adev_ms12_cleanup(struct audio_hw_device *dev);
void aml_close_ms12_output_main_stream(struct aml_stream_out *amlStream);

//add for HDMI code refine. TODO
int adev_open_input_stream(struct audio_hw_device *dev,
                                audio_io_handle_t handle __unused,
                                audio_devices_t devices,
                                struct audio_config *config,
                                struct audio_stream_in **stream_in,
                                audio_input_flags_t flags __unused,
                                const char *address,
                                audio_source_t source);
void adev_close_input_stream(struct audio_hw_device *dev, struct audio_stream_in *stream);
int adev_open_output_stream_new(struct audio_hw_device *dev,
                                audio_io_handle_t handle,
                                audio_devices_t devices,
                                audio_output_flags_t flags,
                                struct audio_config *config,
                                struct audio_stream_out **stream_out,
                                const char *address);
void adev_close_output_stream_new(struct audio_hw_device *dev, struct audio_stream_out *stream);

ssize_t mixer_aux_buffer_write(struct audio_stream_out *stream, void *abuffer);

/* 'bytes' are the number of bytes written to audio FIFO, for which 'timestamp' is valid.
 * 'available' is the number of frames available to read (for input) or yet to be played
 * (for output) frames in the PCM buffer.
 * timestamp and available are updated by pcm_get_htimestamp(), so they use the same
 * data types as the corresponding arguments to that function. */
struct aec_info {
    struct timespec timestamp;
    uint64_t timestamp_usec;
    unsigned int available;
    size_t bytes;
};
/* Capture codec parameters */
/* Set up a capture period of 32 ms:
 * CAPTURE_PERIOD = PERIOD_SIZE / SAMPLE_RATE, so (32e-3) = PERIOD_SIZE / (16e3)
 * => PERIOD_SIZE = 512 frames, where each "frame" consists of 1 sample of every channel (here, 2ch) */
#define CAPTURE_PERIOD_MULTIPLIER 16
#define CAPTURE_PERIOD_SIZE (CODEC_BASE_FRAME_COUNT * CAPTURE_PERIOD_MULTIPLIER)
#define CAPTURE_PERIOD_START_THRESHOLD 4
#define CAPTURE_CODEC_SAMPLING_RATE 16000

#ifdef ENABLE_AEC_APP
/* App AEC uses 2-channel reference */
#define NUM_AEC_REFERENCE_CHANNELS 2
#endif /* #ifdef ENABLE_AEC_FUNC */
#define CODEC_BASE_FRAME_COUNT 32
#define PLAYBACK_PERIOD_MULTIPLIER 32  /* 21 ms */
#define PLAYBACK_PERIOD_SIZE (CODEC_BASE_FRAME_COUNT * PLAYBACK_PERIOD_MULTIPLIER)
#define CHANNEL_STEREO 2
#define PLAYBACK_CODEC_SAMPLING_RATE 48000

static inline int16_t CLIP16(int r)
{
    return (r >  0x7fff) ? 0x7fff :
           (r < -0x8000) ? 0x8000 :
           r;
}

static inline int32_t CLIP32(int64_t r)
{
    return (r > INT32_MAX) ? INT32_MAX :
           (r < INT32_MIN) ? INT32_MIN :
           r;
}

enum pcm_format aml_pcm_format_from_audio_format(audio_format_t format);
audio_format_t aml_audio_format_from_pcm_format(enum pcm_format format);

#endif
