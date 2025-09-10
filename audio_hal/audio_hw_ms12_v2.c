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

#define LOG_TAG "audio_hw_hal_ms12v2"
//#define LOG_NDEBUG 0
#define __USE_GNU

#include <cutils/log.h>
#include <dolby_ms12.h>
#include <dolby_ms12_config_params.h>
#include <dolby_ms12_status.h>
#include <aml_android_utils.h>
#include <sys/prctl.h>
#include <cutils/properties.h>
#include <inttypes.h>
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>
#include <audio_utils/primitives.h>
#include <aml_android_utils.h>
#include <audio_utils/channels.h>

#include "audio_hw_ms12.h"
#include "alsa_config_parameters.h"
#include "aml_ac3_parser.h"
#include "audio_hw.h"
#include "alsa_manager.h"
#include "aml_audio_stream.h"
#include "dolby_lib_api.h"
#include "aml_audio_timer.h"
#include "audio_virtual_buf.h"
#include "ac3_parser_utils.h"
#include "aml_audio_ac3parser.h"
#include "audio_hw_utils.h"
#include "aml_audio_ms12_bypass.h"
#include "aml_audio_ac4parser.h"
#include "aml_volume_utils.h"
#include "aml_audio_spdifdec.h"
#include "aml_audio_matparser.h"
#include "aml_audio_spdifout.h"
#include "aml_dump_debug.h"
#include "aml_data_utils.h"
#include "aml_audio_ms12_sync.h"
#include "aml_malloc_debug.h"
#include "audio_hw_ms12_common.h"
#include "aml_audio_report.h"
#include "audio_hw_resource_mgr.h"
#include "audio_ms12_continuous_standby.h"
#include "aml_stream_manager.h"
#include "audio_hwsync_wrap.h"
#include "audio_ms12_continuous_standby.h"



#ifdef ENABLE_DVB_PATCH
#include "dtv_patch_dtvsync.h"
#include "dtv_patch.h"
#endif

#include "aml_audio_scaletempo.h"
#include "aml_audio_output.h"
#include "tv_patch_ctrl.h"
#include "audio_hw_resource_mgr.h"
#include "aml_audio_enhancement.h"

//dolby truehd parser
#include "aml_audio_truehdparser.h"
#include "tv_patch.h"

#define DDP_MAX_BUFFER_SIZE 2560//dolby ms12 input buffer threshold
#define CONVERT_ONEDB_TO_GAIN  1.122018f
#define MS12_MAIN_INPUT_BUF_PCM_NS         (64000000LL)
#define MS12_MAIN_INPUT_BUF_PCM_NS_TARGET         (128000000LL)
#define MS12_MAIN_INPUT_BUF_NONEPCM_NS     (160000000LL)
#define MS12_MAIN_INPUT_BUF_NS_UPTHRESHOLD (160000000LL)
#define MS12_MAIN_INPUT_BUF_NS_UPTHRESHOLD_AC4 (256000000LL)


#define MS12_SYS_INPUT_BUF_NS  (64000000LL)
#define MS12_DEEP_BUF_INPUT_BUF_NS  (64000000LL)  // 512 * 6 frames

#define NANO_SECOND_PER_SECOND 1000000000LL
#define NANO_SECOND_PER_MILLISECOND 1000000LL
#define MICRO_SECOND_PER_MILLISECOND 1000LL


#define CONVERT_NS_TO_48K_FRAME_NUM(ns)    (ns * 48 / NANO_SECOND_PER_MILLISECOND)
#define CONVERT_US_TO_48K_FRAME_NUM(us)    (us * 48 / MICRO_SECOND_PER_MILLISECOND)

#define MS12_MAIN_BUF_INCREASE_TIME_MS (1000)
#define MS12_SYS_BUF_INCREASE_TIME_MS (1000)
#define MS12_DEEP_BUF_INCREASE_TIME_MS (500)


#define MS12_PCM_FRAME_SIZE         (6144)
#define MS12_DD_FRAME_SIZE          (6144)
#define MS12_DDP_FRAME_SIZE         (24576)

#define DUMP_MS12_OUTPUT_SPEAKER_PCM     0x1
#define DUMP_MS12_OUTPUT_SPDIF_PCM       0x2
#define DUMP_MS12_OUTPUT_BITSTREAM       0x4
#define DUMP_MS12_OUTPUT_BITSTREAM2      0x8
#define DUMP_MS12_OUTPUT_BITSTREAM_MAT   0x10
#define DUMP_MS12_OUTPUT_BITSTREAM_MAT_WI_MLP   0x20
#define DUMP_MS12_OUTPUT_MC_PCM          0x40


#define DUMP_MS12_INPUT_MAIN             0x100
#define DUMP_MS12_INPUT_SYS              0x200
#define DUMP_MS12_INPUT_APP              0x400
#define DUMP_MS12_INPUT_ASSOCIATE        0x800
#define DUMP_MS12_INPUT_DEEP_BUF         0x1000
#define DUMP_MS12_CALLBACK_PROCESS       0x2000

#define AML_PARSED_TRUEHD_FILE           "/data/vendor/audiohal/aml_audio_parsed_truehd.raw"
#define MS12_OUTPUT_SPEAKER_PCM_FILE     "/data/vendor/audiohal/ms12_speaker_pcm.raw"
#define MS12_OUTPUT_SPDIF_PCM_FILE       "/data/vendor/audiohal/ms12_spdif_pcm.raw"
#define MS12_OUTPUT_MC_PCM_FILE          "/data/vendor/audiohal/ms12_mc_pcm.raw"
#define MS12_OUTPUT_BITSTREAM_FILE       "/data/vendor/audiohal/ms12_bitstream.raw"
#define MS12_OUTPUT_BITSTREAM2_FILE      "/data/vendor/audiohal/ms12_bitstream2.raw"
#define MS12_OUTPUT_BITSTREAM_MAT_FILE   "/data/vendor/audiohal/ms12_bitstream.mat"
#define MS12_OUTPUT_BITSTREAM_MAT_WI_MLP_FILE   "/data/vendor/audiohal/ms12_bitstream_wi_mlp.mat"

#define MS12_INPUT_SYS_PCM_FILE          "/data/vendor/audiohal/ms12_input_sys.pcm"
#define MS12_INPUT_DEEP_BUF_PCM_FILE     "/data/vendor/audiohal/ms12_input_deepbuf.pcm"
#define MS12_INPUT_SYS_MAIN_FILE         "/data/vendor/audiohal/ms12_input_main.raw"
#define MS12_INPUT_SYS_ASSOCIATE_FILE    "/data/vendor/audiohal/ms12_input_associate.raw"
#define MS12_INPUT_SYS_APP_FILE          "/data/vendor/audiohal/ms12_input_app.pcm"
#define MS12_INPUT_SYS_MAIN_IEC_FILE     "/data/vendor/audiohal/ms12_input_main_iec.raw"

#define MS12_CALLBACK_IN_FILE            "/data/vendor/audiohal/ms12_callback_in_stereo_f32.pcm"
#define MS12_CALLBACK_OUT_FILE           "/data/vendor/audiohal/ms12_callback_out_stereo_f32.pcm"


#define MS12_OUTPUT_5_1_DDP "vendor.media.audio.ms12.output.5_1_ddp"
#define MS12_TV_TUNING "vendor.media.audio.ms12.tv_tuning"

// Downmix Mode start
#define DOWNMIX_MODE_LtRt (0)
#define DOWNMIX_MODE_LoRo (1)

/* for HE-AAC, currently, it is not used at all. */
#define DOWNMIX_MODE_ARIB (2)

#define MS12_DOWNMIX_MODE_PROPERTY "vendor.media.audio.ms12.downmixmode"
// Downmix Mode end

#define MS12_MAIN_WRITE_RETIMES             (600)
#define MS12_ATMOS_TRANSITION_THRESHOLD     (3)

#define MS12_BYPASS_DROP_CNT                (5)  /*5 frames is about 150ms*/

#define ms12_to_adev(ms12_ptr)  (struct aml_audio_device *) (((char*) (ms12_ptr)) - offsetof(struct aml_audio_device, ms12))

#define DOLBY_MS12_AVSYNC_BEEP_DURATION (360)//ms, every 3s one beep
#define MILLISECOND_2_PTS (90) // 1ms = 90 (pts)

#define IEC61937_PAPB (0xf8724e1f)

#define DDP_FRAME_MAX_NUMBLK (6)

/*this enum should be same with ms12 lib*/
typedef enum {
    MS12_SYNC_AUDIO_UNKNOWN = 0,
    MS12_SYNC_AUDIO_NORMAL_OUTPUT,
    MS12_SYNC_AUDIO_DROP_PCM,
    MS12_SYNC_AUDIO_INSERT,
    MS12_SYNC_AUDIO_HOLD,
    MS12_SYNC_AUDIO_MUTE,
    MS12_SYNC_AUDIO_RESAMPLE,
    MS12_SYNC_AUDIO_ADJUST_CLOCK,
} MS12_Sync_Policy;

typedef struct Aml_MS12_SyncPolicy_s {
    MS12_Sync_Policy eSyncPolicy;
    int s32TagFrame;
    int s32CurFrame;
} Aml_MS12_SyncPolicy_t;


typedef struct Aml_MS12_Delay_s {
    unsigned int u32DelayFrame;
    unsigned long long u64DelayTimeStamp;
} Aml_MS12_Delay_t;

typedef struct Aml_MS12_TempoInfo_s {
    int s32SampleRate;
    int s32Channel;
    int s32InSampleSize;
    char *pu8InBuffer;
    unsigned int u32InBufferSize;
    int s32OutSampleSize;
    char *pu8OutBuffer;
    unsigned int u32OutBufferSize;
    float f32TempoSpeed;
    int s32InBufferAllocSize;
} Aml_MS12_TempoInfo_t;

static int ms12_update_decoded_info_process(struct audio_stream_out *stream, void *input_buffer, size_t input_bytes, int *ddp_1st_frame_size, int *ddp_1st_numblks);
static int ms12_decoder_volume_process(struct aml_stream_out *aml_out, Aml_MS12_ProcessInfo_t *pstProcessInfo);
static int ms12_decoder_sound_mode_process(struct aml_stream_out *aml_out, Aml_MS12_ProcessInfo_t *pstProcessInfo);
static void ms12_stream_config_apts_gap_easing(struct aml_stream_out *aml_out);


static const unsigned int ms12_muted_dd_raw[] = {
    0x8f6d770b, 0xffe13024,   0x92f4fc, 0x785502fc, 0x7f188661, 0x3e9fafce, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9,
     0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xfff7f97c, 0xf97cbe3a, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7,
     0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0xfcdfe7f3, 0xe7f3f9ea, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f,
     0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0xf37f9fcf, 0x9fcfe7ab, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c,
     0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xceff7d3e, 0x7c3e9faf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3,
     0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0x3afff7f9, 0x91383ee5, 0x10894422, 0xff9ea0f7, 0x8fc7e3d9, 0xdddddd1d,       0xdc,          0,          0,          0,          0,          0,
     0xbbbb3b00, 0xb66ddbb6, 0x6bcde7db, 0xafb5d65a, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e,     0xc0e7, 0x78bc0300, 0xbbbbe3f1,   0x80bbbb,          0,          0,          0,          0,          0,
     0x77070000, 0x6ddb7677, 0xf97cdbb6, 0xd65a6bad, 0xcfe7f3b5, 0xf97c3e9f, 0x9fcfe7f3, 0xcafb7c3e, 0x577fb903, 0x773c1e8f, 0x70777777,          0,          0,          0,          0,          0,
              0, 0xdbeeeeee, 0x6fdbb66d, 0x6bad359f, 0x7cbed65a, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3,          0, 0xc7e3f10e, 0xeeeeee8e,       0xee,          0,          0,          0,          0,
              0, 0xdddd1d00, 0xdbb66ddb, 0xb5e6f36d, 0xd75a6bad, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f,     0xe0f3, 0x3cde0100, 0xddddf178,   0xc0dddd,          0,          0,          0,          0,
              0, 0xbb030000, 0xb66dbbbb, 0x7cbe6ddb, 0x6badb5d6, 0xe7f3f95a, 0x7c3e9fcf, 0xcfe7f3f9,   0x7c3e9f, 0x3b000000,     0x7ec0, 0x3d41ef01, 0x8fc7b3ff, 0xbbbb3b1e,     0xb8bb,          0,
              0,          0,          0,          0, 0x77770000, 0xdbb66d77, 0x9acfb76d, 0x6badb5d6, 0xf97c3e5f, 0x9fcfe7f3, 0xf3f97c3e,   0x80cfe7, 0x78070000, 0x77c7e3f1,   0x777777,          0,
              0,          0,          0,          0,  0xe000000, 0xb6edeeee, 0xf9b66ddb, 0xb5d65af3, 0xcfe76bad, 0xf97c3e9f, 0x9fcfe7f3, 0xf7f97c3e, 0xfe720794, 0x783c1eaf, 0xeeeeeeee,       0xe0,
              0,          0,          0,          0,          0, 0xdddddd01, 0xb66ddbb6, 0x5a6b3edf, 0x7cadb5d6, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3,       0x3e, 0xc7e31d00, 0xdddd1d8f,     0xdcdd,
              0,          0,          0,          0,          0, 0xbb3b0000, 0x6ddbb6bb, 0xcde7dbb6, 0xb5d65a6b, 0x7c3e9faf, 0xcfe7f3f9, 0xf97c3e9f,   0xc0e7f3, 0xbc030000, 0xbbe3f178, 0x80bbbbbb,
              0,          0,          0,          0,          0,  0x7000000, 0xdb767777, 0x7cdbb66d, 0x5a6badf9, 0xe7f3b5d6, 0x7c3e9fcf, 0xcfe7f3f9, 0xf87c3e9f,          0,   0xfc8077, 0x82de0300,
     0x8f67ff7b, 0x77773c1e,   0x707777,          0,          0,          0,          0,          0, 0xee000000, 0x6ddbeeee, 0x9f6fdbb6, 0x5a6bad35, 0xf97cbed6, 0x9fcfe7f3, 0xf3f97c3e,   0x9fcfe7,
      0xe000000, 0x8ec7e3f1, 0xeeeeeeee,          0,          0,          0,          0,          0,          0, 0xdbdddd1d, 0x6ddbb66d, 0xadb5e6f3, 0xcfd75a6b, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e,
     0xe50e28ef, 0x783c5efd, 0xddddddf1,     0xc0dd,          0,          0,          0,          0,          0, 0xbbbb0300, 0xdbb66dbb, 0xd67cbe6d, 0x5a6badb5, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3,
         0x7c3e, 0xc73b0000, 0xbb3b1e8f,   0xb8bbbb,          0,          0,          0,          0,          0, 0x77000000, 0xb66d7777, 0xcfb76ddb, 0xadb5d69a, 0x7c3e5f6b, 0xcfe7f3f9, 0xf97c3e9f,
     0x80cfe7f3,  0x7000000, 0xc7e3f178, 0x77777777,          0,          0,          0,          0,          0,          0, 0xedeeee0e, 0xb66ddbb6, 0xd65af3f9, 0xe76badb5, 0x7c3e9fcf, 0xcfe7f3f9,
     0xf97c3e9f,       0xf0, 0xf801ef00, 0x38080000, 0x1fa03601, 0x2c15dfc7, 0xa1e00baf, 0x82de774b, 0x8f67ff7b, 0x77773c1e,   0x707777,          0,          0,          0,          0,          0,
     0xee000000, 0x6ddbeeee, 0x9f6fdbb6, 0x5a6bad35, 0xf97cbed6, 0x9fcfe7f3, 0xf3f97c3e,   0x9fcfe7,  0xe000000, 0x8ec7e3f1, 0xeeeeeeee,          0,          0,          0,          0,          0,
              0, 0xdbdddd1d, 0x6ddbb66d, 0xadb5e6f3, 0xcfd75a6b, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0xe50e28ef, 0x783c5efd, 0xddddddf1,     0xc0dd,          0,          0,          0,          0,
              0, 0xbbbb0300, 0xdbb66dbb, 0xd67cbe6d, 0x5a6badb5, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3,     0x7c3e, 0xc73b0000, 0xbb3b1e8f,   0xb8bbbb,          0,          0,          0,          0,
              0, 0x77000000, 0xb66d7777, 0xcfb76ddb, 0xadb5d69a, 0x7c3e5f6b, 0xcfe7f3f9, 0xf97c3e9f, 0x80cfe7f3,  0x7000000, 0xc7e3f178, 0x77777777,          0,          0,          0,          0,
              0,          0, 0xedeeee0e, 0xb66ddbb6, 0xd65af3f9, 0xe76badb5, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f,       0xf0, 0xf801ef00, 0x60090000, 0x593fbd00, 0x9fb871e9, 0xd7dab421, 0xc05f51d9,
     0x2205fedb, 0xb69081dd, 0x3cc496a1, 0x7a59fcef, 0x24127d7c,  0xaaccf0e, 0xe2ecb666, 0x6c96ed43, 0x6d5e3e62, 0xa20a5c81, 0xcb581169, 0xa60e1dd5, 0xf7e93981, 0x7aa42e35, 0xf107b2ac, 0x1cca8ea7,
     0xbdb07be5, 0x937d3f2a, 0xff7b82de, 0x3c1e8f67, 0x77777777,       0x70,          0,          0,          0,          0,          0, 0xeeeeee00, 0xdbb66ddb, 0xad359f6f, 0xbed65a6b, 0xe7f3f97c,
     0x7c3e9fcf, 0xcfe7f3f9,       0x9f, 0xe3f10e00, 0xeeee8ec7,     0xeeee,          0,          0,          0,          0,          0, 0xdd1d0000, 0xb66ddbdd, 0xe6f36ddb, 0x5a6badb5, 0x3e9fcfd7,
     0xe7f3f97c, 0x7c3e9fcf, 0x28eff3f9, 0x5efde50e, 0xddf1783c, 0xc0dddddd,          0,          0,          0,          0,          0,  0x3000000, 0x6dbbbbbb, 0xbe6ddbb6, 0xadb5d67c, 0xf3f95a6b,
     0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf,          0, 0x1e8fc73b, 0xbbbbbb3b,       0xb8,          0,          0,          0,          0,          0, 0x77777700, 0x6ddbb66d, 0xd69acfb7, 0x5f6badb5,
     0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c,     0x80cf, 0xf1780700, 0x7777c7e3,     0x7777,          0,          0,          0,          0,          0, 0xee0e0000, 0xdbb6edee, 0xf3f9b66d, 0xadb5d65a,
     0x9fcfe76b, 0xf3f97c3e, 0x3e9fcfe7,   0xf0f97c, 0xef000000,     0xf801, 0x2d035c09, 0xbb5bf290, 0x8ad7c43a, 0x58c3befb, 0xf3e7998a,  0xcfe1bb2, 0x6dca0229, 0xcc0908ba, 0xf77cf51b, 0xa2e4840d,
     0x8d017859, 0x809094b6, 0x3b5690eb, 0x710f31af, 0xf27834c8, 0x765b5cf5, 0xb96f6af9, 0x86761c8f, 0x95303075, 0xa65e6b76, 0x7cc18745, 0xd81947ad, 0x7b82de67, 0x1e8f67ff, 0x7777773c,     0x7077,
              0,          0,          0,          0,          0, 0xeeee0000, 0xb66ddbee, 0x359f6fdb, 0xd65a6bad, 0xf3f97cbe, 0x3e9fcfe7, 0xe7f3f97c,     0x9fcf, 0xf10e0000, 0xee8ec7e3,   0xeeeeee,
              0,          0,          0,          0,          0, 0x1d000000, 0x6ddbdddd, 0xf36ddbb6, 0x6badb5e6, 0x9fcfd75a, 0xf3f97c3e, 0x3e9fcfe7, 0xeff3f97c, 0xfde50e28, 0xf1783c5e, 0xdddddddd,
           0xc0,          0,          0,          0,          0,          0, 0xbbbbbb03, 0x6ddbb66d, 0xb5d67cbe, 0xf95a6bad, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7,       0x7c, 0x8fc73b00, 0xbbbb3b1e,
         0xb8bb,          0,          0,          0,          0,          0, 0x77770000, 0xdbb66d77, 0x9acfb76d, 0x6badb5d6, 0xf97c3e5f, 0x9fcfe7f3, 0xf3f97c3e,   0x80cfe7, 0x78070000, 0x77c7e3f1,
       0x777777,          0,          0,          0,          0,          0,  0xe000000, 0xb6edeeee, 0xf9b66ddb, 0xb5d65af3, 0xcfe76bad, 0xf97c3e9f, 0x9fcfe7f3, 0xf0f97c3e,          0, 0x685c00ef,
};

static const unsigned int ms12_muted_ddp_raw[] = {
    0xff04770b, 0xfaff673f, 0x40000049,  0x4000000,  0x8000000, 0x866100e1, 0x3aff6118, 0xf3f97cbe, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7,
     0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0xeafcdfe7, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f,
     0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xabf37f9f, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c,
     0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xafceff7d, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3,
     0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0xbe3afff7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf,
     0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x94ebfcdf, 0x82dee3f8, 0x8f67ff7b, 0x77773c1e,   0x707777,          0,          0,          0,          0,          0,
     0xee000000, 0x6ddbeeee, 0x9f6fdbb6, 0x5a6bad35, 0xf97cbed6, 0x9fcfe7f3, 0xf3f97c3e,   0x9fcfe7,  0xe000000, 0x8ec7e3f1, 0xeeeeeeee,          0,          0,          0,          0,          0,
              0, 0xdbdddd1d, 0x6ddbb66d, 0xadb5e6f3, 0xcfd75a6b, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0xe50e28ef, 0x783c5efd, 0xddddddf1,     0xc0dd,          0,          0,          0,          0,
              0, 0xbbbb0300, 0xdbb66dbb, 0xd67cbe6d, 0x5a6badb5, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3,     0x7c3e, 0xc73b0000, 0xbb3b1e8f,   0xb8bbbb,          0,          0,          0,          0,
              0, 0x77000000, 0xb66d7777, 0xcfb76ddb, 0xadb5d69a, 0x7c3e5f6b, 0xcfe7f3f9, 0xf97c3e9f, 0x80cfe7f3,  0x7000000, 0xc7e3f178, 0x77777777,          0,          0,          0,          0,
              0,          0, 0xedeeee0e, 0xb66ddbb6, 0xd65af3f9, 0xe76badb5, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f,       0xf0,  0x320ef00, 0xff7b82de, 0x3c1e8f67, 0x77777777,       0x70,          0,
              0,          0,          0,          0, 0xeeeeee00, 0xdbb66ddb, 0xad359f6f, 0xbed65a6b, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9,       0x9f, 0xe3f10e00, 0xeeee8ec7,     0xeeee,          0,
              0,          0,          0,          0, 0xdd1d0000, 0xb66ddbdd, 0xe6f36ddb, 0x5a6badb5, 0x3e9fcfd7, 0xe7f3f97c, 0x7c3e9fcf, 0x28eff3f9, 0x5efde50e, 0xddf1783c, 0xc0dddddd,          0,
              0,          0,          0,          0,  0x3000000, 0x6dbbbbbb, 0xbe6ddbb6, 0xadb5d67c, 0xf3f95a6b, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf,          0, 0x1e8fc73b, 0xbbbbbb3b,       0xb8,
              0,          0,          0,          0,          0, 0x77777700, 0x6ddbb66d, 0xd69acfb7, 0x5f6badb5, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c,     0x80cf, 0xf1780700, 0x7777c7e3,     0x7777,
              0,          0,          0,          0,          0, 0xee0e0000, 0xdbb6edee, 0xf3f9b66d, 0xadb5d65a, 0x9fcfe76b, 0xf3f97c3e, 0x3e9fcfe7,   0xf0f97c, 0xef000000, 0x82de0320, 0x8f67ff7b,
     0x77773c1e,   0x707777,          0,          0,          0,          0,          0, 0xee000000, 0x6ddbeeee, 0x9f6fdbb6, 0x5a6bad35, 0xf97cbed6, 0x9fcfe7f3, 0xf3f97c3e,   0x9fcfe7,  0xe000000,
     0x8ec7e3f1, 0xeeeeeeee,          0,          0,          0,          0,          0,          0, 0xdbdddd1d, 0x6ddbb66d, 0xadb5e6f3, 0xcfd75a6b, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0xe50e28ef,
     0x783c5efd, 0xddddddf1,     0xc0dd,          0,          0,          0,          0,          0, 0xbbbb0300, 0xdbb66dbb, 0xd67cbe6d, 0x5a6badb5, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3,     0x7c3e,
     0xc73b0000, 0xbb3b1e8f,   0xb8bbbb,          0,          0,          0,          0,          0, 0x77000000, 0xb66d7777, 0xcfb76ddb, 0xadb5d69a, 0x7c3e5f6b, 0xcfe7f3f9, 0xf97c3e9f, 0x80cfe7f3,
      0x7000000, 0xc7e3f178, 0x77777777,          0,          0,          0,          0,          0,          0, 0xedeeee0e, 0xb66ddbb6, 0xd65af3f9, 0xe76badb5, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f,
           0xf0,  0x320ef00, 0xff7b82de, 0x3c1e8f67, 0x77777777,       0x70,          0,          0,          0,          0,          0, 0xeeeeee00, 0xdbb66ddb, 0xad359f6f, 0xbed65a6b, 0xe7f3f97c,
     0x7c3e9fcf, 0xcfe7f3f9,       0x9f, 0xe3f10e00, 0xeeee8ec7,     0xeeee,          0,          0,          0,          0,          0, 0xdd1d0000, 0xb66ddbdd, 0xe6f36ddb, 0x5a6badb5, 0x3e9fcfd7,
     0xe7f3f97c, 0x7c3e9fcf, 0x28eff3f9, 0x5efde50e, 0xddf1783c, 0xc0dddddd,          0,          0,          0,          0,          0,  0x3000000, 0x6dbbbbbb, 0xbe6ddbb6, 0xadb5d67c, 0xf3f95a6b,
     0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf,          0, 0x1e8fc73b, 0xbbbbbb3b,       0xb8,          0,          0,          0,          0,          0, 0x77777700, 0x6ddbb66d, 0xd69acfb7, 0x5f6badb5,
     0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c,     0x80cf, 0xf1780700, 0x7777c7e3,     0x7777,          0,          0,          0,          0,          0, 0xee0e0000, 0xdbb6edee, 0xf3f9b66d, 0xadb5d65a,
     0x9fcfe76b, 0xf3f97c3e, 0x3e9fcfe7,   0xf0f97c, 0xef000000, 0x82de0320, 0x8f67ff7b, 0x77773c1e,   0x707777,          0,          0,          0,          0,          0, 0xee000000, 0x6ddbeeee,
     0x9f6fdbb6, 0x5a6bad35, 0xf97cbed6, 0x9fcfe7f3, 0xf3f97c3e,   0x9fcfe7,  0xe000000, 0x8ec7e3f1, 0xeeeeeeee,          0,          0,          0,          0,          0,          0, 0xdbdddd1d,
     0x6ddbb66d, 0xadb5e6f3, 0xcfd75a6b, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0xe50e28ef, 0x783c5efd, 0xddddddf1,     0xc0dd,          0,          0,          0,          0,          0, 0xbbbb0300,
     0xdbb66dbb, 0xd67cbe6d, 0x5a6badb5, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3,     0x7c3e, 0xc73b0000, 0xbb3b1e8f,   0xb8bbbb,          0,          0,          0,          0,          0, 0x77000000,
     0xb66d7777, 0xcfb76ddb, 0xadb5d69a, 0x7c3e5f6b, 0xcfe7f3f9, 0xf97c3e9f, 0x80cfe7f3,  0x7000000, 0xc7e3f178, 0x77777777,          0,          0,          0,          0,          0,          0,
     0xedeeee0e, 0xb66ddbb6, 0xd65af3f9, 0xe76badb5, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f,       0xf0,  0x320ef00, 0xff7b82de, 0x3c1e8f67, 0x77777777,       0x70,          0,          0,          0,
              0,          0, 0xeeeeee00, 0xdbb66ddb, 0xad359f6f, 0xbed65a6b, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9,       0x9f, 0xe3f10e00, 0xeeee8ec7,     0xeeee,          0,          0,          0,
              0,          0, 0xdd1d0000, 0xb66ddbdd, 0xe6f36ddb, 0x5a6badb5, 0x3e9fcfd7, 0xe7f3f97c, 0x7c3e9fcf, 0x28eff3f9, 0x5efde50e, 0xddf1783c, 0xc0dddddd,          0,          0,          0,
              0,          0,  0x3000000, 0x6dbbbbbb, 0xbe6ddbb6, 0xadb5d67c, 0xf3f95a6b, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf,          0, 0x1e8fc73b, 0xbbbbbb3b,       0xb8,          0,          0,
              0,          0,          0, 0x77777700, 0x6ddbb66d, 0xd69acfb7, 0x5f6badb5, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c,     0x80cf, 0xf1780700, 0x7777c7e3,     0x7777,          0,          0,
              0,          0,          0, 0xee0e0000, 0xdbb6edee, 0xf3f9b66d, 0xadb5d65a, 0x9fcfe76b, 0xf3f97c3e, 0x3e9fcfe7,   0xf0f97c, 0xef000000,          0,          0,          0,          0,
              0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,
              0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,
              0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,          0,  0x1000000, 0xa2101051,

};

static int nbytes_of_dolby_ms12_downmix_output_pcm_frame();
void ms12_do_dtv_sync(struct audio_stream_out *stream);
static void *dolby_ms12_threadloop(void *data);
static int correct_the_duration_by_align_the_mat_frame_header(char *data, size_t len);
static void update_ms12_focus_info(struct audio_stream_out *stream);
int ms12_content_process_callback(void *priv_data, void *info);

static int get_ms12_dump_enable(int dump_type) {
    int value = 0;
    value = get_debug_value(AML_DUMP_AUDIOHAL_MS12);
    return (value & dump_type);
}

unsigned int get_ms12_buffer_latency(struct aml_stream_out *out)
{
    unsigned int ms12_latency = 0;
    ALOGV("%s, flags:0x%x, format:0x%x", __func__, out->flags, out->hal_internal_format);
    if (is_dolby_ms12_support_compression_format(out->hal_internal_format)) {
        ms12_latency = MS12_MAIN_INPUT_BUF_NONEPCM_NS / (1000*1000);
    } else if (out->hal_internal_format & AUDIO_FORMAT_PCM_16_BIT) {
        if (out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC) {
            ms12_latency = MS12_MAIN_INPUT_BUF_PCM_NS / (1000*1000);
        } else {
            ms12_latency = MS12_SYS_INPUT_BUF_NS / (1000*1000);
        }
    } else {
        // do nothing.
    }

    return ms12_latency;
}

static void ms12_spdif_encoder(void * in_buf, int in_size, audio_format_t output_format, void *out_buf, int * out_size) {
    uint16_t iec61937_pa = 0xf872;
    uint16_t iec61937_pb = 0x4e1f;
    uint16_t iec61937_pc = 0;
    uint16_t iec61937_pd = 0;
    uint16_t preamble[4] = { 0 };
    int offset  = 0;
    uint8_t * in_data = (uint8_t *)in_buf;

    if (in_size <= 0) {
        *out_size = 0;
        return;
    }

    if (output_format == AUDIO_FORMAT_AC3) {
        iec61937_pc = 0x1;
        *out_size   = MS12_DD_FRAME_SIZE;
        iec61937_pd = in_size << 3;

    } else if (output_format == AUDIO_FORMAT_E_AC3) {
        iec61937_pc = 0x15;
        *out_size   = MS12_DDP_FRAME_SIZE;
        iec61937_pd = in_size;
    } else {
        *out_size = 0;
        return;
    }

    preamble[0] = iec61937_pa;
    preamble[1] = iec61937_pb;
    preamble[2] = iec61937_pc;
    preamble[3] = iec61937_pd;

    memset(out_buf, 0, *out_size);
    memcpy(out_buf, (void *)preamble, sizeof(preamble));
    offset += sizeof(preamble);
    memcpy((char *)out_buf + offset, in_buf, in_size);

    if (in_data[0] == 0x0b && in_data[1] == 0x77) {
        endian16_convert((char *)out_buf + offset, in_size);
        /*if we want little endian, we can use below code*/
        //endian16_convert(preamble, sizeof(preamble));
    }


    return;
}

/*
 *@brief dump ms12 output data
 */
static void dump_ms12_output_data(void *buffer, int size, char *file_name)
{
    aml_dump_audio_bitstreams(file_name, buffer, size);
}

static int get_ms12_output_config(audio_format_t format)
{
    if (format == AUDIO_FORMAT_AC3)
        return MS12_OUTPUT_MASK_DD;
    else if (format == AUDIO_FORMAT_E_AC3)
        return MS12_OUTPUT_MASK_DDP;
    else if (format == AUDIO_FORMAT_MAT)
        return MS12_OUTPUT_MASK_MAT;
    else
        return MS12_OUTPUT_MASK_SPEAKER;
}

static void set_ms12_out_ddp_5_1(audio_format_t input_format, bool is_sink_supported_ddp_atmos)
{
    /*In case of AC-4 or Dolby Digital Plus input, set legacy ddp out ON/OFF*/
    ALOGD("%s input_format %#x is_sink_supported_ddp_atmos %d", __func__, input_format, is_sink_supported_ddp_atmos);
    bool is_ddp = (input_format == AUDIO_FORMAT_AC3) || (input_format == AUDIO_FORMAT_E_AC3);
    bool is_ac4 = (input_format == AUDIO_FORMAT_AC4);
    if (is_ddp || is_ac4) {
        bool is_out_ddp_5_1 = !is_sink_supported_ddp_atmos;
        /*
         *case1 support ddp atmos(is_out_ddp_5_1 as false), MS12 should output Dolby Atmos as 5.1.2
         *case2 only support ddp(is_out_ddp_5_1 as true), MS12 should downmix Atmos signals rendered from 5.1.2 to 5.1
         *It only effect the DDP-Atmos(5.1.2) output.
         *1.ATMOS INPUT, the ddp encoder output will output DDP-ATMOS
         *2.None-Atmos and Continuous with -atmos_lock=1,the ddp encoder output will output DDP-ATMOS.
         */
        dolby_ms12_set_ddp_5_1_out(is_out_ddp_5_1);
    }
}

bool is_platform_supported_ddp_atmos(bool atmos_supported, audio_devices_t cur_out_devices, bool is_tv)
{
    bool ret = false;
    //ALOGD("%s atmos_supported %d current_out_port %d", __func__, atmos_supported, current_out_port);
    if ((cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) != 0 || (cur_out_devices & AUDIO_DEVICE_OUT_HDMI) != 0) {
        /*ARC case*/
        ret = atmos_supported;
    }
    else {
        if (is_tv) {
            /*SPEAKER/HEADPHONE case*/
            ret = true;
        }
        else {
            /*through it is speaker output, but the sink device is connected with atmos*/
            if (atmos_supported) {
                ret = true;
            } else {
                /* OTT CVBS case */
                ret = false;
            }
        }
    }
    //ALOGD("%s Line %d return %s", __func__, __LINE__, ret ? "true": "false");
    return ret;
}

bool is_ms12_out_ddp_5_1_suitable(bool is_ddp_atmos)
{
    bool is_ms12_out_ddp_5_1 = dolby_ms12_get_ddp_5_1_out();
    bool is_sink_only_ddp_5_1 = !is_ddp_atmos;

    if (is_ms12_out_ddp_5_1 == is_sink_only_ddp_5_1) {
        /*
         *Sink device can decode MS12 DDP bitstream correctly
         *case1 Sink device Support DDP ATMOS, MS12 output DDP-ATMOS(5.1.2)
         *case2 Sink device Support DDP, MS12 output DDP(5.1)
         */
        return true;
    } else {
        /*
         *case1 Sink device support DDP ATMOS, but MS12 output DDP(5.1)
         *case2 Sink device support DDP 5.1, but MS12 output DDP-ATMOS(5.1.2)
         *should reconfig the parameter about MS12 output DDP bitstream.
         */
        return false;
    }
}

/*
 *@brief get ms12 output configure mask
 */
static int get_ms12_output_mask(audio_format_t sink_format,audio_format_t  optical_format,bool is_arc)
{
    int  output_config;
    if (sink_format == AUDIO_FORMAT_E_AC3) /* ARC with DDP Sink-cap */
        output_config = MS12_OUTPUT_MASK_DD | MS12_OUTPUT_MASK_DDP;
    else if (sink_format == AUDIO_FORMAT_AC3) /* ARC with DD Sink-cap */
        output_config = MS12_OUTPUT_MASK_DD;
    else if (sink_format == AUDIO_FORMAT_MAT) /* E-ARC with DD Sink-cap */
        output_config = MS12_OUTPUT_MASK_STEREO | MS12_OUTPUT_MASK_MAT;
    else if (sink_format == AUDIO_FORMAT_PCM_16_BIT && optical_format == AUDIO_FORMAT_AC3) /* Speaker or Optical Sink */
        output_config = MS12_OUTPUT_MASK_DD | MS12_OUTPUT_MASK_SPEAKER | MS12_OUTPUT_MASK_STEREO;
    else if (is_arc) {
        /* ARC with PCM Sink-cap */
        output_config = MS12_OUTPUT_MASK_STEREO;
    }
    else
        output_config = MS12_OUTPUT_MASK_SPEAKER | MS12_OUTPUT_MASK_STEREO;
    return output_config;
}

inline bool is_mpeg_lay2or3_audio(audio_format_t hal_format)
{
    return (hal_format == AUDIO_FORMAT_MP2 || hal_format == AUDIO_FORMAT_MP3);
}

audio_format_t ms12_get_audio_hal_format(audio_format_t hal_format)
{
    if (hal_format == AUDIO_FORMAT_E_AC3_JOC) {
        return AUDIO_FORMAT_E_AC3;
    } else if (is_mpeg_lay2or3_audio(hal_format) || hal_format == AUDIO_FORMAT_DRA) {
        return AUDIO_FORMAT_PCM_16_BIT;
    } else {
        if (hal_format == AUDIO_FORMAT_HE_AAC_V1 ||
            hal_format == AUDIO_FORMAT_HE_AAC_V2 ||
            hal_format == AUDIO_FORMAT_AAC ||
            hal_format == AUDIO_FORMAT_AAC_LATM)  {
            if (!property_get_bool("ro.vendor.audio.use.ms12heaac", true)) {
                return AUDIO_FORMAT_PCM_16_BIT;
            }
        }
        return hal_format;
    }
}

static void update_ms12_atmos_info(struct audio_stream_out *stream) {
    int is_atmos = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
#if 0

    is_atmos = (dolby_ms12_get_input_atmos_info() == 1);
    aml_ms12_decoder_getparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_ATMOS_PRESENT, &is_atmos, sizeof(is_atmos));
    ms12_dec->is_dolby_atmos = is_atmos;

    ALOGV("atmos =%d",ms12_dec->is_dolby_atmos);
#endif
    return;
}

static void update_ms12_focus_info(struct audio_stream_out *stream) {
    int is_atmos = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;

    ms12->focus_audioformat = ms12_dec->codec_info.u32AudioFormat;
    ms12->focus_is_bypass_ms12 = ms12_dec->is_bypass_ms12;
    ms12->focus_is_dolby_atmos = ms12_dec->is_dolby_atmos;
    ms12->focus_is_paused = ms12_dec->is_paused;

    return;
}

void set_ms12_ad_mixing_enable(struct audio_stream_out *stream, int ad_mixing_enable)
{// -xa
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_XA, &ad_mixing_enable, sizeof(ad_mixing_enable));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set ad mixing_enable to %d. ret %d",
             stream, aml_out->ms12_dec_handle, ad_mixing_enable, ret);
}

void set_ms12_ad_mixing_level(struct audio_stream_out *stream, int mixing_level)
{// -xu
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_XU, &mixing_level, sizeof(mixing_level));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set ad mixing_level to %d. ret %d",
             stream, aml_out->ms12_dec_handle, mixing_level, ret);
}

void set_ms12_ad_vol(struct audio_stream_out *stream, int ad_vol)
{// -main2_mixgain
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    /*ad_vol is 0~100*/
    float gain = (float)ad_vol / 100;
    int gain_db = (int)(128 * AmplToDb(gain));
    int vol_param[3];
    /*target gain at end of ramp in 1/128 dB (range: -12288..0)*/
    gain_db = gain_db > 0 ? 0 : gain_db;
    gain_db = gain_db < -12288 ? -12288 : gain_db;
    vol_param[0] = gain_db;//target gain at end of ramp in dB (range: -12288..0)
    vol_param[1] = 10;     //duration of ramp in milliseconds (range: 0..60000)
    vol_param[2] = 0;      //shape of the ramp (0: linear, 1: in cube, 2: out cube)
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_MAIN2_MIXGAIN, vol_param, sizeof(vol_param));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set AD gain %ddB,%dms,shape:%d. ret %d",
             stream, aml_out->ms12_dec_handle, vol_param[0] , vol_param[1], vol_param[2], ret);
}


void set_dolby_ms12_runtime_system_mixing_enable(struct dolby_ms12_desc *ms12, int system_mixing_enable)
{
    char parm[12] = "";
    sprintf(parm, "%s %d", "-xs", system_mixing_enable);
    if ((strlen(parm) > 0) && ms12) {
        aml_ms12_update_runtime_params(ms12, parm);
    }
}

void set_ms12_atmos_lock(struct dolby_ms12_desc *ms12, bool is_atmos_lock_on)
{
    char parm[64] = "";
    sprintf(parm, "%s %d", "-atmos_lock", is_atmos_lock_on);
    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
    audio_continuous_standby_reset(ms12->continuous_standby_handle);
}

void set_ms12_acmod2ch_lock(struct dolby_ms12_desc *ms12, bool is_lock_on)
{
    char parm[64] = "";
    bool output_5_1_ddp = getprop_bool(MS12_OUTPUT_5_1_DDP);
    ALOGI("%s output_5_1_ddp %d", __func__, output_5_1_ddp);

    if (output_5_1_ddp)
        is_lock_on = false;

    sprintf(parm, "%s %d", "-acmod2ch_lock", is_lock_on);
    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}

void set_ms12_chmod_lock(struct dolby_ms12_desc *ms12, bool is_lock_on)
{
    char parm[64] = "";

    sprintf(parm, "%s %d", "-chmod_locking", is_lock_on);
    if ((strlen(parm)) > 0 && ms12) {
        dolby_ms12_set_encoder_channel_mode_locking_mode(is_lock_on);
        aml_ms12_update_runtime_params(ms12, parm);
    }
    audio_continuous_standby_reset(ms12->continuous_standby_handle);
}

void set_ms12_mch_enable(struct dolby_ms12_desc *ms12, bool enable)
{
    char parm[64] = "";

    sprintf(parm, "%s %d", "-mch_enable", enable);
    if ((strlen(parm)) > 0 && ms12) {
        aml_ms12_update_runtime_params(ms12, parm);
    }
}

void set_ms12_main_volume(struct dolby_ms12_desc *ms12, float volume) {
    //if (fabs(ms12->main_volume - volume) > 1e-06) {
        dolby_ms12_set_main_volume(volume);
        ms12->main_volume = volume;
        //ALOGI("%s line %d main_volume %f\n", __func__, __LINE__, ms12->main_volume);
    //}
}

int set_ms12_fadein_max_detect_time_ms(int time_ms)
{
    return dolby_ms12_set_fadein_max_detect_time_ms(time_ms);
}

void set_ms12_ac4_presentation_group_index(struct audio_stream_out *stream, int index)
{// -ac4_pres_group_idx
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_AC4_PRES_GROUP_IDX, &index, sizeof(index));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set group index to %d. ret %d",
             stream, aml_out->ms12_dec_handle, index, ret);
}

void set_ms12_ac4_1st_preferred_language_code(struct audio_stream_out *stream, char *lang_iso639_code)
{// -lang
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_AC4_LANG, lang_iso639_code, sizeof(char) * 4);
    }
    ALOGI("%s line %d %c%c%C\n", __func__, __LINE__, lang_iso639_code[0], lang_iso639_code[1], lang_iso639_code[2]);
    ALOGI("stream:%p ms12_dec_handle:%p. ret %d", stream, aml_out->ms12_dec_handle, ret);
}

void set_ms12_ac4_2nd_preferred_language_code(struct audio_stream_out *stream, char *lang_iso639_code)
{// -lang2
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_AC4_LANG2, lang_iso639_code, sizeof(char) * 4);
    }
    ALOGI("%s line %d %c%c%C\n", __func__, __LINE__, lang_iso639_code[0], lang_iso639_code[1], lang_iso639_code[2]);
    ALOGI("stream:%p ms12_dec_handle:%p. ret %d", stream, aml_out->ms12_dec_handle, ret);
}

void set_ms12_ac4_prefer_presentation_selection_by_associated_type_over_language(struct audio_stream_out *stream, int prefer_selection_type)
{// -pat
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_PAT, &prefer_selection_type, sizeof(prefer_selection_type));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set pat to %d. ret %d",
             stream, aml_out->ms12_dec_handle, prefer_selection_type, ret);
}

void set_ms12_ac4_short_prog_identifier(struct audio_stream_out *stream, int short_program_identifier)
{// -ac4_short_prog_id
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_AC4_SHORT_PROG_ID, &short_program_identifier, sizeof(short_program_identifier));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set short_prog_id to %d. ret %d",
             stream, aml_out->ms12_dec_handle, short_program_identifier, ret);
}

void set_ms12_ac4_preferred_associated_type(struct audio_stream_out *stream, int associated_type)
{// -at
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_AT, &associated_type, sizeof(associated_type));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set associated_type to %d. ret %d",
             stream, aml_out->ms12_dec_handle, associated_type, ret);
}

void set_ms12_ac4_dialogue_enhancement(struct audio_stream_out *stream, int ac4_de)
{// -ac4_de
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_AC4_DE, &ac4_de, sizeof(ac4_de));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set ac4_de to %d. ret %d",
             stream, aml_out->ms12_dec_handle, ac4_de, ret);
}

void set_ms12_content_volume_leveler(struct audio_stream_out *stream, int* volume_leveler)
{// -dap_leveler
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_CONTENT_VOLUME_LEVELER, volume_leveler, 2 * sizeof(int));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set volume_leveler to %d,%d. ret %d",
             stream, aml_out->ms12_dec_handle, volume_leveler[0], volume_leveler[1], ret);
}

void set_ms12_content_dialogue_enhancer(struct audio_stream_out *stream, int* content_de)
{// -dap_dialogue_enhancer
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_CONTENT_DIALOGUE_ENHANCER, content_de, 2 * sizeof(int));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set content_de to %d,%d. ret %d",
             stream, aml_out->ms12_dec_handle, content_de[0], content_de[1], ret);
}

void set_ms12_decoder_sleep_time(struct audio_stream_out *stream, int time_us)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;

    if (time_us <= 0 && time_us > 10*1000) {
        ALOGE("%s : invalid time_us %d, use default value 1000", __func__, time_us);
        time_us = 1000;
    }

    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_SLEEP_TIME_US, &time_us, sizeof(int));
    }
    ALOGI("stream:%p ms12_dec_handle:%p set sleeptime us %d. ret %d",
             stream, aml_out->ms12_dec_handle, time_us, ret);
}

void set_ms12_decoder_parameters(struct aml_audio_device *adev, char * parm)
{
    char *saveptr = NULL;
    char *string_tok = strtok_r(parm, "-", &saveptr);
    struct dolby_ms12_desc *ms12_desc = &(adev->ms12);
    struct aml_stream_out * stream = NULL;

    while (string_tok) {
        if (string_tok) {
            ALOGI("%s() cmd =%s", __func__, string_tok);
        }
        if (strstr(string_tok, "ac4_de")) {
            int ac4_de = atoi(string_tok+7);
            ms12_desc->ac4_de = ac4_de;
            AM_LOGI("ac4_de=%d", ac4_de);
            for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
                stream = adev->active_outputs[i];
                if (stream && stream->is_ms12_main_decoder && (stream->hal_internal_format == AUDIO_FORMAT_AC4)) {
                    set_ms12_ac4_dialogue_enhancement((struct audio_stream_out *)stream, ac4_de);
                }
            }
        } else if (strstr(string_tok, "lang2")) {
            strncpy(ms12_desc->lang2, string_tok+6, 2);
            AM_LOGI("2nd lang=%s", ms12_desc->lang2);
            for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
                stream = adev->active_outputs[i];
                if (stream && stream->is_ms12_main_decoder && (stream->hal_internal_format == AUDIO_FORMAT_AC4)) {
                    set_ms12_ac4_2nd_preferred_language_code((struct audio_stream_out *)stream, ms12_desc->lang2);
                }
            }
        } else if (strstr(string_tok, "lang")) {
            strncpy(ms12_desc->lang, string_tok+5, 2);
            AM_LOGI("1st lang=%s", ms12_desc->lang);
            for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
                stream = adev->active_outputs[i];
                if (stream && stream->is_ms12_main_decoder && (stream->hal_internal_format == AUDIO_FORMAT_AC4)) {
                    set_ms12_ac4_1st_preferred_language_code((struct audio_stream_out *)stream, ms12_desc->lang);
                }
            }
        } else if (strstr(string_tok, "pat")) {
            int prefer_presentation_selection = atoi(string_tok+4);
            ms12_desc->pat = prefer_presentation_selection;
            AM_LOGI("prefer_selection_type=%d", prefer_presentation_selection);
            for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
                stream = adev->active_outputs[i];
                if (stream && stream->is_ms12_main_decoder && (stream->hal_internal_format == AUDIO_FORMAT_AC4)) {
                    set_ms12_ac4_prefer_presentation_selection_by_associated_type_over_language((struct audio_stream_out *)stream, prefer_presentation_selection);
                }
            }
        } else if (strstr(string_tok, "at")) {
            int ad_type = atoi(string_tok+3);
            ms12_desc->at = ad_type;
            AM_LOGI("ad_type=%d", ad_type);
            for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
                stream = adev->active_outputs[i];
                if (stream && stream->is_ms12_main_decoder && (stream->hal_internal_format == AUDIO_FORMAT_AC4)) {
                    set_ms12_ac4_preferred_associated_type((struct audio_stream_out *)stream, ad_type);
                }
            }
        } else if (strstr(string_tok, "dap_dialogue_enhancer")) {
            int dap_de_enable = atoi(string_tok+22);
            int dap_de_amount = atoi(string_tok+24);
            ms12_desc->dap_dialogue_enhancer[0] = dap_de_enable;
            ms12_desc->dap_dialogue_enhancer[1] = dap_de_amount;
            ALOGI("dap_dialogue_enhancer=%d,%d", dap_de_enable, dap_de_amount);
            for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
                stream = adev->active_outputs[i];
                if (stream && stream->is_ms12_main_decoder) {
                    set_ms12_content_dialogue_enhancer((struct audio_stream_out *)stream, ms12_desc->dap_dialogue_enhancer);
                }
            }
        } else if (strstr(string_tok, "dap_leveler")) {
            int dap_leveler_setting = atoi(string_tok+12);
            int dap_leveler_amount = atoi(string_tok+14);
            ms12_desc->dap_leveler[0] = dap_leveler_setting;
            ms12_desc->dap_leveler[1] = dap_leveler_amount;
            ALOGI("dap_leveler=%d,%d", dap_leveler_setting, dap_leveler_amount);
            for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
                stream = adev->active_outputs[i];
                if (stream && stream->is_ms12_main_decoder) {
                    set_ms12_content_volume_leveler((struct audio_stream_out *)stream, ms12_desc->dap_leveler);
                }
            }
        }
        string_tok = strtok_r(NULL, "-", &saveptr);
    }
    return;
}


static inline alsa_device_t usecase_device_adapter_with_ms12(alsa_device_t usecase_device, audio_format_t output_format)
{
    ALOGI("%s usecase_device %d output_format %#x", __func__, usecase_device, output_format);
    switch (usecase_device) {
    case DIGITAL_DEVICE:
    case I2S_DEVICE:
        if ((output_format == AUDIO_FORMAT_AC3) || (output_format == AUDIO_FORMAT_E_AC3)
            || (output_format == AUDIO_FORMAT_MAT) ) {
            return DIGITAL_DEVICE;
        } else {
            return I2S_DEVICE;
        }
    default:
        return I2S_DEVICE;
    }
}


void set_ms12_dap_postgain(struct dolby_ms12_desc *ms12, int postgain)
{
    char parm[64] = "";
    sprintf(parm, "%s %d", "-dap_gains", postgain);

    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}

void set_ms12_fade_pan
    (struct audio_stream_out *stream
    , int fade_byte
    , int gain_byte_center
    , int gain_byte_front
    , int gain_byte_surround
    , int pan_byte
    )
{// -ad_fade_pan
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int fade_pan_parm[5];

    if (adev->debug_flag)
        ALOGI("%s fade_byte %d gain_byte_center %d gain_byte_front %d gain_byte_surround %d pan_byte %d",
        __func__, fade_byte, gain_byte_center, gain_byte_front, gain_byte_surround, pan_byte);

    fade_pan_parm[0] = fade_byte;
    fade_pan_parm[1] = gain_byte_center;
    fade_pan_parm[2] = gain_byte_front;
    fade_pan_parm[3] = gain_byte_surround;
    fade_pan_parm[4] = pan_byte;
    if (ms12 && aml_out->ms12_dec_handle) {
        aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_AD_FADE_PAN, fade_pan_parm, sizeof(fade_pan_parm));
    }
}


void set_ms12_main_audio_pts(struct dolby_ms12_desc *ms12, uint64_t apts, uint64_t bytes_offset)
{
    char parm[64] = "";
    uint32_t apts_high32b = (uint32_t)(apts>>32);
    uint32_t apts_low32b = (uint32_t)(apts&UINT_MAX);
    uint32_t offset_high32b = (uint32_t)(bytes_offset>>32);
    uint32_t offset_low32b = (uint32_t)(bytes_offset&UINT_MAX);
    struct aml_audio_device *adev = (struct aml_audio_device *)adev_get_handle();

    sprintf(parm, "%s %u,%u,%u,%u", "-main_audio_pts", apts_high32b, apts_low32b, offset_high32b, offset_low32b);
    if (adev->debug_flag)
        ALOGI("%s offset =0x%" PRIx64 " pts =0x%" PRIx64 " high =0x%x low =0x%x", __func__, bytes_offset, apts, apts_high32b, apts_low32b);
    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}

void set_ms12_main1_audio_pts(struct dolby_ms12_desc *ms12, uint64_t apts, uint64_t bytes_offset)
{
    char parm[64] = "";
    uint32_t apts_high32b = (uint32_t)(apts>>32);
    uint32_t apts_low32b = (uint32_t)(apts&UINT_MAX);
    uint32_t offset_high32b = (uint32_t)(bytes_offset>>32);
    uint32_t offset_low32b = (uint32_t)(bytes_offset&UINT_MAX);
    sprintf(parm, "%s %u,%u,%u,%u", "-main1_audio_pts", apts_high32b,apts_low32b, offset_high32b, offset_low32b);
    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}

void set_ms12_is_dtg_case(struct dolby_ms12_desc *ms12, int is_dtg_case)
{
    char parm[64] = "";
    struct aml_audio_device *adev = (struct aml_audio_device *)adev_get_handle();

    snprintf(parm, sizeof(parm), "%s %d", "-b_dtg_case", is_dtg_case);

    if (adev->debug_flag)
        ALOGI("%s b_dtg_case %d", __func__, is_dtg_case);

    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}



int get_ms12_mat_dec_delay() {
    return dolby_ms12_get_mat_dec_latency();
}

int get_ms12_codec_format_info(struct audio_stream_out *stream, struct codec_format_info *codec_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);

    if (!ms12 || !codec_format) {
        ALOGE("-%s() pointer error ms12 %p codec_format %p", __FUNCTION__, ms12, codec_format);
        return -EINVAL;
    }
    codec_format->encoding_format = aml_out->hal_internal_format;
    codec_format->channel_mask = aml_out->hal_channel_mask;
    codec_format->sampe_rate = aml_out->hal_rate;
    bool is_aac_format = ((codec_format->encoding_format == AUDIO_FORMAT_AAC) || \
                            (codec_format->encoding_format == AUDIO_FORMAT_AAC_LATM) || \
                            (codec_format->encoding_format == AUDIO_FORMAT_HE_AAC_V1) || \
                            (codec_format->encoding_format == AUDIO_FORMAT_HE_AAC_V2));
    if (is_aac_format) {
        //int aac_profile = dolby_ms12_get_aac_profile();
        Aml_MS12_DecInfo_t dec_info;
        memset(&dec_info, 0x00, sizeof(dec_info));
        if (0 != get_ms12_main_dec_info(stream, &dec_info)) {
            ALOGE("get_ms12_main_dec_info fail");
        } else {
            int aac_profile = dec_info.s32AacProfile;
            if (adev->debug_flag)
                ALOGI("aac_profile %d",aac_profile);
            if (aac_profile == AAC_PROFILE_LC) {
                codec_format->encoding_format = AUDIO_FORMAT_AAC_LC;
            } else if (aac_profile == AAC_PROFILE_HEAAC_V1) {
                codec_format->encoding_format = AUDIO_FORMAT_AAC_HE_V1;
            } else if (aac_profile == AAC_PROFILE_HEAAC_V2) {
                codec_format->encoding_format = AUDIO_FORMAT_AAC_HE_V2;
            }
        }
    }
    if (adev->debug_flag)
        ALOGD("encoding_format %0x channel_mask %0x codec_format->sampe_rate %d",
        codec_format->encoding_format, codec_format->channel_mask, codec_format->sampe_rate);
    return 0;
}

int get_ms12_main_dec_info(struct audio_stream_out *stream, Aml_MS12_DecInfo_t *dec_info)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (aml_out && aml_out->ms12_dec_handle && dec_info) {
        ret = aml_ms12_decoder_getparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_DEC_INFO, dec_info, sizeof(Aml_MS12_DecInfo_t));
        if (adev->debug_flag) {
            ALOGI("stream:%p ms12_dec_handle:%p ret:%d sr:%d acmod:%d lfe:%d aac_profile:%d", stream, aml_out->ms12_dec_handle, ret, dec_info->s32SampleRate,
                dec_info->s32ChannelAcmod, dec_info->s32LfePresent, dec_info->s32AacProfile);
        }
    } else {
        ALOGE("Invalid parameter: stream:%p dec_info:%p", stream, dec_info);
    }
    return ret;
}

void ms12_close_all_spdifout(struct dolby_ms12_desc *ms12) {
    int i = 0;
    struct aml_audio_device *adev = adev_get_handle();
    pthread_mutex_lock(&adev->bitstream_lock);
    for (i = 0; i < BITSTREAM_OUTPUT_CNT; i++) {
        struct bitstream_out_desc * bitstream_out = &ms12->bitstream_out[i];
        if (bitstream_out->spdifout_handle) {
            ALOGI("%s id=%d spdif handle =%p", __func__, i, bitstream_out->spdifout_handle);
            aml_audio_spdifout_close(bitstream_out->spdifout_handle);
            bitstream_out->spdifout_handle = NULL;
        }
        memset(bitstream_out, 0, sizeof(struct bitstream_out_desc));
    }
    pthread_mutex_unlock(&adev->bitstream_lock);
}

static void ms12_reset_all_spdifout(struct dolby_ms12_desc *ms12) {
    int i = 0;
    struct aml_audio_device *adev = adev_get_handle();
    pthread_mutex_lock(&adev->bitstream_lock);
    for (i = 0; i < BITSTREAM_OUTPUT_CNT; i++) {
        struct bitstream_out_desc * bitstream_out = &ms12->bitstream_out[i];
        if (bitstream_out->spdifout_handle) {
            aml_audio_spdifout_reset_hdmitx(bitstream_out->spdifout_handle);

        }
    }
    pthread_mutex_unlock(&adev->bitstream_lock);
}


#if 0
void dynamic_set_dolby_ms12_drc_parameters(struct dolby_ms12_desc *ms12)
{
    int drc_mode = 0;
    int drc_cut = 0;
    int drc_boost = 0;
    int dolby_ms12_drc_mode = DOLBY_DRC_RF_MODE;
    int dolby_ms12_dap_drc_mode = DOLBY_DRC_RF_MODE;

    if (!ms12) {
        ALOGE("%s() input ms12 is NULL!\n", __FUNCTION__);
        return ;
    }

    if (0 == aml_audio_get_dolby_drc_mode(ms12, &drc_mode, &drc_cut, &drc_boost))
        dolby_ms12_drc_mode = drc_mode;

    /*
     * if main input is hdmi-in/dtv/other-source PCM
     * would not go through the DRC processing
     * DRC LineMode means to bypass DRC processing.
     */
    if (audio_is_linear_pcm(ms12->main_input_fmt)) {
        dolby_ms12_drc_mode = DOLBY_DRC_LINE_MODE;
    }

    set_ms12_drc_boost_value_for_2ch_downmixed_output(ms12, drc_boost);
    set_ms12_drc_cut_value_for_2ch_downmixed_output(ms12, drc_cut);
    set_ms12_drc_mode_for_2ch_downmixed_output(ms12, dolby_ms12_drc_mode);
    ALOGI("%s dynamic set drc %s boost %d cut %d", __FUNCTION__,
        (dolby_ms12_drc_mode == DOLBY_DRC_RF_MODE) ? "RF MODE" : "LINE MODE", drc_boost, drc_cut);

    if (ms12->output_config & MS12_OUTPUT_MASK_DAP) {
        if (0 == aml_audio_get_dolby_dap_drc_mode(ms12, &drc_mode, &drc_cut, &drc_boost))
            dolby_ms12_dap_drc_mode = drc_mode;

        /*
         * if main input is hdmi-in/dtv/other-source PCM
         * would not go through the DRC processing
         * DRC LineMode means to bypass DRC processing.
         */
        if (audio_is_linear_pcm(ms12->main_input_fmt)) {
            dolby_ms12_dap_drc_mode = DOLBY_DRC_LINE_MODE;
        }

        set_ms12_drc_boost_value(ms12, drc_boost);
        set_ms12_drc_cut_value(ms12, drc_cut);
        set_ms12_drc_mode_for_multichannel_and_dap_output(ms12, dolby_ms12_dap_drc_mode);
        ALOGI("%s dynamic set dap_drc %s",
            __FUNCTION__, (dolby_ms12_dap_drc_mode == DOLBY_DRC_RF_MODE) ? "RF MODE" : "LINE MODE");
    }

}
#endif

void set_ms12_decoder_mute(struct audio_stream_out *stream, bool b_mute, unsigned int duration)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    if (aml_out == NULL) {
        ALOGE("%s b_mute %d, duration %d fail ! (stream is NULL)", __func__, b_mute, duration);
        return;
    }
    ALOGI("%s b_mute %d duration %d ms", __FUNCTION__, b_mute, duration);
    aml_out->is_decoder_muted = b_mute;
    aml_out->decoder_mute_duration = duration;
}

void set_dolby_ms12_drc_parameters(audio_format_t input_format, int output_config_mask, struct dolby_ms12_desc *ms12)
{
    int dolby_ms12_drc_mode = DOLBY_DRC_RF_MODE;
    int dolby_ms12_dap_drc_mode = DOLBY_DRC_RF_MODE;
    int drc_mode = 0;
    int drc_cut = 0;
    int drc_boost = 0;

    if (0 == aml_audio_get_dolby_drc_mode(ms12, &drc_mode, &drc_cut, &drc_boost))
        dolby_ms12_drc_mode = drc_mode;
    //for mul-pcm
    dolby_ms12_set_drc_boost(drc_boost);
    dolby_ms12_set_drc_cut(drc_cut);
    //for 2-channel downmix
    dolby_ms12_set_drc_boost_stereo(drc_boost);
    dolby_ms12_set_drc_cut_stereo(drc_cut);

    /*
     * if main input is hdmi-in/dtv/other-source PCM
     * would not go through the DRC processing
     * DRC LineMode means to bypass DRC processing.
     */
    if (audio_is_linear_pcm(input_format)) {
        dolby_ms12_drc_mode = DOLBY_DRC_LINE_MODE;
    }

    dolby_ms12_set_drc_mode(dolby_ms12_drc_mode);
    ALOGI("%s dolby_ms12_set_drc_mode %s", __FUNCTION__, (dolby_ms12_drc_mode == DOLBY_DRC_RF_MODE) ? "RF MODE" : "LINE MODE");

    if (output_config_mask & MS12_OUTPUT_MASK_DAP) {
        if (0 == aml_audio_get_dolby_dap_drc_mode(ms12, &drc_mode, &drc_cut, &drc_boost))
            dolby_ms12_dap_drc_mode = drc_mode;
        /*
         * if main input is hdmi-in/dtv/other-source PCM
         * would not go through the DRC processing
         * DRC LineMode means to bypass DRC processing.
         */
        if (audio_is_linear_pcm(input_format)) {
            dolby_ms12_dap_drc_mode = DOLBY_DRC_LINE_MODE;
        }

        dolby_ms12_set_dap_drc_mode(dolby_ms12_dap_drc_mode);
        ALOGI("%s dolby_ms12_set_dap_drc_mode %s",
            __FUNCTION__, (dolby_ms12_dap_drc_mode == DOLBY_DRC_RF_MODE) ? "RF MODE" : "LINE MODE");
    }
}

static void set_dolby_ms12_dap_init_mode(struct aml_audio_device *adev)
{
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int dap_init_mode = 0;

    /* Dolby MS12 V2 uses DAP Tuning file */
    if (adev->is_ms12_tuning_dat
        && ((adev->board_config.dolby_ms12_audio_config == MS12_CONFIG_X)
        || (adev->board_config.dolby_ms12_audio_config == MS12_CONFIG_Z))) {
        dap_init_mode = get_ms12_dap_init_mode(is_TV(adev) || is_SBR(adev));
    }

    if (adev->dolby_ms12_dap_init_mode) {
        dap_init_mode = adev->dolby_ms12_dap_init_mode;
    } else {
        adev->dolby_ms12_dap_init_mode = dap_init_mode;
    }
    ALOGI("dap_init_mode = %d", dap_init_mode);
    dolby_ms12_set_dap2_initialisation_mode(dap_init_mode);
}

static void set_dolby_ms12_downmix_mode(struct aml_audio_device *adev)
{
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int downmix_mode = DOWNMIX_MODE_LtRt; // Lt/Rt is default mode
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int value = 0;

    ret = property_get(MS12_DOWNMIX_MODE_PROPERTY, buf, NULL);
    if (ret > 0) {
        if (strcasecmp(buf, "Lt/Rt") == 0) {
            downmix_mode = DOWNMIX_MODE_LtRt;
        }
        else if (strcasecmp(buf, "Lo/Ro") == 0) {
            downmix_mode = DOWNMIX_MODE_LoRo;
        }
        else if (strcasecmp(buf, "ARIB") == 0) {
            /* for HE-AAC, currently, it is not used at all. */
            downmix_mode = DOWNMIX_MODE_ARIB;
        }
    }

    dolby_ms12_set_downmix_modes(downmix_mode);
}


void set_dolby_ms12_main_speed(struct audio_stream_out *stream, double speed) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;

    if (fabs(speed) < 1e-6) {
        ALOGE("%s invalid speed =%f", __func__, speed);
        return;
    }
    if (ms12_dec) {
        ms12_dec->tempo_speed = speed;
        ALOGI("%s ms12_dec->tempo_speed = %f", __FUNCTION__, ms12_dec->tempo_speed);
    }

    speed_info->speed = speed;
    speed_info->last_speed = speed;
}

void set_dolby_ms12_continuous_state(struct dolby_ms12_desc *ms12, int state) {
    struct aml_audio_device *adev = aml_adev_get_handle();
    char parm[64] = "";
    bool enable;
    if (is_TV(adev)) {
        ms12->ms12_continuous_state = state;
        if (state == MS12_SCHEDULER_RUNNING) {
            if (sem_post(&ms12->standby_sem)) {
                ALOGE("%s post ms12 unstandby semaphore failed", __FUNCTION__);
            } else {
                ALOGD("%s  post ms12 unstandby semaphore successful", __FUNCTION__);
            }
        } else {
            // do nothing
        }
    } else {
        // ott ms12 continuous thread always running.
        ms12->ms12_continuous_state = MS12_SCHEDULER_RUNNING;
        if (state == MS12_SCHEDULER_RUNNING) {
            enable = false;
        } else {
            enable = true;
        }
        ALOGD("%s  audio_continuous_standby_set status %d", __FUNCTION__, enable);
        audio_continuous_standby_set(ms12->continuous_standby_handle, STANDBY_SET_STATUS, enable);
    }
}

/*
 *@brief get dolby ms12 prepared
 */
int get_the_dolby_ms12_prepared(
    struct aml_stream_out *aml_out
    , audio_format_t input_format
    , audio_channel_mask_t input_channel_mask
    , int input_sample_rate)
{
    ALOGI("+%s()  aml_out:%p input_format %#x\n", __FUNCTION__, aml_out, input_format);
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct aml_stream_out *out = aml_out;
    int output_config = MS12_OUTPUT_MASK_STEREO;
    uint64_t dtv_decoder_offset_base = 0;
    unsigned int sink_max_channels = 2;
    int ret = 0, associate_audio_mixing_enable = 0 , media_presentation_id = -1,mixing_level = 0,ad_vol = 100;
    int ms12_init_count = 0;
    struct aml_arc_hdmi_desc* hdmi_descs = NULL;
    bool output_5_1_ddp = getprop_bool(MS12_OUTPUT_5_1_DDP);
    ms12->tv_tuning_flag = getprop_bool(MS12_TV_TUNING);

    if (adev->ms12.dap_only_enable) {
        aml_dap_close(&(adev->ms12));
    }

    ALOGI("\n+%s()", __FUNCTION__);
    pthread_mutex_lock(&ms12->lock);
    pthread_mutex_lock(&ms12->main_lock);
    ALOGI("++%s(), locked", __FUNCTION__);
    ms12->optical_format = adev->optical_format;
    ms12->sink_format    = adev->sink_format;
    sink_max_channels    = adev->sink_max_channels;
    set_audio_system_format(AUDIO_FORMAT_PCM_16_BIT);
    input_format = ms12_get_audio_hal_format(input_format);
    /*
    when HDMITX send pause frame,we treated as INVALID format.
    for MS12,we treat it as LPCM and mute the frame
    */
    if (input_format == AUDIO_FORMAT_INVALID ||
        !is_dolby_ms12_support_compression_format(input_format)) {
        input_format = AUDIO_FORMAT_PCM_16_BIT;
    }
    set_audio_app_format(AUDIO_FORMAT_PCM_16_BIT);
    set_audio_main_format(input_format);
    dolby_ms12_set_dap_only(0);

    // to do
    if (adev->board_config.ai_de_config == 1) {
        dolby_ms12_set_hal_content_process(1);
    }

    /*
     *-tv_tuning    Flag to activate a special processing graph for TV tuning purposes:
     *     * The input is expected to be a MAT tuning signal (-im).
     *     * The output is the MAT decoded signal without further processing (-o_dap_speaker).
     */
    if (ms12->tv_tuning_flag && (input_format == AUDIO_FORMAT_MAT)) {
        dolby_ms12_set_tv_tuning_flag(ms12->tv_tuning_flag);
        output_config |= MS12_OUTPUT_MASK_SPEAKER;
    }

    /*set the continuous output flag*/
    set_dolby_ms12_continuous_mode((bool)adev->continuous_audio_mode);
    dolby_ms12_set_atmos_lock_flag(adev->atoms_lock_flag);
    /*set the dolby ms12 debug level*/
    dolby_ms12_enable_debug();

    /*
     *In case of AC-4 or Dolby Digital Plus input,
     *set output DDP bitstream format DDP Atmos(5.1.2) or DDP(5.1)
     */

    if (output_5_1_ddp) {
        dolby_ms12_set_encoder_channel_mode_locking_mode(output_5_1_ddp);
    }

    adev->ms12_out = out;
    adev->ms12_out->standby = false;
    ALOGI("%s adev->ms12_out =  %p", __func__, adev->ms12_out);

    {
        int ret = aml_audio_timer_create(ms12_timer_callback_handler);
        if (ret < 0) {
            ALOGE("func:%s  timer_id:%d error and exit", __func__, ms12->ms12_timer_id);
            goto Err_Timer_Create;
        } else {
            ms12->ms12_timer_id = ret;
            ALOGI("func:%s  timer_id:%d", __func__, ms12->ms12_timer_id);
        }
    }

    /************end**************/
    /*set the system app sound mixing enable*/
    dolby_ms12_set_system_app_audio_mixing(SYSTEM_APP_SOUND_MIXING_ON);

    /* set DAP init mode */
    set_dolby_ms12_dap_init_mode(adev);
    /* set Downmix mode(Lt/Rt as default) */
    set_dolby_ms12_downmix_mode(adev);

    ms12->dual_bitstream_support = adev->dual_spdif_support;
    if (adev->sink_capability == AUDIO_FORMAT_MAT && !netflix_request_dd_output()) {
        output_config = MS12_OUTPUT_MASK_STEREO | MS12_OUTPUT_MASK_MAT;
    } else {
        if (is_TV(adev)) {
            output_config = MS12_OUTPUT_MASK_DD | MS12_OUTPUT_MASK_STEREO | MS12_OUTPUT_MASK_SPEAKER;
        } else {
            output_config = MS12_OUTPUT_MASK_DD | MS12_OUTPUT_MASK_STEREO;
        }
        if (adev->sink_capability == AUDIO_FORMAT_E_AC3) {
            // output ddp when sink needs, to reduce cpu loading
            output_config |= MS12_OUTPUT_MASK_DDP;
        }
    }
    /* for soundbar, we still need stereo pcm, sometimes the dap will be disabled*/
    if (is_SBR(adev) /*&& (adev->enable_soundbar_mode(adev) == 1)*/)
        output_config |= MS12_OUTPUT_MASK_SPEAKER | MS12_OUTPUT_MASK_STEREO;


    // MS12_OUTPUT_MASK_MC : NTS LLP-AUDIO-OUTPUT-LATENCY-STB-6CH
    // mc-pcm file writer should be created, later we can turn it off by runtime parameters
    output_config |= MS12_OUTPUT_MASK_MC;

    struct audio_board_config *bd_config = &adev->board_config;
    if (bd_config->ms12_output_mask)
        output_config = bd_config->ms12_output_mask;

    set_dolby_ms12_drc_parameters(input_format, output_config, ms12);


    if (is_dev_patch_valid(adev) && is_dev_patch_exist(adev) && get_dev_patch(adev)->input_src == AUDIO_DEVICE_IN_HDMI) {
        if (!adev->continuous_audio_mode &&
            ((input_format == AUDIO_FORMAT_AC3) || (input_format == AUDIO_FORMAT_E_AC3))) {
            dolby_ms12_set_enforce_timeslice(true);
            ALOGI("hdmi in ddp/dd case, use enforce timeslice");
        }
    }

    if (is_TV(adev) && (output_config & MS12_OUTPUT_MASK_DDP)) {
        // reduce ddp encoder latency (phase 90 shifted : disable)
        dolby_ms12_set_hdmi_output_type(HDMI_ARC_OUTPUT);
    }

    if (input_sample_rate != OUTPUT_ALSA_SAMPLERATE &&
        (aml_out->streamType == STREAM_PCM_HWSYNC || aml_out->streamType == STREAM_PCM_DIRECT)
        && !is_multi_channel_pcm((struct audio_stream_out *)aml_out)) {
        ALOGD("%s change SampleRate from %d to %d, for ms12 config.", __func__, input_sample_rate, OUTPUT_ALSA_SAMPLERATE);
        input_sample_rate = OUTPUT_ALSA_SAMPLERATE;
    }

    if (continuous_mode(adev) && adev->continuous_enable_mixer_max_size) {
        ms12->enable_mixer_max_size = adev->continuous_enable_mixer_max_size;
    } else {
        ms12->enable_mixer_max_size = true;
    }
    ALOGI("%s : continuous_mode %d, continuous_enable_mixer_max_size %d, ms12->enable_mixer_max_size %d", __func__, \
            continuous_mode(adev), adev->continuous_enable_mixer_max_size, ms12->enable_mixer_max_size);

    do {
        aml_ms12_config(ms12, input_format, input_channel_mask, input_sample_rate, output_config, get_ms12_path());
        if (ms12->dolby_ms12_enable) {
            break;
        } else {
            ms12_init_count++;
        }
        /*coverity[sleep]*/
        usleep(1000);
        ALOGI("%s ms12_init_count:%d", __func__, ms12_init_count);
    } while(ms12_init_count < 5);//give the 5 times to config ms12.
    if (ms12_init_count >= 5 || !ms12->dolby_ms12_enable) {
        goto Err_Ms12_Config;
    }

    //Todo
    if (adev->board_config.ai_de_config == 1) {
        dolby_ms12_continuous_register_callback(ms12->dolby_ms12_ptr, MS12_CONTINUOUS_CALLBACK_CONTENT_PROCESS, ms12_content_process_callback, (void *)out);
    }

    ms12->dolby_ms12_init_flags = true;
    if (ms12->dolby_ms12_enable) {
        //register Dolby MS12 callback
        dolby_ms12_register_output_callback(ms12_output, (void *)out);

        ms12->device = usecase_device_adapter_with_ms12(out->device,AUDIO_FORMAT_PCM_16_BIT/* adev->sink_format*/);
        ALOGI("%s out [dual_output_flag %d] adev [format sink %#x optical %#x] ms12 [output-format %#x device %d]",
              __FUNCTION__, out->dual_output_flag, adev->sink_format, adev->optical_format, ms12->output_config, ms12->device);
        memcpy((void *) & (adev->ms12_config), (const void *) & (out->config), sizeof(struct pcm_config));
        get_hardware_config_parameters(
            &(adev->ms12_config)
            , AUDIO_FORMAT_PCM_16_BIT
            , bd_config->default_alsa_ch
            , ms12->output_samplerate
            , out->is_tv_platform
            , continuous_mode(adev)
            , is_game_mode(adev));

        /*config the ms12 encoder output graph*/
        dolby_ms12_encoder_open(ms12->dolby_ms12_ptr, ms12->dolby_ms12_init_argc, ms12->dolby_ms12_init_argv);
        //n bytes of downmix output pcm frame, 16bits_per_sample / stereo, it value is 4bytes.
        ms12->nbytes_of_dmx_output_pcm_frame = nbytes_of_dolby_ms12_downmix_output_pcm_frame();
        ms12->ms12_digital_audio_format = adev->digital_audio_mode;
        /*IEC61937 DDP format, the real samplerate need device by 4*/
        if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
            if ((aml_out->hal_internal_format & AUDIO_FORMAT_E_AC3) == AUDIO_FORMAT_E_AC3) {
                input_sample_rate /= 4;
            }
        }
    }
    ms12->sys_audio_base_pos = adev->sys_audio_frame_written;
    ms12->deep_buf_audio_base_pos = adev->deep_buf_audio_frame_written;
    ms12->sys_audio_skip     = 0;
    ms12->deep_buf_audio_skip = 0;
    ms12->dap_pcm_frames     = 0;
    ms12->stereo_pcm_frames  = 0;
    ms12->master_pcm_frames  = 0;
    ms12->do_easing = false;
    ms12->is_muted = false;
    ms12->b_legacy_ddpout    = dolby_ms12_get_ddp_5_1_out();
    set_ms12_main_volume(ms12, 1.0f);
    ALOGI("%s line %d set ms12 main volume as 1.0\n", __func__, __LINE__);
    ms12->dtv_decoder_offset_base = dtv_decoder_offset_base;
    ALOGI("set ms12 sys pos =%" PRId64 "", ms12->sys_audio_base_pos);
    ms12->aaudio_low_latency = false;
    ms12->tempo_speed        = 1.0f;
    ms12->ms12_continuous_state = MS12_SCHEDULER_RUNNING;
    ms12->ms12_scheduler_state = MS12_SCHEDULER_RUNNING;

    if (sem_init(&ms12->standby_sem, 0, 1)) {
        ALOGE("%s init ms12 standby semaphore failed\n", __FUNCTION__);
        goto Err_Ms12_Config;
    } else {
        ALOGD("%s init ms12 standby semaphore successful\n", __FUNCTION__);
    }

    ALOGI("set ms12 deep buf pos =%" PRId64 "", ms12->deep_buf_audio_base_pos);

    ms12->iec61937_ddp_buf = aml_audio_calloc(1, MS12_DDP_FRAME_SIZE);
    if (ms12->iec61937_ddp_buf == NULL) {
        goto Err_Iec61937_Calloc;
    }

    /*coverity[missing_lock]*/
    ret = ring_buffer_init(&ms12->spdif_ring_buffer, ms12->dolby_ms12_out_max_size);
    if (ret != 0) {
        ALOGW("[%s:%d] init is error", __func__, __LINE__);
        goto Err_RingBuf_Init;
    }
    adev->doing_reinit_ms12 = false;
    ms12->debug_synced_frame_pts_flag = get_debug_value(AML_DEBUG_AUDIOHAL_SYNCPTS);
    /*
     * usage to set the MAT Encoder debug level:
     *          setprop "vendor.media.audiohal.matenc.debug" 1
     *          1: for mat encoder debug config
     *          2: for mat encoder debug init
     *          4: for mat encoder debug input
     *          8: for mat encoder debug output
     */
    ms12->mat_enc_debug_enable = get_debug_value(AML_DEBUG_AUDIOHAL_MATENC);

    /*if arc is connected, we need disable dap*/
    if (ms12->dolby_ms12_enable && adev->cur_out_devices == OUTPORT_HDMI_ARC && is_HDMI_connected(adev)) {
        set_ms12_full_dap_disable(ms12, true);
    }

    ms12->system_sound_target = 0 - get_ms12_syss_mixgain_target();
    ALOGI("%s() line %d system_sound_target %d dB (only do pre attenuation for DTV-patch&System PCM on DRC-RF mode)\n",
        __FUNCTION__, __LINE__, ms12->system_sound_target);

    if (pthread_mutex_init(&ms12->main_apts_update_lock, NULL)) {
        ALOGE("%s pthread_mutex_init(main_apts_update_lock) failed", __func__);
    }
    set_ms12_alsa_limit_frame(ms12, MS12_ALSA_DEFAULT_LIMIT_FRAME);
    set_ms12_scheduler_sleep(ms12, true);
    ms12->scheduler_run_count = 0;
    hdmi_descs = get_arc_hdmi_cap(adev);
    aml_out->trace_last_write_time_ms = 0;

    /* only enable mc output when it supports multi channel */
    // For netflix apk, DDP/MAT and mc-pcm will not exist at the same time.
    // currently, eARC always support 8ch pcm.
    if ((hdmi_descs->pcm_fmt.max_channels >= 6 || is_earc_connected(adev))
        && !(output_config & (MS12_OUTPUT_MASK_MAT|MS12_OUTPUT_MASK_DDP))) {
        ms12->output_config |= MS12_OUTPUT_MASK_MC;
        set_ms12_mch_enable(ms12, true);
    } else {
        ms12->output_config &= (~MS12_OUTPUT_MASK_MC);
        set_ms12_mch_enable(ms12, false);
    }

    audio_continuous_standby_open(&ms12->continuous_standby_handle, &ms12_output, (void *)out);
    if (adev->dolby_ms12_dap_init_mode) {
        output_config |= MS12_OUTPUT_MASK_DAP;
    }
    audio_continuous_standby_set(ms12->continuous_standby_handle, STANDBY_SET_OUTPUT_PORT, output_config);

    /*ms12 related resources are prepared, we can start ms12 thread*/
    if (continuous_mode(adev) && ms12->dolby_ms12_enable) {
        ms12->dolby_ms12_thread_exit = false;
        ret = pthread_create(&(ms12->dolby_ms12_threadID), NULL, &dolby_ms12_threadloop, out);
        if (ret != 0) {
            ALOGE("%s, Create dolby_ms12_thread fail!\n", __FUNCTION__);
            goto Err_DolbyMs12_Thread;
        }
        ALOGI("%s() thread is build, get dolby_ms12_threadID %ld\n", __FUNCTION__, ms12->dolby_ms12_threadID);
    }

    ALOGI("--%s(), locked", __FUNCTION__);
    pthread_mutex_unlock(&ms12->main_lock);
    pthread_mutex_unlock(&ms12->lock);

    ALOGI("-%s()\n\n", __FUNCTION__);

    return ret;
Err_DolbyMs12_Thread:
    if (continuous_mode(adev)) {
        if (ms12->dolby_ms12_enable) {
            ALOGE("%s() %d exit dolby_ms12_thread\n", __FUNCTION__, __LINE__);
            ms12->dolby_ms12_thread_exit = true;
            ms12->dolby_ms12_threadID = 0;
        }
    }
Err_RingBuf_Init:
    ring_buffer_release(&ms12->spdif_ring_buffer);
Err_Iec61937_Calloc:
    if (ms12->iec61937_ddp_buf) {
        aml_audio_free(ms12->iec61937_ddp_buf);
        ms12->iec61937_ddp_buf = NULL;
    }
Err_Ms12_Config:
    aml_ms12_cleanup(ms12);
Err_Timer_Create:
    aml_audio_timer_delete(ms12->ms12_timer_id);
    pthread_mutex_unlock(&ms12->main_lock);
    pthread_mutex_unlock(&ms12->lock);
    return ret;
}

static inline bool is_hdmiin_source_for_audio_patch(struct aml_audio_device *adev)
{
    struct aml_audio_patch *patch =  get_dev_patch(adev);
    bool is_hdmiin_input_src = (patch && (patch->input_src == AUDIO_DEVICE_IN_HDMI));

    return is_hdmiin_input_src;
}

bool is_ms12_passthrough(struct audio_stream_out *stream) {
    bool bypass_ms12 = false;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;

    /* TrueHD-content can not passthrough, should be decoded with DLB-MS12 pipeline */
    bool is_bypass_truehd = false;
    bool is_truehd = false;
    bool is_mat = false;
    bool is_truehd_supported = false;

    /* Fixme: The TrueHD passthrough function is in the TODO List. */

    is_truehd = (aml_out->hal_internal_format == AUDIO_FORMAT_DOLBY_TRUEHD);
    is_mat = (aml_out->hal_internal_format == AUDIO_FORMAT_MAT);
    is_truehd_supported = ((adev->optical_format == AUDIO_FORMAT_MAT) || (adev->optical_format == AUDIO_FORMAT_DOLBY_TRUEHD));

    /* source is HDMI-IN, the mat can do passthrough when MAT is supported in sink*/
    if (is_hdmiin_source_for_audio_patch(adev)) {
        is_bypass_truehd = is_mat && is_truehd_supported;
    }
    /* source is local playback, the truehd can do passthrough when MAT is supported in sink*/
    else {
        is_bypass_truehd = is_truehd && is_truehd_supported;
    }

    if (adev->debug_flag & AUDIO_HAL_DEBUG_PASSTHROUGH) {
        ALOGD("%s line %d is_bypass_truehd %d is_truehd %d is_mat %d is_truehd_supported %d",
            __FUNCTION__, __LINE__, is_bypass_truehd, is_truehd, is_mat, is_truehd_supported);
    }

    if ((adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS)
        /* when arc output, the optical_format == sink format
         * when speaker output, the optical format != format
         * only the optical_format >= hal_internal_format, we can do passthrough,
         * otherwise we need do some convert
         */
        && ((adev->optical_format >= aml_out->hal_internal_format) || is_bypass_truehd)) {
        if (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3 ||
            aml_out->hal_internal_format == AUDIO_FORMAT_AC3) {
            /*current we only support 48k ddp/dd bypass*/
            if (aml_out->hal_rate == 48000 || aml_out->hal_rate == 192000 ||
                aml_out->hal_rate == 44100 || aml_out->hal_rate == 176400) {
                bypass_ms12 = true;
            } else if (aml_out->hal_internal_format == AUDIO_FORMAT_AC3 &&
                (aml_out->hal_rate == 32000)) {
                bypass_ms12 = true;
            }
        }
        else if (is_bypass_truehd) {
            bypass_ms12 = true;
        }
    }
    if (adev->debug_flag & AUDIO_HAL_DEBUG_PASSTHROUGH) {
        ALOGD("%s line %d bypass_ms12 =%d digital mode =%s optical format =0x%x internal format 0x%x  hal_rate:%d",
            __FUNCTION__, __LINE__, bypass_ms12, digitalAudioModeType2Str(adev->digital_audio_mode),
            adev->optical_format, aml_out->hal_internal_format, aml_out->hal_rate);
    }
    return bypass_ms12;
}

/*
 *@brief dolby ms12 main process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */

int dolby_ms12_main_process(
    struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , size_t *use_size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = NULL;
    int ms12_output_size = 0;
    int dolby_ms12_input_bytes = 0;
    int dolby_ms12_associate_input_bytes = 0;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;

    void *input_buffer = (void *)buffer;
    size_t input_bytes = bytes;
    int single_decoder_used_bytes = 0;
    void *main_frame_buffer = input_buffer;/*input_buffer as default*/
    int main_frame_size = input_bytes;/*input_bytes as default*/
    void *associate_frame_buffer = NULL;
    int associate_frame_size = 0;
    bool associate_audio_mixing_enable = false;
    int32_t parser_used_size = 0;
    int32_t spdif_dec_used_size = 0;
    int dependent_frame = 0;
    int sample_rate = 48000;
    int ret = 0;
    struct ac4_parser_info ac4_info = { 0 };
    audio_format_t ms12_hal_format = ms12_get_audio_hal_format(aml_out->hal_format);
    bool is_dd_format = (ms12_hal_format == AUDIO_FORMAT_AC3);
    bool is_ddp_format = (ms12_hal_format == AUDIO_FORMAT_E_AC3);
    bool is_heaac_format = ((ms12_hal_format == AUDIO_FORMAT_AAC) || \
                            (ms12_hal_format == AUDIO_FORMAT_AAC_LATM) || \
                            (ms12_hal_format == AUDIO_FORMAT_HE_AAC_V1) || \
                            (ms12_hal_format == AUDIO_FORMAT_HE_AAC_V2));

    int ddp_1st_used_size = 0;
    void * ddp_1st_main_frame_buffer = NULL;
    int ddp_1st_main_frame_size = 0;
    int ddp_1st_numblks = 0;

    if (adev->debug_flag >= 2) {
        ALOGI("\n%s() in continuous %d input bytes %zu\n",
              __FUNCTION__, adev->continuous_audio_mode, input_bytes);
    }

    if (adev->ms12_to_be_cleanup && is_dtv_stream_out(stream)) {
        return ret;
    }

    //double check ms12 dec handle
    dolby_ms12_create_dec_handle(stream);
    ms12_dec = aml_out->ms12_dec_handle;

    pthread_mutex_lock(&ms12_dec->main_lock);

    if (aml_out->is_ms12_main_decoder_disable) {
        AM_LOGI("is_ms12_main_decoder_disable, drop %zu bytes", bytes);
        if (audio_is_linear_pcm(aml_out->hal_format)) {
            // sleep half of data's duration , avoid audioflinger underrun frequently.
            int duration_ms = bytes / aml_out->hal_frame_size / (aml_out->hal_rate/1000) / 2;
            if (duration_ms > 0) {
                usleep(duration_ms * 1000);
            }
        }
        *use_size = bytes;
        goto exit;
    }

    if (ms12->dolby_ms12_enable && !aml_out->is_ms12_main_decoder) {
        dolby_ms12_main_open(stream);
    }

    if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT) && audio_is_linear_pcm(aml_out->hal_internal_format)) {
        check_audio_level("ms12_main", buffer, bytes);
    }

    if (ms12->dolby_ms12_enable) {
        //ms12 input main
        int dual_input_ret = 0;

        /*this status is only updated in hw_write, continuous mode also need it*/
        if (adev->continuous_audio_mode) {
            if (aml_out->stream_status != STREAM_HW_WRITING) {
                aml_out->stream_status = STREAM_HW_WRITING;
            }
        }
#ifdef ENABLE_DVB_PATCH
        aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)aml_out->audio_buffer;
        aml_audio_buffer_t *audioBuffer = pBuffer->inBuffer;
        aml_dtv_audiopara_t *dtv_audio_info = audioBuffer->privObject;
        if (is_dtv_stream_out(stream) && dtv_audio_info) {
            associate_frame_buffer = dtv_audio_info->ad_data;
            associate_frame_size = dtv_audio_info->ad_size;
            associate_audio_mixing_enable = dtv_audio_info->associate_audio_mixing_enable;
        }
#endif
        /* Passthrough Mode, only get the MAIN data */
        if (ms12_dec->codec_info.s32AdInput && is_ad_data_available(adev->digital_audio_mode) && (is_dd_format || is_ddp_format || is_heaac_format)) {
            main_frame_buffer = input_buffer;
            main_frame_size = input_bytes;
        }
        /*
        As the audio payload may cross two write process,we can not skip the
        data when we do not get a complete payload.for ATSC,as we have a
        complete burst align for 6144/24576,so we always can find a valid
        payload in one write process.
        */
        else if (is_iec61937_format(stream)) {
            struct ac3_parser_info ac3_info = { 0 };
            void * inbuf = NULL;
            int32_t buf_size = 0;

            audio_format_t output_format = AUDIO_FORMAT_PCM_16_BIT;
            aml_spdif_decoder_process(ms12_dec->spdif_dec_handle, input_buffer , input_bytes, &spdif_dec_used_size, &main_frame_buffer, &main_frame_size);
            if (main_frame_size && main_frame_buffer) {
                endian16_convert(main_frame_buffer, main_frame_size);
            }

            if (main_frame_size == 0) {
                *use_size = spdif_dec_used_size;
                goto exit;
            }
            output_format = aml_spdif_decoder_getformat(ms12_dec->spdif_dec_handle);
            if (output_format == AUDIO_FORMAT_E_AC3
                || output_format == AUDIO_FORMAT_AC3) {
                inbuf = main_frame_buffer;
                buf_size = main_frame_size;
                aml_ac3_parser_process(ms12_dec->ac3_parser_handle, inbuf, buf_size, &ddp_1st_used_size, &ddp_1st_main_frame_buffer, &ddp_1st_main_frame_size, &ac3_info);
                if (ac3_info.sample_rate != 0) {
                    sample_rate = ac3_info.sample_rate;
                }
                if (ddp_1st_main_frame_size) {
                    ddp_1st_numblks = ac3_info.numblks;
                }
                ALOGV("Input size =%zu used_size =%d output size=%d rate=%d internal format=0x%x rate=%d",
                    input_bytes, spdif_dec_used_size, main_frame_size, aml_out->hal_rate, aml_out->hal_internal_format, sample_rate);

                if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS && main_frame_size != 0 && !is_tv_stream_out(aml_out)) {
                    dolby_ms12_bypass_process(stream, main_frame_buffer, main_frame_size);
                }
            }

            if (ms12_dec->codec_info.u32AudioFormat == AUDIO_FORMAT_MAT) {
                int mat_stream_profile = get_stream_profile_from_dolby_mat_frame((const char *)main_frame_buffer, main_frame_size);
                if (IS_AVAILABLE_MAT_STREAM_PROFILE(mat_stream_profile)) {
                    dolby_ms12_set_mat_stream_profile(mat_stream_profile);
                    if (ms12_dec->mat_stream_profile == 0) {
                        ms12_dec->mat_stream_profile = mat_stream_profile;
                    } else {
                        bool original_is_mat_pcm = ((OBJECT_PCM_WITHIN_MAT_PROFILE == ms12_dec->mat_stream_profile) || (CHANNEL_BASED_PCM_WITHIN_MAT_PROFILE == ms12_dec->mat_stream_profile));
                        bool current_is_mat_pcm = ((OBJECT_PCM_WITHIN_MAT_PROFILE == mat_stream_profile) || (CHANNEL_BASED_PCM_WITHIN_MAT_PROFILE == mat_stream_profile));
                        if (current_is_mat_pcm != original_is_mat_pcm) {
                            ALOGI("mat format change from %d to %d", original_is_mat_pcm, current_is_mat_pcm);
                            aml_out->is_mat_changed = true;
                            *use_size = spdif_dec_used_size;
                            goto exit;
                        }
                    }
                }
            }
        }
        /*
         *continuous output with dolby atmos input, the ddp frame size is variable.
         */
        else if (!is_tv_stream_out(aml_out)) {
            if ((ms12_hal_format == AUDIO_FORMAT_AC3) ||
                (ms12_hal_format == AUDIO_FORMAT_E_AC3)) {
                struct ac3_parser_info ac3_info = { 0 };
                if (adev->debug_flag) {
                    ALOGI("%s line %d ###### frame size %d #####",
                        __func__, __LINE__, aml_out->ddp_frame_size);
                }
                {
                    aml_ac3_parser_process(ms12_dec->ac3_parser_handle, input_buffer, bytes, &parser_used_size, &main_frame_buffer, &main_frame_size, &ac3_info);
                    aml_out->ddp_frame_size = main_frame_size;
                    aml_out->ddp_frame_nblks = ac3_info.numblks;
                    aml_out->total_ddp_frame_nblks += aml_out->ddp_frame_nblks;
                    dependent_frame = ac3_info.frame_dependent;
                    sample_rate = ac3_info.sample_rate;
                    if (ac3_info.frame_size == 0) {
                        *use_size = parser_used_size;
                        if (parser_used_size == 0) {
                            *use_size = bytes;
                        }
                        goto exit;

                    }
                }
                if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS && main_frame_size != 0) {
                    dolby_ms12_bypass_process(stream, main_frame_buffer, main_frame_size);
                }

           } else if (ms12_hal_format == AUDIO_FORMAT_AC4) {
                aml_ac4_parser_process(aml_out->ac4_parser_handle, input_buffer, bytes, &parser_used_size, &main_frame_buffer, &main_frame_size, &ac4_info);
                ALOGV("frame size =%d frame rate=%d sample rate=%d used =%d", ac4_info.frame_size, ac4_info.frame_rate, ac4_info.sample_rate, parser_used_size);
                if (main_frame_size == 0 && parser_used_size == 0) {
                    *use_size = bytes;
                    ALOGE("wrong ac4 frame size");
                    goto exit;
                }
                update_audio_format(adev, ms12_hal_format);

            }
        } else {
            ms12_update_decoded_info_process(stream, input_buffer, input_bytes, &ddp_1st_main_frame_size, &ddp_1st_numblks);
        }

        /* Passthrough Mode, only get the MAIN data as the single input */
        if ((ms12_dec->codec_info.s32AdInput) && associate_audio_mixing_enable &&
            ((is_ad_data_available(adev->digital_audio_mode) &&
            (is_dd_format || is_ddp_format)) || is_heaac_format)) {
            /*if there is associate frame, send it to dolby ms12.*/
            char tmp_array[4096] = {0};
            /* if AD is disappeared, use main same length but data is zero. */
            if (!associate_frame_buffer || (associate_frame_size == 0)) {
                associate_frame_buffer = (void *)&tmp_array[0];
                associate_frame_size = sizeof(tmp_array);
                if (is_heaac_format && (main_frame_size < associate_frame_size)) {
                    associate_frame_size = main_frame_size;
                }
            }
            /* For DD/DDP, if AD length < Main length, use main same length */
           if ((is_dd_format || is_ddp_format) && (associate_frame_size < main_frame_size)) {
                ALOGV("%s() main frame addr %p size %d associate frame addr %p size %d, need a larger ad input size!\n",
                      __FUNCTION__, main_frame_buffer, main_frame_size, associate_frame_buffer, associate_frame_size);
                memcpy(&tmp_array[0], associate_frame_buffer, associate_frame_size);
                 associate_frame_size = main_frame_size;
            }
            ms12_pcminfo_t ms12_pcminfo;
            ms12_pcminfo.channel_num =  aml_out->hal_ch;
            ms12_pcminfo.sample_rate = 48000;
            ms12_pcminfo.sample_bytes = 2;
            dolby_ms12_associate_input_bytes = aml_ms12_associate_decoder_write(
                                        ms12
                                        , ms12_dec
                                        , associate_frame_buffer
                                        , associate_frame_size
                                        , &ms12_pcminfo);
              if (adev->debug_flag >= 2)
                  ALOGI("%s line %d associate_frame_size %d ret dolby_ms12_associate_input_bytes %d",
                     __func__, __LINE__, main_frame_size, dolby_ms12_associate_input_bytes);
            if (get_ms12_dump_enable(DUMP_MS12_INPUT_ASSOCIATE)) {
                dump_ms12_output_data((void*)associate_frame_buffer, associate_frame_size, MS12_INPUT_SYS_ASSOCIATE_FILE);
            }

        }

MAIN_INPUT:
        if (main_frame_buffer && (main_frame_size > 0)) {
            /*input main frame*/
            int main_format = ms12_dec->codec_info.u32AudioFormat;
            int main_channel_num = aml_out->hal_ch;
            int main_sample_rate = 48000;
            int n_bytes_decoder_consumed = 0;
            int write_size = main_frame_size;
            int left_size = main_frame_size - n_bytes_decoder_consumed;

            {
                int max_size = 0;
                int main_avail = 0;
                int wait_retry = 0;
                int ms12_codecbuf_delay1 = 0;
                int ms12_codecbuf_delay2 = 0;
                ms12_pcminfo_t ms12_pcminfo;
                ms12_pcminfo.channel_num = main_channel_num;
                ms12_pcminfo.sample_rate = main_sample_rate;
                ms12_pcminfo.sample_bytes = audio_bytes_per_sample(get_primary_out_format(adev));

                /*set the dolby ms12 debug level*/
                dolby_ms12_enable_debug();
                char *frame_addr = (char *)main_frame_buffer;

                do {
                    left_size = main_frame_size - n_bytes_decoder_consumed;
                    if (left_size < 0) {
                        *use_size = bytes;
                        ALOGE("%s main =%d consune=%d left=%d", __func__, main_frame_size, n_bytes_decoder_consumed, left_size);
                        goto exit;
                    }
                    write_size = left_size;
                    main_avail = aml_ms12_decoder_getparameter(ms12, ms12_dec, MS12_CODEC_PARAMETER_MAIN_BUFFER_AVAIL, &max_size, sizeof(int));
                    /* after flush, max_size value will be set to 0 and after write first data,
                     * it will be initialized
                     */
                    if (main_avail == 0 && max_size == 0) {
                        break;
                    }
                    /*
                    the audio output not is a constant level in data_rate_DRswpddp-ARC case
                    is related to test signal and can observe in IIDK reference code.
                    Regarding your audio latency result, it looks not exceed the criteria too much
                    but I’m not in a position to evaluate the SDK test result is acceptable or not.

                    I also want to update how to prevent MS12 queue a frame during transition for your reference.
                    There are two approaches and both should work.

                    Method 1: Adding a DDP/DD framer and only send a complete frame into MS12 a time.
                    In this case, when receiving 2 DDP frames (2*2560),
                    one frame would be queue in DDP buffer because UDC decoder assumed to receive a single DDP in force time-sliced mode.
                    If there is a framer to detect frame size is 2560bytes, and send 2560bytes to MS12 a time,
                    it could prevent UDC to buffer a frame in internal buffer.

                    //Method 2: Modifying ddpi_udc_addbytes() to parse a complete frame and exit, not to consume all available data.
                    This needs to modify above function in UDC CIDK and a condition in IIDK to make it work.
                    It also needs further testing to check if there is any side effects.
                    */
                    if (is_ddp_format && ddp_1st_main_frame_size && (ddp_1st_numblks < DDP_FRAME_MAX_NUMBLK)) {
                        write_size = ddp_1st_main_frame_size;
                        if (write_size > left_size) {
                            write_size = left_size;
                        }
                    } else if (main_avail == 0 && write_size > (max_size - main_avail)) {
                        /*the buf size is not big enough, we need sperate it to several times*/
                        write_size = max_size - main_avail;
                    }
                    /*
                     * for pcm case we don't need check the available buf size, ms12 will allocate new one
                     */
                    if ((max_size - main_avail) >= write_size || audio_is_linear_pcm(ms12_hal_format)) {
                        dolby_ms12_input_bytes = aml_ms12_main_decoder_write(
                                                            ms12
                                                            , ms12_dec
                                                            , (frame_addr + n_bytes_decoder_consumed)
                                                            , write_size
                                                            , &ms12_pcminfo);
                        if (dolby_ms12_input_bytes < 0) {
                            *use_size = bytes;
                            goto exit;
                        }
                        if (adev->debug_flag >= 2)
                            ALOGI("%s line %d write_size %d ret dolby_ms12 input_bytes %d",
                                __func__, __LINE__, write_size, dolby_ms12_input_bytes);
                        n_bytes_decoder_consumed += dolby_ms12_input_bytes;
                        //let the cpu scheduling
                        dolby_ms12_get_latency_for_stereo_out(&ms12_codecbuf_delay1);
                        aml_ms12_main_decoder_process(ms12, ms12_dec);
                        dolby_ms12_get_latency_for_stereo_out(&ms12_codecbuf_delay2);
                        if (adev->debug_flag >= 2)
                            ALOGI("%s line %d ms12_codecbuf_delay START %d ms12_codecbuf_delay END %d", __func__, __LINE__, ms12_codecbuf_delay1, ms12_codecbuf_delay2);
                        aml_ms12_decoder_getparameter(ms12, ms12_dec, MS12_CODEC_PARAMETER_MAIN_CONSUMED, &ms12_dec->ms12_main_consume_bytes, sizeof(uint64_t));
                        if (adev->debug_flag >= 2) {
                            ALOGD("ms12_main_consume_bytes %" PRId64 ", dolby_ms12_input_bytes %d", ms12_dec->ms12_main_consume_bytes, dolby_ms12_input_bytes);
                        }

                        continue;
                    }

                    pthread_mutex_unlock(&ms12_dec->main_lock);
                    aml_audio_sleep(5*1000);
                    pthread_mutex_lock(&ms12_dec->main_lock);
                    wait_retry++;
                    /*it cost 3s*/
                    if (wait_retry >= MS12_MAIN_WRITE_RETIMES) {
                        *use_size = parser_used_size;
                        if (parser_used_size == 0) {
                            *use_size = bytes;
                        }
                        ALOGE("write dolby main time out, discard data=%zu main_frame_size=%d main_avail=%d max=%d", *use_size, main_frame_size, main_avail, max_size);
                        goto exit;
                    }

                }
                while (aml_out->stream_status != STREAM_STANDBY && (n_bytes_decoder_consumed < main_frame_size))  ;
            }

            if ((adev->debug_flag >= 2) && (is_dd_format || is_ddp_format)) {
                ms12->measure_last_frame_us = ms12->measure_new_frame_us;
                struct timespec measure_ts;
                clock_gettime(CLOCK_MONOTONIC, &measure_ts);
                ms12->measure_new_frame_us = measure_ts.tv_sec * 1000000LL + measure_ts.tv_nsec / 1000LL;
                uint64_t delta_frame_appear = ms12->measure_new_frame_us - ms12->measure_last_frame_us;
                aml_audio_trace_int("iec_parser_time", (int)(delta_frame_appear / 1000LL));
                ALOGI("%s line %d new IEC61937 frame parser done, diff is %"PRId64"\n", __func__, __LINE__, delta_frame_appear);
                aml_audio_trace_int("iec_parser_time", 0);
                if ((delta_frame_appear / 1000LL) > 32) {
                    ALOGI("%s line %d new IEC61937 frame parser done, diff is too large as %"PRId64"\n", __func__, __LINE__, delta_frame_appear);
                }
            }


            if (adev->continuous_audio_mode == 0) {
                aml_audio_trace_int("ms12_scheduler_run", dolby_ms12_input_bytes);
                //dolby_ms12_scheduler_run(ms12->dolby_ms12_ptr);
                aml_audio_trace_int("ms12_scheduler_run", 0);
            }

            if (n_bytes_decoder_consumed > 0) {
                /* Passthrough Mode, only get the MAIN data as the single input */
                if ((ms12_dec->codec_info.s32AdInput) && is_ad_data_available(adev->digital_audio_mode)) {
                    *use_size = n_bytes_decoder_consumed;

                } else {
                    if (adev->debug_flag >= 2) {
                        ALOGI("%s() continuous %d n_bytes_decoder_consumed %d input bytes %zu  main size %d parser size %d\n\n",
                              __FUNCTION__, adev->continuous_audio_mode, n_bytes_decoder_consumed, input_bytes, main_frame_size, single_decoder_used_bytes);
                    }

                    if (is_iec61937_format(stream)) {
                        *use_size = spdif_dec_used_size;
                    } else {
                        *use_size = n_bytes_decoder_consumed;
                        if (adev->continuous_audio_mode == 1 && !is_tv_stream_out(aml_out)) {
                            if (((ms12_hal_format == AUDIO_FORMAT_AC3)
                               || (ms12_hal_format == AUDIO_FORMAT_E_AC3)
                               || (ms12_hal_format == AUDIO_FORMAT_AC4))) {
                                *use_size = parser_used_size;
                            } else if (ms12_hal_format == AUDIO_FORMAT_IEC61937) {
                                *use_size = spdif_dec_used_size;
                            }
                        }
                    }
                }
            }
        } else {
            /* Passthrough Mode, only get the MAIN data as the single input */
            if ((ms12_dec->codec_info.s32AdInput) && is_ad_data_available(adev->digital_audio_mode)) {
                *use_size = input_bytes;

            } else {
                *use_size = input_bytes;
            }
        }

        ms12_dec->is_bypass_ms12 = is_ms12_passthrough(stream);
        aml_ms12_decoder_getparameter(ms12, ms12_dec, MS12_CODEC_PARAMETER_ATMOS_PRESENT, &(ms12_dec->is_dolby_atmos), sizeof(int));
        aml_ms12_decoder_getparameter(ms12, ms12_dec, MS12_CODEC_PARAMETER_REQUEST_FOCUS, &(ms12_dec->is_focus), sizeof(int));
        if (ms12_dec->is_focus) {
            update_ms12_focus_info(stream);
        }
exit:
        if (get_ms12_dump_enable(DUMP_MS12_INPUT_MAIN)) {
            dump_ms12_output_data((void*)buffer, *use_size, MS12_INPUT_SYS_MAIN_FILE);
        }
        ms12_dec->ms12_main_input_size += *use_size;
        ret = 0;
    } else {
        ret = -1;
    }
    pthread_mutex_unlock(&ms12_dec->main_lock);
    return ret;
}


/*
 *@brief dolby ms12 system process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */
int dolby_ms12_system_process(
    struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , size_t *use_size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    //audio_channel_mask_t mixer_default_channelmask = AUDIO_CHANNEL_OUT_STEREO;
    int mixer_default_samplerate = 48000;
    int dolby_ms12_input_bytes = 0;
    int ms12_output_size = 0;
    int ret = -1;

    if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
        check_audio_level("ms12_system", buffer, bytes);
    }

    pthread_mutex_lock(&ms12->lock);
    if (ms12->dolby_ms12_enable) {

        if (ms12->tv_tuning_flag && ms12->input_config_format == AUDIO_FORMAT_MAT) {
            ALOGW("MS12 use -tv_tuning Flag to activate a special processing graph for TV tuning purposes!\n");
            ALOGW("System sound is Mute as design!\n");
            pthread_mutex_unlock(&ms12->lock);
            return ret;
        }
        /*set the dolby ms12 debug level*/
        dolby_ms12_enable_debug();

        //Dual input, here get the system data
        dolby_ms12_input_bytes =
            dolby_ms12_input_system(
                ms12->dolby_ms12_ptr
                , buffer
                , bytes
                , aml_out->hal_format
                , aml_out->hal_ch
                , mixer_default_samplerate);
        if (dolby_ms12_input_bytes > 0) {
            *use_size = dolby_ms12_input_bytes;
            ret = 0;
        }else {
            *use_size = 0;
            ret = -1;
        }
    }
    if (get_ms12_dump_enable(DUMP_MS12_INPUT_SYS)) {
        dump_ms12_output_data((void*)buffer, *use_size, MS12_INPUT_SYS_PCM_FILE);
    }
    pthread_mutex_unlock(&ms12->lock);

    if (adev->continuous_audio_mode == 1) {
        uint64_t input_ns = 0;
        input_ns = (uint64_t)(*use_size) * NANO_SECOND_PER_SECOND / aml_out->hal_frame_size / mixer_default_samplerate;

        if (ms12->system_virtual_buf_handle == NULL) {
            //aml_audio_sleep(input_ns/1000);
            if (input_ns == 0) {
                input_ns = (uint64_t)(bytes) * NANO_SECOND_PER_SECOND / aml_out->hal_frame_size / mixer_default_samplerate;
            }
            audio_virtual_buf_open(&ms12->system_virtual_buf_handle, "ms12 system input", input_ns*0.8, MS12_SYS_INPUT_BUF_NS, 0, MS12_SYS_BUF_INCREASE_TIME_MS);
        }
        audio_virtual_buf_process(ms12->system_virtual_buf_handle, input_ns);
    }

    return ret;
}


int dolby_ms12_deep_buffer_process(
    struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , size_t *use_size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int mixer_default_samplerate = 48000;
    int dolby_ms12_input_bytes = 0;
    int ms12_output_size = 0;
    int ret = -1;

    if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
        check_audio_level("ms12_deep_buf", buffer, bytes);
    }

    pthread_mutex_lock(&ms12->lock);
    if (ms12->dolby_ms12_enable) {

        if (ms12->tv_tuning_flag && ms12->input_config_format == AUDIO_FORMAT_MAT) {
            ALOGW("MS12 use -tv_tuning Flag to activate a special processing graph for TV tuning purposes!\n");
            ALOGW("System sound is Mute as design!\n");
            pthread_mutex_unlock(&ms12->lock);
            return ret;
        }
        /*set the dolby ms12 debug level*/
        dolby_ms12_enable_debug();

        //Dual input, here get the system data
        dolby_ms12_input_bytes =
            dolby_ms12_input_deep_buffer(
                ms12->dolby_ms12_ptr
                , buffer
                , bytes
                , aml_out->hal_format
                , aml_out->hal_ch
                , mixer_default_samplerate);
        if (dolby_ms12_input_bytes > 0) {
            *use_size = dolby_ms12_input_bytes;
            ret = 0;
        }else {
            *use_size = 0;
            ret = -1;
        }
    }
    if (get_ms12_dump_enable(DUMP_MS12_INPUT_DEEP_BUF)) {
        dump_ms12_output_data((void*)buffer, *use_size, MS12_INPUT_DEEP_BUF_PCM_FILE);
    }
    pthread_mutex_unlock(&ms12->lock);

    if (adev->continuous_audio_mode == 1) {
        uint64_t input_ns = 0;
        input_ns = (uint64_t)(*use_size) * NANO_SECOND_PER_SECOND / aml_out->hal_frame_size / mixer_default_samplerate;

        if (ms12->deep_buf_virtual_buf_handle == NULL) {
            //aml_audio_sleep(input_ns/1000);
            if (input_ns == 0) {
                input_ns = (uint64_t)(bytes) * NANO_SECOND_PER_SECOND / aml_out->hal_frame_size / mixer_default_samplerate;
            }
            audio_virtual_buf_open(&ms12->deep_buf_virtual_buf_handle, "ms12 deep buf input", input_ns*0.8, MS12_DEEP_BUF_INPUT_BUF_NS, 0, MS12_DEEP_BUF_INCREASE_TIME_MS);
        }
        audio_virtual_buf_process(ms12->deep_buf_virtual_buf_handle, input_ns);
    }

    return ret;
}


/*
 *@brief dolby ms12 app process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */
int dolby_ms12_app_process(
    struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , size_t *use_size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    //audio_channel_mask_t mixer_default_channelmask = AUDIO_CHANNEL_OUT_STEREO;
    int mixer_default_samplerate = 48000;
    int dolby_ms12_input_bytes = 0;
    int ms12_output_size = 0;
    int ret = 0;
    if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
        check_audio_level("ms12_app", buffer, bytes);
    }

    if (ms12->dolby_ms12_enable) {
        /*set the dolby ms12 debug level*/
        dolby_ms12_enable_debug();

        //Dual input, here get the system data
        dolby_ms12_input_bytes =
            dolby_ms12_input_app(
                ms12->dolby_ms12_ptr
                , buffer
                , bytes
                , aml_out->hal_format
                , aml_out->hal_ch
                , mixer_default_samplerate);
        if (dolby_ms12_input_bytes > 0) {
            *use_size = dolby_ms12_input_bytes;
            ret = 0;
        } else {
            *use_size = 0;
            ret = -1;
        }
    }
    if (get_ms12_dump_enable(DUMP_MS12_INPUT_APP)) {
        dump_ms12_output_data((void*)buffer, *use_size, MS12_INPUT_SYS_APP_FILE);
    }

    return ret;
}


int dolby_ms12_multi_app_process(
    struct dolby_ms12_desc *ms12
    , const void *buffer
    , size_t bytes
    , size_t *use_size
    , const struct audioCfg *pstAudioConfig
    , bool bConfigUpdate)
{
    int mixer_default_samplerate = 48000;
    int dolby_ms12_input_bytes = 0;
    int ms12_output_size = 0;
    int ret = 0;

    if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
        check_audio_level("ms12_app", buffer, bytes);
    }
    if (pstAudioConfig->sampleRate != mixer_default_samplerate) {
        AM_LOGE("not support sampleRate %d", pstAudioConfig->sampleRate);
        return -1;
    }

    if (bConfigUpdate && ms12->dolby_ms12_enable) {
        set_ms12_app_pcm_acmod_lfe(ms12, pstAudioConfig->channelMask);
        dolby_ms12_app_flush();
    }

    if (ms12->dolby_ms12_enable) {
        /*set the dolby ms12 debug level*/
        dolby_ms12_enable_debug();

        dolby_ms12_input_bytes =
            dolby_ms12_input_app(
                ms12->dolby_ms12_ptr
                , buffer
                , bytes
                , pstAudioConfig->format
                , pstAudioConfig->channelCnt
                , mixer_default_samplerate);
        if (dolby_ms12_input_bytes > 0) {
            *use_size = dolby_ms12_input_bytes;
            ret = 0;
        } else {
            *use_size = 0;
            ret = -1;
        }
    }
    if (get_ms12_dump_enable(DUMP_MS12_INPUT_APP)) {
        dump_ms12_output_data((void*)buffer, *use_size, MS12_INPUT_SYS_APP_FILE);
    }

    return ret;
}

static void close_all_ms12_dec() {

    /*close all the ms12 main decoder*/
    struct aml_audio_device *adev = aml_adev_get_handle();
    struct aml_stream_out *amlStream = NULL;
    bool retValue = false;
    ALOGI("%s close all ms12 decoder", __func__);
    /*we will check all the stream, need lock it first*/
    pthread_mutex_lock(&adev->streamList_MutexLock);
    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;
            amlStream = (struct aml_stream_out *)ptmp->pStream;
            if (amlStream->is_ms12_main_decoder && amlStream->ms12_dec_handle) {
                amlStream->is_ms12_main_decoder_disable = true;
                dolby_ms12_main_close((struct audio_stream_out *)amlStream);
                ALOGI("%s close stream =%p", __func__, amlStream);
            }
        }
    }
    pthread_mutex_unlock(&adev->streamList_MutexLock);

    return;
}
/*
 *@brief get dolby ms12 cleanup
 */
int get_dolby_ms12_cleanup(struct dolby_ms12_desc *ms12, bool set_non_continuous)
{
    int is_quit = 1;
    int i = 0;
    struct aml_audio_device *adev = NULL;
    unsigned int remaining_time = 0;
    ALOGI("+%s()", __FUNCTION__);
    if (!ms12) {
        ALOGI("-%s()  exit.", __FUNCTION__);
        return -EINVAL;
    }
    adev = ms12_to_adev(ms12);
    adev->ms12_to_be_cleanup = true;


   /*close all the ms12 dec before cleanup ms12*/
    close_all_ms12_dec();

    pthread_mutex_lock(&ms12->lock);

    if (!ms12->dolby_ms12_init_flags || (ms12->dolby_ms12_enable == false)) {
        ALOGI("ms12 is not init, don't need cleanup");
        if (set_non_continuous) {
            adev->continuous_audio_mode = 0;
            ALOGI("%s set ms12 to non continuous mode", __func__);
        }
        goto exit;
    }

    ALOGI("++%s(), locked", __FUNCTION__);

    /* check timers is running or not,
    ** timer should be stopped if running.
    **/
    remaining_time = audio_timer_remaining_time(ms12->ms12_timer_id);
    if (remaining_time > 0) {
        audio_timer_stop(ms12->ms12_timer_id);
    }
    int ret = aml_audio_timer_delete(ms12->ms12_timer_id);
    ALOGD("func:%s timer_id:%d  ret:%d",__func__, ms12->ms12_timer_id, ret);

    ALOGI("%s() dolby_ms12_set_quit_flag %d", __FUNCTION__, is_quit);
    dolby_ms12_set_quit_flag(is_quit);

    if (ms12->dolby_ms12_threadID != 0) {
        ms12->dolby_ms12_thread_exit = true;
        if (ms12->ms12_continuous_state == MS12_SCHEDULER_STANDBY) {
            sem_post(&ms12->standby_sem);
        }
        pthread_join(ms12->dolby_ms12_threadID, NULL);
        ms12->dolby_ms12_threadID = 0;
        ALOGI("%s() dolby_ms12_threadID reset to %ld\n", __FUNCTION__, ms12->dolby_ms12_threadID);
    }
    if (sem_destroy(&ms12->standby_sem)) {
        ALOGE("%s release ms12 standby semaphore failed\n", __FUNCTION__);
    } else {
        ALOGD("%s release ms12 standby semaphore successful\n", __FUNCTION__);
    }

    //Todo
    if (adev->board_config.ai_de_config == 1) {
        dolby_ms12_continuous_unregister_callback(ms12->dolby_ms12_ptr, MS12_CONTINUOUS_CALLBACK_CONTENT_PROCESS);
    }

    set_audio_system_format(AUDIO_FORMAT_INVALID);
    set_audio_app_format(AUDIO_FORMAT_INVALID);
    set_audio_main_format(AUDIO_FORMAT_INVALID);
    dolby_ms12_config_params_set_system_flag(false);
    dolby_ms12_config_params_set_app_flag(false);
    dolby_ms12_set_enforce_timeslice(false);
    dolby_ms12_set_tv_tuning_flag(false);
    aml_ms12_cleanup(ms12);
    ms12->output_config = 0;
    ms12->dolby_ms12_enable = false;
    ms12->input_total_ms = 0;
    ms12->bitstream_cnt = 0;
    ms12->nbytes_of_dmx_output_pcm_frame = 4; //2ch * 16bit, set a default one
    ms12->last_frames_position = 0;
    ms12->dolby_ms12_init_flags = false;
    ms12->dtv_decoder_offset_base = 0;
    ms12->focus_is_bypass_ms12 = 0;
    ms12->last_focus_is_bypass_ms12 = 0;
    audio_continuous_standby_close(&ms12->continuous_standby_handle);

    audio_continuous_standby_close(&ms12->continuous_standby_handle);

    audio_virtual_buf_close(&ms12->system_virtual_buf_handle);

    ring_buffer_release(&ms12->spdif_ring_buffer);
    if (ms12->mat_enc_out_buffer) {
        aml_audio_free(ms12->mat_enc_out_buffer);
        ms12->mat_enc_out_buffer = NULL;
    }
    if (ms12->mat_enc_handle) {
        dolby_ms12_mat_encoder_cleanup(ms12->mat_enc_handle);
        ms12->mat_enc_handle = NULL;
    }
    ms12->ms12_scheduler_state = MS12_SCHEDULER_NONE;
    ms12->last_scheduler_state = MS12_SCHEDULER_NONE;
    ms12_close_all_spdifout(ms12);
    if (ms12->iec61937_ddp_buf) {
        aml_audio_free(ms12->iec61937_ddp_buf);
        ms12->iec61937_ddp_buf = NULL;
    }
    if (ms12->scaletempo) {
        hal_scaletempo_release((struct scale_tempo *)ms12->scaletempo);
        ms12->scaletempo = NULL;
    }
    pthread_mutex_destroy(&ms12->main_apts_update_lock);

    /*because we are still in lock, we can set continuous_audio_mode here safely*/
    if (set_non_continuous) {
        adev->continuous_audio_mode = 0;
        ALOGI("%s set ms12 to non continuous mode", __func__);
    }
#ifdef SUPPORT_KARAOKE
    karaoke_close(&adev->usb_audio.karaoke);
    karaoke_close(&adev->linein_karaoke);
#endif
    adev->ms12_out = NULL;
exit:
    ALOGI("--%s(), locked", __FUNCTION__);
    pthread_mutex_unlock(&ms12->lock);
    adev->ms12_to_be_cleanup = false;
    ALOGI("-%s()", __FUNCTION__);
    return 0;
}

/*
 *@brief set dolby ms12 primary gain
 */
int set_dolby_ms12_primary_input_db_gain(struct dolby_ms12_desc *ms12, int db_gain , int duration)
{
    MixGain gain;
    int ret = 0;

    ALOGI("+%s(): gain %ddb, ms12 enable(%d)",
          __FUNCTION__, db_gain, ms12->dolby_ms12_enable);

    gain.target = db_gain;
    gain.duration = duration;
    gain.shape = 0;
    dolby_ms12_set_system_sound_mixer_gain_values_for_primary_input(&gain);
    //dolby_ms12_set_input_mixer_gain_values_for_main_program_input(&gain);
    //Fixme when tunnel mode is working, the Alexa start and mute the main input!
    //dolby_ms12_set_input_mixer_gain_values_for_ott_sounds_input(&gain);
    // only update very limited parameter with out lock
    //ret = aml_ms12_update_runtime_params_lite(ms12);

exit:
    return ret;
}

static ssize_t aml_ms12_spdif_output_new (struct audio_stream_out *stream,
                                struct bitstream_out_desc * bitstream_desc,
                                audio_format_t output_format,
                                audio_format_t sub_format,
                                int sample_rate,
                                int data_ch,
                                int ch_mask,
                                void *buffer,
                                size_t byte)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    spdif_config_t spdif_config = { 0 };
    struct dolby_ms12_desc *ms12 = &(adev->ms12);

    int ret = 0;

    pthread_mutex_lock(&adev->bitstream_lock);
    /*some switch happen*/
    if (bitstream_desc->spdifout_handle != NULL && bitstream_desc->audio_format != output_format) {
        ALOGI("spdif output format changed from =0x%x to 0x%x", bitstream_desc->audio_format, output_format);
        aml_audio_spdifout_close(bitstream_desc->spdifout_handle);
        ALOGI("%s spdif format changed from 0x%x to 0x%x", __FUNCTION__, bitstream_desc->audio_format, output_format);
        bitstream_desc->spdifout_handle = NULL;
    }


    if (bitstream_desc->spdifout_handle == NULL) {
        /*we need update ms12 optical_format in the master pcm output*/
        if (ms12->optical_format != adev->optical_format) {
            pthread_mutex_unlock(&adev->bitstream_lock);
            ALOGI("wait ms12 optical format update");
            return -1;
        }

        if (output_format == AUDIO_FORMAT_IEC61937) {
            spdif_config.audio_format = AUDIO_FORMAT_IEC61937;
            spdif_config.sub_format   = sub_format;
        } else {
            spdif_config.audio_format = output_format;
            spdif_config.sub_format   = output_format;
        }
        spdif_config.rate = sample_rate;
        /*for mat output, the rate should be 768 and 2ch, here we set 192, driver will convert to 768*/
        if (output_format == AUDIO_FORMAT_MAT) {
            spdif_config.rate = sample_rate * 4;
        }
        spdif_config.channel_mask = ch_mask;
        spdif_config.data_ch      = data_ch;
        bitstream_desc->sample_rate = spdif_config.rate;
        ret = aml_audio_spdifout_open(&bitstream_desc->spdifout_handle, &spdif_config);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->bitstream_lock);
            ALOGE("open spdif out failed\n");
            return ret;
        }
        //bitstream_desc->is_bypass_ms12 = ms12->is_bypass_ms12;
        /*for bypass case, we drop some data at the beginning to make sure it is stable*/
        //ALOGI("is ms12 bypass =%d", ms12->is_bypass_ms12);
        if (bitstream_desc->is_bypass_ms12) {
            bitstream_desc->need_drop_frame = MS12_BYPASS_DROP_CNT;
        }
    }

    bitstream_desc->audio_format = output_format;
    bitstream_desc->sub_format = sub_format;

    if (bitstream_desc->is_bypass_ms12) {
        if (ms12->main_volume < FLOAT_ZERO) {
            aml_audio_spdifout_mute(bitstream_desc->spdifout_handle, 1);
        } else {
            aml_audio_spdifout_mute(bitstream_desc->spdifout_handle, 0);
        }
    }
    ret = aml_audio_spdifout_process(bitstream_desc->spdifout_handle, buffer, byte);

    /*it is earc output*/
    if ((adev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) != 0) {
        aml_audio_spdifout_config_earc_ca(bitstream_desc->spdifout_handle, ch_mask);
    }
    pthread_mutex_unlock(&adev->bitstream_lock);

    return ret;
}

int ac3_and_eac3_bypass_process(struct audio_stream_out *stream, void *buffer, size_t bytes) {
    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
    struct bitstream_out_desc *bitstream_out_b = &ms12->bitstream_out[BITSTREAM_OUTPUT_B];
    audio_format_t output_format =  ms12_get_audio_hal_format(aml_out->hal_format);
    ALOGV("[%s:%d]output_format=0x%x hal_format=0x%#x internal=0x%x",__FUNCTION__,__LINE__, output_format, aml_out->hal_format, aml_out->hal_internal_format);
    bool is_dolby = (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) || (aml_out->hal_internal_format == AUDIO_FORMAT_AC3);
    spdif_config_t spdif_config = { 0 };
    audio_format_t hal_internal_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);
    aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)aml_out->audio_buffer;
    aml_audio_buffer_t *audioBuffer = pBuffer->inBuffer;

    if (!buffer) {
        ALOGE("%s buffer is null ", __func__);
        return -1;
    }
    /*for patch mode, the hal_rate is not correct, we should parse it*/
    if (is_dolby) {
        struct ac3_parser_info ac3_info = { 0 };
        void *main_frame_buffer = NULL;
        int32_t main_frame_size = 0;
        int32_t parser_used_size = 0;
        int32_t offset = 0;
        int32_t bytes_left = bytes;

        if (!aml_out->ac3_parser_init) {
            aml_ac3_parser_open(&aml_out->ac3_parser_handle);
            aml_out->ac3_parser_init = true;
        }

        do {
            aml_ac3_parser_process(aml_out->ac3_parser_handle, (char *)buffer + offset, bytes_left, &parser_used_size, &main_frame_buffer, &main_frame_size, &ac3_info);
            offset += parser_used_size;
            if (bytes_left >= parser_used_size) {
                bytes_left -= parser_used_size;
            } else {
                bytes_left = 0;
            }
            if (parser_used_size == 0) {
                ALOGE("%s error", __func__);
                break;
            }

            if (ac3_info.sample_rate != 0 && main_frame_size) {
                aml_out->hal_rate = ac3_info.sample_rate;
                aml_out->decoded_frame += ac3_info.numblks * SAMPLE_NUMS_IN_ONE_BLOCK;
                ALOGV("aml_out->decoded_frame =%" PRIu64 "", aml_out->decoded_frame);
            }

            //for Tv-61707, the format of PMT table is different with the actual format,
            //case 1: the aml_out->hal_internal_format is AUDIO_FORMAT_AC3 and the actual format is AUDIO_FORMAT_E_AC3,
            //case 2: the aml_out->hal_internal_format is AUDIO_FORMAT_E_AC3 and the actual format is AUDIO_FORMAT_AC3,
            //so we need to judge the format whether or not there are accurate depending on the ac3_info.nIsEc3.
            if (ac3_info.nIsEc3 == 1 && aml_out->hal_internal_format == AUDIO_FORMAT_AC3  && (aml_out->ms12_dec_handle->codec_info.s32AdInput == false)) {
                ALOGV("output_format=0x%x hal_format=0x%#x internal=0x%x nIsEc3 = %d",output_format, aml_out->hal_format, aml_out->hal_internal_format,ac3_info.nIsEc3);
                aml_out->hal_internal_format = AUDIO_FORMAT_E_AC3;
                output_format = AUDIO_FORMAT_E_AC3;
                if (audioBuffer->isDtv) {
                    aml_out->hal_internal_format = AUDIO_FORMAT_E_AC3;
                }

            }
        } while(bytes_left != 0);

    }

    aml_out->ms12_dec_handle->is_bypass_ms12 = is_ms12_passthrough(stream);
    if (aml_out->ms12_dec_handle->is_bypass_ms12
        && is_dolby) {
        if (bytes != 0 && buffer != NULL) {
            bool no_bitstream_ready = ((bitstream_out->spdifout_handle != NULL) && (bitstream_out->is_bypass_ms12 == 0));
            /*if the bitstream is still open, it need to be closed in ms12 output thread first*/
            if (no_bitstream_ready) {
                int wait_cnt = 0;
                do {
                    if (no_bitstream_ready) {
                        usleep(10*1000);
                        wait_cnt++;
                    } else {
                        ALOGI("%s wait bitstream closed cnt =%d", __func__, wait_cnt);
                        break;
                    }
                    if (wait_cnt > 10) {
                        ALOGI("%s wait bitstream closed timeout, and close spdif directly", __func__);
                        aml_audio_spdifout_close(bitstream_out->spdifout_handle);
                        bitstream_out->spdifout_handle = NULL;
                        return 0;
                    }
                    no_bitstream_ready = ((bitstream_out->spdifout_handle != NULL) && (bitstream_out->is_bypass_ms12 == 0));
                } while (1);
            }

            pthread_mutex_lock(&adev->bitstream_lock);
            if ((bitstream_out->spdifout_handle != NULL ) &&
                ((bitstream_out->audio_format != output_format) ||
                (bitstream_out->sample_rate !=  aml_out->hal_rate))) {
                aml_audio_spdifout_close(bitstream_out->spdifout_handle);
                ALOGI("%s spdif format changed from 0x%x to 0x%x", __FUNCTION__, bitstream_out->audio_format, output_format);
                bitstream_out->spdifout_handle = NULL;
            }

            if ((bitstream_out_b->spdifout_handle != NULL) &&
                //this is dd output, 44.1 ddp files in passthrough mode should output 48k dd/44.1k ddp.
                //here should not close spdifout. Or dd output will be close and open always.
                (aml_out->hal_internal_format != AUDIO_FORMAT_E_AC3 && bitstream_out_b->sample_rate != aml_out->hal_rate)) {
                aml_audio_spdifout_close(bitstream_out_b->spdifout_handle);
                ALOGI("%s spdif_b format changed from 0x%x to 0x%x", __FUNCTION__, bitstream_out_b->audio_format, output_format);
                bitstream_out_b->spdifout_handle = NULL;
            }

            if (bitstream_out->spdifout_handle == NULL) {
                if (output_format == AUDIO_FORMAT_IEC61937) {
                    spdif_config.audio_format = AUDIO_FORMAT_IEC61937;
                    spdif_config.sub_format   = aml_out->hal_internal_format;
                } else {
                    spdif_config.audio_format = output_format;
                    spdif_config.sub_format   = output_format;
                }
                spdif_config.rate = DDP_OUTPUT_SAMPLE_RATE;
                if (aml_out->hal_rate == 44100 ||
                    aml_out->hal_rate == 176400) {
                    spdif_config.rate = 44100;
                } else if (aml_out->hal_rate == 32000 ||
                           aml_out->hal_rate == 128000) {
                    spdif_config.rate = 32000;
                }
                spdif_config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                spdif_config.data_ch = 2;
                bitstream_out->sample_rate = spdif_config.rate;
                bitstream_out->is_bypass_ms12 = aml_out->ms12_dec_handle->is_bypass_ms12;
                ret = aml_audio_spdifout_open(&bitstream_out->spdifout_handle, &spdif_config);
                if (ret != 0) {
                    pthread_mutex_unlock(&adev->bitstream_lock);
                    ALOGE("%s open spdif out failed\n", __func__);
                    return ret;
                }
            }
            pthread_mutex_unlock(&adev->bitstream_lock);
        }

        bitstream_out->audio_format = output_format;
        pthread_mutex_lock(&adev->bitstream_lock);
        if (bitstream_out->spdifout_handle) {
            if (ms12->main_volume < FLOAT_ZERO) {
                aml_audio_spdifout_mute(bitstream_out->spdifout_handle, 1);
            } else {
                aml_audio_spdifout_mute(bitstream_out->spdifout_handle, 0);
            }
        }
        pthread_mutex_unlock(&adev->bitstream_lock);

#ifdef ENABLE_DVB_PATCH
        if (is_dtv_stream_out(stream) && aml_out->dtvsync_enable) {
            aml_dtvsync_t *aml_dtvsync =(aml_dtvsync_t *)aml_out->hwsync->mediasync;
            int alsa_bitstream_delay_ms = out_get_ms12_bitstream_latency_ms(stream);
            int64_t alsa_latency = (alsa_bitstream_delay_ms >= 0) ? (alsa_bitstream_delay_ms * MILLISECOND_2_PTS) : 0;
            int64_t ms12_bypass_tuning_pts = dtv_get_ms12_bypass_latency_offset()/*ms*/ * MILLISECOND_2_PTS;
            if (aml_dtvsync && audioBuffer && ( audioBuffer->apts != DTVSYNC_INVALID_PTS)) {
                /* Fixme: if there are multi frames in the dolby raw data, how to update the pts? */
                if ((aml_dtvsync->out_end_apts >  audioBuffer->apts)
                    && (DIFF_ABS(aml_dtvsync->last_package_pts, aml_dtvsync->out_end_apts) <= AUDIO_PTS_DISCONTINUE_THRESHOLD))
                    aml_dtvsync->out_start_apts = aml_dtvsync->out_end_apts;
                else
                    aml_dtvsync->out_start_apts = audioBuffer->apts;
                if (aml_dtvsync->out_start_apts == DTVSYNC_INIT_PTS) {
                    /*invalid pts */
                    aml_dtvsync->cur_outapts = DTVSYNC_INIT_PTS;
                }
                else {
                    aml_dtvsync->cur_outapts = aml_dtvsync->out_start_apts - alsa_latency + ms12_bypass_tuning_pts;
                }
                if (adev->debug_flag > 1) {
                    ALOGI("%s last_package_pts  %" PRIx64 " out_end_apts %" PRIx64 " diff is %" PRIx64 " (max:5*90000)",
                        __func__, aml_dtvsync->last_package_pts, aml_dtvsync->out_end_apts, DIFF_ABS(aml_dtvsync->last_package_pts, aml_dtvsync->out_end_apts));
                    ALOGI("%s package pts(ms) %" PRIu64 " start_pts(ms) %" PRIu64 " cur_outapts(ms) %" PRIu64 ", alsa_latency(ms) %" PRId64 "\n",
                        __func__, audioBuffer->apts / 90, aml_dtvsync->out_start_apts / 90, aml_dtvsync->cur_outapts / 90, alsa_latency / 90);
                    ALOGI("%s package pts %" PRIx64 " start_pts %" PRIx64 " cur_outapts %" PRIx64 ", alsa_latency %" PRIx64 "\n",
                        __func__, audioBuffer->apts, aml_dtvsync->out_start_apts, aml_dtvsync->cur_outapts, alsa_latency);
                }
            }
        }

        if (is_dtv_stream_out(stream) && aml_out->dtvsync_enable) {
            aml_dtvsync_t *aml_dtvsync = (aml_dtvsync_t *)aml_out->hwsync->mediasync;
            struct dtvsync_audio_policy *async_policy = NULL;
             if (aml_dtvsync != NULL) {
                 ms12_do_dtv_sync(stream);
                 async_policy = &(aml_dtvsync->apolicy);
                 if (async_policy->audiopolicy == DTVSYNC_AUDIO_DROP_PCM) {
                    return 0;
                 }
             }
        }
#endif
        /*
        **ms12 callback write dd stream and ac3 bypass write dd stream, the two thread use the same alsa
        **device(dd) handle. It will appear that alsa device of ac3 bypss is closed by upper bitstream_out_b
        **detected aml_audio_spdifout_close logic.
        **that leads to this scene, spdifout_handle is not null but the alsa handle is null.
        **Here add a protect to reopen alsa device by closing spdifout_handle.
        */
        pthread_mutex_lock(&adev->bitstream_lock);
        ret = aml_audio_spdifout_process(bitstream_out->spdifout_handle, buffer, bytes);
        if (ret == AML_SPDIFOUT_PROCESS_ALSA_IS_NULL) {
            ALOGW("%s: close spdifout %p, then auto reopen it next call", __func__, bitstream_out->spdifout_handle);
            aml_audio_spdifout_close(bitstream_out->spdifout_handle);
            bitstream_out->spdifout_handle = NULL;
        }
#ifdef ENABLE_DVB_PATCH
        if (is_dtv_stream_out(stream) && aml_out->dtvsync_enable) {
            aml_dtvsync_t *aml_dtvsync = (aml_dtvsync_t *)aml_out->hwsync->mediasync;
            int spdifout_duration = get_aml_audio_spdifout_duration(bitstream_out->spdifout_handle);
            if (adev->debug_flag > 1) {
                ALOGI("%s line %d format 0x%x spdif out duration %d", __func__, __LINE__, bitstream_out->audio_format, spdifout_duration);
            }
            if (spdifout_duration > 0) {
                int cur_bitstream_pts = spdifout_duration * MILLISECOND_2_PTS;
                aml_dtvsync->out_end_apts = aml_dtvsync->out_start_apts + cur_bitstream_pts;
            }
        }
#endif

        pthread_mutex_unlock(&adev->bitstream_lock);
    }

    return 0;
}

static int dolby_mat_encoder_process(struct dolby_ms12_desc *ms12, void *buffer, size_t bytes, int *nbytes_consumed)
{
    int ret = 0;
    int offset = 0;
    unsigned char *pbuf = (unsigned char *)buffer;
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];

    while (offset < bytes) {
        ret = dolby_ms12_mat_encoder_process
            (ms12->mat_enc_handle
            , (const unsigned char *)(pbuf + offset)
            , (bytes - offset)
            , (const unsigned char *)ms12->mat_enc_out_buffer
            , &ms12->mat_enc_out_bytes
            , ms12->matenc_maxoutbufsize
            , nbytes_consumed
            );
        ALOGV("[%s:%d] bytes %zu, offset %d, nbytes_consumed %d, mat_enc_out_bytes %d\n", __func__, __LINE__, bytes, offset, *nbytes_consumed, ms12->mat_enc_out_bytes);
        if (ret) {
            ALOGE("[%s:%d] mat_encoder_process error %d \n", __func__, __LINE__, ret);
            if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_BITSTREAM_MAT_WI_MLP)) {
                dump_ms12_output_data(buffer, bytes, "/data/vendor/audiohal/mat_enc_error.thd");
            }
            /* try to re-init the mat encoder */
            if (ms12->mat_enc_handle) {
                dolby_ms12_mat_encoder_cleanup(ms12->mat_enc_handle);
                ms12->mat_enc_handle = NULL;
            }
            break;
        }

        /* update the offset with the nbytes_consumed */
        offset += *nbytes_consumed;

        /* when (mat encoder output data(mat_enc_out_bytes) not 0), send them to alsa */
        if (ms12->mat_enc_out_bytes) {
            endian16_convert(ms12->mat_enc_out_buffer, ms12->mat_enc_out_bytes);
            aml_audio_spdifout_process
                        (bitstream_out->spdifout_handle
                        , ms12->mat_enc_out_buffer
                        , ms12->mat_enc_out_bytes);
            /*
             * usage to dump the IEC61937 within MAT(TrueHD inside):
             *        setenforce 0
             *        mkdir -p /data/vendor/audiohal/
             *        rm /data/vendor/audiohal/
             *        chmod 777 /data/vendor/audiohal/
             *        setprop vendor.media.audiohal.ms12dump 0x20
             */
            if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_BITSTREAM_MAT_WI_MLP)) {
                dump_ms12_output_data(ms12->mat_enc_out_buffer, ms12->mat_enc_out_bytes, MS12_OUTPUT_BITSTREAM_MAT_WI_MLP_FILE);
            }
            /* after write the IEC61937 data to hardware, reset it to zero position. */
            ms12->mat_enc_out_bytes = 0;
        }

    }

    return ret;

}


int dolby_truehd_bypass_process(struct audio_stream_out *stream, void *buffer, size_t bytes) {
    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
    /*
     *only Dolby TrueHD should use Dolby MAT Encoder w/i MS12.
     */
    bool is_dolby_truehd = (aml_out->hal_internal_format == AUDIO_FORMAT_DOLBY_TRUEHD);
    audio_format_t output_format = AUDIO_FORMAT_IEC61937; //suppose MAT encoder always output IEC61937 format.
    if (ms12->mat_enc_debug_enable) {
        ALOGI("[%s:%d] output_format=0x%x, hal_format=0x%#x, internal=0x%x\n", __func__, __LINE__, output_format, aml_out->hal_format, aml_out->hal_internal_format);
    }
    spdif_config_t spdif_config = { 0 };
    audio_format_t hal_internal_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);


    ms12_dec->is_bypass_ms12 = is_ms12_passthrough(stream);
    if (ms12_dec->is_bypass_ms12
        && is_dolby_truehd) {
        /*
         * First of all, initialize the MAT Encoder with the b_lfract_precision(1)/b_chmod_locking(0)/b_iec_header(1).
         * If MAT encoder is successfully built, we got the mat_encoder_handle and matenc_maxoutbufsize.
         * then malloc the mat_enc_out_buffer by the matenc_maxoutbufsize.
         */
        if (!ms12->mat_enc_handle) {
            int b_lfract_precision = 1;
            int b_chmod_locking = 0;
            ms12->b_iec_header = 1; //mat encoder output the IEC61937 format.
            ret = dolby_ms12_mat_encoder_init
                    ( b_lfract_precision
                    , b_chmod_locking
                    , &(ms12->matenc_maxoutbufsize) //get the matenc_maxoutbufsize for the mat_enc_out_buffer.
                    , ms12->b_iec_header
                    , ms12->mat_enc_debug_enable
                    , (void **)&ms12->mat_enc_handle
                    );
            if (ret) {
                ALOGE("%s mat_encoder_init failed (%d)\n", __func__, ret);
                return ret;
            }
            else {
                ms12->matenc_maxoutbufsize *= 4;
                ALOGD("%s matenc_maxoutbufsize %d\n", __func__, ms12->matenc_maxoutbufsize);
                if (!ms12->mat_enc_out_buffer) {
                    ms12->mat_enc_out_buffer = (char *)aml_audio_malloc(ms12->matenc_maxoutbufsize);
                    if (!ms12->mat_enc_out_buffer) {
                        ALOGE("%s ms12->mat_enc_out_buffer malloc failed\n", __func__);
                        return ret;
                    }
                }
            }
        }

        if (bytes != 0 && buffer != NULL) {
            /*
             * if the format/sample-rate are changed, restart the alsa-card.
             */
            pthread_mutex_lock(&adev->bitstream_lock);
            if ((bitstream_out->spdifout_handle != NULL )&&
                (bitstream_out->audio_format != output_format)) {
                aml_audio_spdifout_close(bitstream_out->spdifout_handle);
                ALOGI("%s spdif format changed from 0x%x to 0x%x", __FUNCTION__, bitstream_out->audio_format, output_format);
                bitstream_out->spdifout_handle = NULL;
            }

            /*
             * if the alsa(use the spdif sound card) out handle is invalid, initialize it immediately.
             */
            if (bitstream_out->spdifout_handle == NULL) {
                spdif_config.audio_format = AUDIO_FORMAT_IEC61937;
                spdif_config.sub_format = AUDIO_FORMAT_MAT;
                /*
                 * FIXME:
                 *      configure the MAT encoder's sample rate as 48kHZ.
                 *      so, here use the 4*48000(192k)Hz as the spdif config rate.
                 *      if input is 44.1kHz truehd, after MAT encoder, the sample rate should be always 48kHz.
                 *      If it is not suitable, please report it.
                 */
                spdif_config.rate = (4 * TRUEHD_OUTPUT_SAMPLE_RATE);
                spdif_config.channel_mask = AUDIO_CHANNEL_OUT_7POINT1;
                spdif_config.data_ch = 8;
                bitstream_out->sample_rate = spdif_config.rate;
                ret = aml_audio_spdifout_open(&bitstream_out->spdifout_handle, &spdif_config);
                if (ret != 0) {
                    ALOGE("%s open spdif out failed\n", __func__);
                    pthread_mutex_unlock(&adev->bitstream_lock);
                    return ret;
                }
                bitstream_out->is_bypass_ms12 = ms12_dec->is_bypass_ms12;
            }
            pthread_mutex_unlock(&adev->bitstream_lock);
        }

        bitstream_out->audio_format = output_format;
        /*
         * control the mute flag to mute/unmute the spdif out.
         */
        pthread_mutex_lock(&adev->bitstream_lock);
        if (bitstream_out->spdifout_handle) {
            if (ms12->main_volume < FLOAT_ZERO) {
                aml_audio_spdifout_mute(bitstream_out->spdifout_handle, 1);
            } else {
                aml_audio_spdifout_mute(bitstream_out->spdifout_handle, 0);
            }
        }
        pthread_mutex_unlock(&adev->bitstream_lock);

        /*
         * get the IEC61937 format audio data by the mat encoder, and write it to hardware.
         */
        if (ms12->mat_enc_handle && buffer && bytes) {
            int parser_offset = 0;
            int parser_consumed = 0;
            unsigned char *pbuf = (unsigned char *)buffer;
            void *frame_buffer = NULL;
            int frame_buffer_size = 0;
            while (parser_offset < bytes) {
                /*truehd parser: Split the data into individual access units*/
                ret = aml_truehd_parser_process(
                    ms12_dec->truehd_parser_handle
                    , (const unsigned char *)(pbuf + parser_offset)
                    , (bytes - parser_offset)
                    , &parser_consumed
                    , &frame_buffer
                    , &frame_buffer_size);
                /*parse fail or not enough data.*/
                if (ret < 0) {
                    ALOGE("[%s:%d] parse fail or not enough data! ret = %d", __func__, __LINE__, ret);
                    break;
                }
                if (ms12->mat_enc_debug_enable) {
                    ALOGI("[%s:%d] bytes %zu, parser_offset %d, parser_consumed %d, mlp access unit length %d\n",
                       __func__, __LINE__ , bytes, parser_offset, parser_consumed, frame_buffer_size);
                }

                /* update the parser_offset with the parser_consumed */
                parser_offset += parser_consumed;

                //Arriving here indicates that frame_buffer already includes one access unit.
                if (frame_buffer && (frame_buffer_size > 0)) {
                    if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_BITSTREAM_MAT_WI_MLP)) {
                        dump_ms12_output_data(frame_buffer, frame_buffer_size, AML_PARSED_TRUEHD_FILE);
                    }
                    int nbytes_matenc_consumed = 0;
                    ret = dolby_mat_encoder_process(ms12, frame_buffer, frame_buffer_size, &nbytes_matenc_consumed);

                    if (ret) {
                        ALOGE("[%s:%d] mat enc error! ret %d, nbytes_matenc_consumed %d\n", __func__, __LINE__ , ret, nbytes_matenc_consumed);
                        continue;
                    }
                }

            }
        }
    }

    return 0;
}

int mat_bypass_process(struct audio_stream_out *stream, void *buffer, size_t bytes) {
    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
    bool is_mat = (aml_out->hal_internal_format == AUDIO_FORMAT_MAT);
    audio_format_t output_format = AUDIO_FORMAT_IEC61937; //suppose MAT encoder always output IEC61937 format.
    ALOGV("output_format=0x%x hal_format=0x%#x internal=0x%x", output_format, aml_out->hal_format, aml_out->hal_internal_format);
    spdif_config_t spdif_config = { 0 };
    audio_format_t hal_internal_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);
    if (!buffer) {
        ALOGE("%s buffer is NULL\n", __func__);
        return -1;
    }

    ms12_dec->is_bypass_ms12 = is_ms12_passthrough(stream);
    if (ms12_dec->is_bypass_ms12
        && is_mat) {

        pthread_mutex_lock(&adev->bitstream_lock);
        if (bytes != 0 && buffer != NULL) {
            /*
             * if the format/sample-rate are changed, restart the alsa-card.
             */
            if ((bitstream_out->spdifout_handle != NULL )&&
                (bitstream_out->audio_format != output_format)) {
                aml_audio_spdifout_close(bitstream_out->spdifout_handle);
                ALOGI("%s spdif format changed from 0x%x to 0x%x", __FUNCTION__, bitstream_out->audio_format, output_format);
                bitstream_out->spdifout_handle = NULL;
            }

            /*
             * if the alsa(use the spdif sound card) out handle is invalid, initialize it immediately.
             */
            if (bitstream_out->spdifout_handle == NULL) {
                spdif_config.audio_format = AUDIO_FORMAT_IEC61937;
                spdif_config.sub_format = aml_out->hal_internal_format;
                /*
                 * FIXME:
                 *      here use the 4*48000(192k)Hz as the spdif config rate.
                 *      if input is 44.1kHz truehd, maybe there is abnormal sound.
                 *      If it is not suitable, please report it.
                 */
                spdif_config.rate = MAT_OUTPUT_SAMPLE_RATE;
                spdif_config.channel_mask = AUDIO_CHANNEL_OUT_7POINT1;
                spdif_config.data_ch = 8;
                bitstream_out->sample_rate = spdif_config.rate;
                ret = aml_audio_spdifout_open(&bitstream_out->spdifout_handle, &spdif_config);
                if (ret != 0) {
                    ALOGE("%s open spdif out failed\n", __func__);
                    pthread_mutex_unlock(&adev->bitstream_lock);
                    return ret;
                }
                bitstream_out->is_bypass_ms12 = ms12_dec->is_bypass_ms12;
            }
        }

        bitstream_out->audio_format = output_format;
        /*
         * control the mute flag to mute/unmute the spdif out.
         */
        if (bitstream_out->spdifout_handle) {
            if (ms12->main_volume < FLOAT_ZERO) {
                aml_audio_spdifout_mute(bitstream_out->spdifout_handle, 1);
            } else {
                aml_audio_spdifout_mute(bitstream_out->spdifout_handle, 0);
            }
        }
        pthread_mutex_unlock(&adev->bitstream_lock);
        /* send these IEC61937 data to alsa */
        aml_audio_spdifout_process(bitstream_out->spdifout_handle, buffer, bytes);
    }
    return 0;
}


int dolby_ms12_bypass_process(struct audio_stream_out *stream, void *buffer, size_t bytes) {
    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
    audio_format_t output_format =  ms12_get_audio_hal_format(aml_out->hal_format);
    ALOGV("output_format=0x%x hal_format=0x%#x internal=0x%x", output_format, aml_out->hal_format, aml_out->hal_internal_format);
    bool is_ac3_eac3 = (aml_out->hal_internal_format == AUDIO_FORMAT_E_AC3) || (aml_out->hal_internal_format == AUDIO_FORMAT_AC3);
    bool is_dolby_truehd = (aml_out->hal_internal_format == AUDIO_FORMAT_DOLBY_TRUEHD);
    bool is_mat = (aml_out->hal_internal_format == AUDIO_FORMAT_MAT);

    if (ms12_dec == NULL || !ms12_dec->is_focus) {
        return bytes;
    }

    pthread_mutex_lock(&ms12->bypass_lock);

    if (is_ac3_eac3) {
        //SPDIF Encoder works well with DDP frame 0x0B77 but not 0x770B.
        const uint8_t *data = (const uint8_t *)buffer;
        if ((data[0] == 0x77) && (data[1] == 0x0B)) {
            endian16_convert(buffer, bytes);
        }
        ret = ac3_and_eac3_bypass_process(stream, buffer, bytes);
    }
    else if (is_dolby_truehd) {
        ret = dolby_truehd_bypass_process(stream, buffer, bytes);
    }
    else if (is_mat) {
        ret = mat_bypass_process(stream, buffer, bytes);
    }
    else {
        ALOGV("%s unsupport format %#x!\n", __func__, aml_out->hal_format);
        ret = -1;
    }
    pthread_mutex_unlock(&ms12->bypass_lock);
    return ret;

}


int master_pcm_type(struct aml_stream_out *aml_out) {
    bool dap_enable = is_dolbyms12_dap_enable(aml_out);
    if (dap_enable) {
        return DAP_LPCM;
    }
    return NORMAL_LPCM;
}

/*this function for continuous mode passthrough*/
int ms12_passthrough_output(struct aml_stream_out *aml_out) {
    int ret = 0;
    int i = 0;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    audio_format_t output_format = AUDIO_FORMAT_PCM_16_BIT;
    void *output_buf = NULL;
    int32_t out_size = 0;
    struct bypass_frame_info frame_info = { 0 };
    int  passthrough_delay_ms = 0;
    audio_format_t hal_internal_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);
    uint64_t ms12_dec_out_nframes = dolby_ms12_get_continuous_nframes_pcm_output(adev->ms12.dolby_ms12_ptr, MAIN_INPUT_STREAM);
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
#if 0
    struct aml_stream_out *focus_stream_out = adev->focus_ms12_stream;

    if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS && ms12_dec_out_nframes != 0 &&
        (hal_internal_format == AUDIO_FORMAT_E_AC3 || hal_internal_format == AUDIO_FORMAT_AC3)) {
        uint64_t consume_offset = aml_out->ms12_dec_handle->ms12_main_consume_bytes;
        aml_ms12_bypass_checkout_data(aml_out->ms12_dec_handle->ms12_bypass_handle, &output_buf, &out_size, consume_offset, &frame_info);
    }


    if (focus_stream_out && (adev->digital_audio_mode != AML_DIGITAL_AUDIO_MODE_BYPASS)) {
        focus_stream_out->ms12_dec_handle->is_bypass_ms12 = false;
    }
    if (focus_stream_out && focus_stream_out->ms12_dec_handle->is_bypass_ms12 != bitstream_out->is_bypass_ms12 &&
        bitstream_out->spdifout_handle != NULL) {
        ALOGI("change to bypass mode from =%d to %d", bitstream_out->is_bypass_ms12, focus_stream_out->ms12_dec_handle->is_bypass_ms12);
        ms12_close_all_spdifout(ms12);
    }


    if (focus_stream_out->ms12_dec_handle->is_bypass_ms12) {
        ALOGV("bypass ms12 size=%d", out_size);
        output_format = hal_internal_format;
        /*nts have one test case, when passthrough and pause, we should close spdif output*/
        if (focus_stream_out && focus_stream_out->ms12_dec_handle->is_paused) {
            ms12_close_all_spdifout(ms12);
            out_size = 0;
        }

        if (out_size != 0 && output_buf != NULL) {
            struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
            ret = aml_ms12_spdif_output_new(stream_out, bitstream_out, output_format, aml_out->hal_internal_format, aml_out->hal_rate, 2, AUDIO_CHANNEL_OUT_STEREO, output_buf, out_size);
        }
        passthrough_delay_ms = aml_audio_spdifout_get_delay(bitstream_out->spdifout_handle);
        ALOGV("passthrough_delay_ms =%d", passthrough_delay_ms);
    }
#endif
    return ret;
}

/*this is the master output, it will do position calculate and av sync*/
static int ms12_output_master(void *buffer, void *priv_data, size_t size, audio_format_t output_format,aml_ms12_dec_info_t *ms12_info) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    audio_data_info_t data_info = { 0 };
    bool netflix_llp_mode = (adev->is_netflix && adev->aaudio_low_latency);

    int ret = 0;
    int i;

    /*we update the optical format in pcm, because it is always output*/
    /*In netflix llp aaudio, always output pcm. Close spdifout will affect stereo pcm output*/
    if (ms12->optical_format != adev->optical_format ||
        (ms12->b_encoder_reset && !netflix_llp_mode)) {
        if (ms12->optical_format != adev->optical_format) {
            ALOGI("ms12 optical format change from 0x%x to  0x%x\n",adev->ms12.optical_format,adev->optical_format);
        } else {
            ALOGI("%s", __func__);
        }
        ms12->optical_format= adev->optical_format;
        ms12_close_all_spdifout(ms12);
        ms12->b_encoder_reset = false;

    }

    if (adev->reset_hdmitx_audio) {
        adev->reset_hdmitx_audio = false;
        ms12_reset_all_spdifout(ms12);
        ALOGI("%s reset hdmitx", __func__);
    }

    //TODO support 24/32 bit sample  */
    ALOGV("dap pcm =%" PRId64 " stereo pcm =%" PRId64 " master =%" PRId64 "", ms12->dap_pcm_frames, ms12->stereo_pcm_frames, ms12->master_pcm_frames);

    data_info.audio_format = output_format;
    data_info.channel_mask = audio_channel_out_mask_from_count(ms12_info->output_ch);
#if 0
    //ms12 master output, alsa bitdepth by ms12 output bitdepth.
    if (data_info.audio_format == AUDIO_FORMAT_PCM_16_BIT) {
        adev->ms12_config.format = PCM_FORMAT_S16_LE;
    } else if (data_info.audio_format == AUDIO_FORMAT_PCM_32_BIT) {
        adev->ms12_config.format = PCM_FORMAT_S32_LE;
    }
#endif

    ret = aml_audio_pcm_output((struct audio_stream_out *)aml_out, buffer, size, &data_info);

    return ret;

}

#define ALSA_MAX_US 8//us
int dap_pcm_output(void *buffer, void *priv_data, size_t size,aml_ms12_dec_info_t *ms12_info)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = ms12_info->data_type;
    int ret = 0;
    int i;

    if (adev->debug_flag > 1) {
        ALOGI("+%s() size %zu,ch %d", __FUNCTION__, size,ms12_info->output_ch);
    }
    if (ms12_info->output_ch != 0)
        ms12->dap_pcm_frames += size / (audio_bytes_per_sample(output_format) * ms12_info->output_ch);
    /*dump ms12 pcm output*/
    if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_SPEAKER_PCM)) {
        dump_ms12_output_data(buffer, size, MS12_OUTPUT_SPEAKER_PCM_FILE);
    }

    // Soudbar device, 1 DAP Effect is ON. 2. adev set param kvpairs="hal_param_soundbar_mode=0"
    if (adev->effect_ctrl.dap_enable && is_SBR(adev)) {
        if (adev->is_alsa_device_conflict) {
            ssize_t alsa_ret = aml_audio_close_pcm_output((struct audio_stream_out *)aml_out);
            adev->is_alsa_device_conflict = false;
        }
    }

    if (is_dolbyms12_dap_enable(aml_out) || ms12->dap_only_enable) {
#ifdef SUPPORT_KARAOKE
        /*do mix usb mic karaoke*/
        struct kara_manager *kara = &adev->usb_audio.karaoke;
        if (karaoke_get_on(kara) && !karaoke_get_start(kara)) {
            karaoke_get_audioCfg_from_ms12_info(&kara->mixout_config, ms12_info);
        }
        karaoke_check_mix_output(kara, buffer, size);
        /*do mix linein mic karaoke*/
        struct kara_manager *linein_kara = &adev->linein_karaoke;
        if (karaoke_get_on(linein_kara) && !karaoke_get_start(linein_kara)) {
            karaoke_get_audioCfg_from_ms12_info(&linein_kara->mixout_config, ms12_info);
            /*fix usb mic noise issue when linein mic work after usb*/
            if (karaoke_get_start(kara))
                karaoke_close(kara);
        }
        karaoke_check_mix_output(linein_kara, buffer, size);
#endif
        aml_audio_trace_int("aml_dap_output", size);
        ms12_output_master(buffer, priv_data, size, output_format,ms12_info);
        aml_audio_trace_int("aml_dap_output", 0);
    } else {
        return ret;
    }
    if (adev->debug_flag > 1) {
        ALOGI("-%s() ret %d", __FUNCTION__, ret);
    }

    return ret;
}

int stereo_pcm_output(void *buffer, void *priv_data, size_t size, aml_ms12_dec_info_t *ms12_info)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    void *output_buffer = buffer;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = ms12_info->data_type;
    struct aml_stream_out *focus_stream_out = adev->focus_ms12_stream;
    int ret = 0;
    int i;

    if (adev->debug_flag > 1) {
        ALOGI("+%s() size %zu", __FUNCTION__, size);
    }
    if (ms12_info->output_ch != 0)
        ms12->stereo_pcm_frames += size / (audio_bytes_per_sample(output_format) * ms12_info->output_ch);
    /*dump ms12 pcm output*/
    if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_SPDIF_PCM)) {
        dump_ms12_output_data(buffer, size, MS12_OUTPUT_SPDIF_PCM_FILE);
    }

    if (get_debug_value(AML_DEBUG_AUDIOHAL_DETECT_ZERO_DATA)) {
        if (ms12_info->data_type == AUDIO_FORMAT_PCM_32_BIT) {
            aml_check_buffer_zero_data("stereo_pcm", buffer, size, 2, AUDIO_FORMAT_PCM_32_BIT);
        } else {
            aml_check_buffer_zero_data("stereo_pcm", buffer, size, 2, AUDIO_FORMAT_PCM_16_BIT);
        }
    }

    // Soudbar device, 1 DAP Effect is OFF. 2. adev set param kvpairs="hal_param_soundbar_mode=1"
    if (!adev->effect_ctrl.dap_enable  && is_SBR(adev)) {
        if (adev->is_alsa_device_conflict) {
            ssize_t alsa_ret = aml_audio_close_pcm_output((struct audio_stream_out *)aml_out);
            adev->is_alsa_device_conflict = false;
        }
    }

    /*it has dap output, then this will be used for spdif output*/
    if (is_dolbyms12_dap_enable(aml_out)) {
        if (get_buffer_write_space (&ms12->spdif_ring_buffer) >= (int) size) {
            ring_buffer_write(&ms12->spdif_ring_buffer, buffer, size, UNCOVER_WRITE);
        }

    } else {
#ifdef SUPPORT_KARAOKE
        /*do mix usb mic karaoke*/
        struct kara_manager *kara = &adev->usb_audio.karaoke;
        if (karaoke_get_on(kara) && !karaoke_get_start(kara)) {
            //set main config for mixing
            karaoke_get_audioCfg_from_ms12_info(&kara->mixout_config, ms12_info);
        }
        karaoke_check_mix_output(kara, buffer, size);

        /*do mix linein mic karaoke*/
        struct kara_manager *linein_kara = &adev->linein_karaoke;
        if (karaoke_get_on(linein_kara) && !karaoke_get_start(linein_kara)) {
            //set main config for mixing
            karaoke_get_audioCfg_from_ms12_info(&linein_kara->mixout_config, ms12_info);
            /*fix usb mic noise issue when linein mic work after usb*/
            if (karaoke_get_start(kara))
                karaoke_close(kara);
        }
        karaoke_check_mix_output(linein_kara, buffer, size);
#endif
        //when Dolby MS12 use not 1.0 volume "-sys_prim_mixgain <3 int>
        //the PCM Render can not output at a same volume for both DDP and AC4.
        //AC4 should use the 1.0 volume and control the volume through the PCM output.
        //In the STB, PCM output will be always without DAP device processing.
        //will not call the dap_pcm_output().
        if  (focus_stream_out && (adev->ms12.focus_audioformat == AUDIO_FORMAT_AC4)) {
            if (is_AC4_stream_with_pcm_sink_on_stb(focus_stream_out)) {
                apply_volume(get_ac4_stream_volume(focus_stream_out), buffer, audio_bytes_per_sample(output_format), size);
            }
        }
        aml_audio_trace_int("stereo_output", size);
        ms12_output_master(buffer, priv_data, size, output_format, ms12_info);
        aml_audio_trace_int("stereo_output", 0);
    }

    if (adev->debug_flag > 1) {
        ALOGI("-%s() ret %d", __FUNCTION__, ret);
    }

    return ret;
}


int bitstream_output(void *buffer, void *priv_data, size_t size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = AUDIO_FORMAT_AC3;
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    int ret = 0;
    int bitstream_delay_ms = 0;
    int out_size = 0;
    ms12->bitstream_cnt++;

    if (adev->debug_flag > 1) {
        ALOGI("+%s() size %zu,dual_output = %d, optical_format = 0x%0x, sink_format = 0x%x out total=%d main in=%d",
            __FUNCTION__, size, aml_out->dual_output_flag, adev->optical_format, adev->sink_format, ms12->bitstream_cnt, ms12->input_total_ms);
    }

    /*
     * when eac3 should bypass ms12 and output the eac3, ignore the MS12 eac3 output.
     */
    if (adev->ms12.focus_is_bypass_ms12) {
        return 0;
    }

    if (adev->optical_format == AUDIO_FORMAT_PCM_16_BIT) {
        return 0;
    }

    /*
     * Old version:(ms12->optical_format != AUDIO_FORMAT_E_AC3)
     *
     * reason:
     *      1. AVR(only MAT1.0 - TrueHD, not MAT2.0/MAT2.1)
     *      2. TrueHD can passthrough the MS12 with MAT encoder.
     *      3. MS12 pipeline add the DDP Encoder, so will output DDP.
     *
     * effect:
     *      AVR(up to MAT1.0) + MS12
     *      A. AUTO Mode:
     *         all dolby input format should output DDP
     *      B. NONE Mode:
     *         all dolby input format should output PCM
     *      C. Passthrough:
     *         AC3/EAC3/MLP can passthrough, others should under ms12 processing.
     */
    if (ms12->optical_format < AUDIO_FORMAT_E_AC3) {
        return 0;
    }

    if (is_SBR_active(adev)) {
        return 0;
    }

    /*dump ms12 bitstream output*/
    if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_BITSTREAM)) {
        dump_ms12_output_data(buffer, size, MS12_OUTPUT_BITSTREAM_FILE);
    }

    output_format = AUDIO_FORMAT_E_AC3;

    ms12_spdif_encoder(buffer, size, output_format, ms12->iec61937_ddp_buf, &out_size);

    aml_audio_trace_int("bitstream_output", out_size);
    ret = aml_ms12_spdif_output_new(stream_out, bitstream_out, AUDIO_FORMAT_IEC61937, AUDIO_FORMAT_E_AC3, DDP_OUTPUT_SAMPLE_RATE, 2, AUDIO_CHANNEL_OUT_STEREO, ms12->iec61937_ddp_buf, out_size);
    aml_audio_trace_int("bitstream_output", 0);

    bitstream_delay_ms = aml_audio_spdifout_get_delay(bitstream_out->spdifout_handle);
    ALOGV("%s delay=%d", __func__, bitstream_delay_ms);

    if (adev->debug_flag > 1) {
        ALOGI("-%s() ret %d", __FUNCTION__, ret);
    }

    return ret;
}

int spdif_bitstream_output(void *buffer, void *priv_data, size_t size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int   bitstream_id = BITSTREAM_OUTPUT_A;
    struct bitstream_out_desc *bitstream_out = NULL;
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = AUDIO_FORMAT_AC3;
    int ret = 0;

    if (adev->debug_flag > 1) {
        ALOGI("+%s() size %zu,dual_output = %d, optical_format = 0x%x, sink_format = 0x%x out total=%d main in=%d",
            __FUNCTION__, size, aml_out->dual_output_flag, adev->optical_format, adev->sink_format, ms12->bitstream_cnt, ms12->input_total_ms);
    }

    /*if it is in bypass mode, spdif output info need update after dolby_ms12_main_open*/
    if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS &&
        (ms12->focus_audioformat == AUDIO_FORMAT_INVALID ||
         ms12->focus_audioformat == AUDIO_FORMAT_AC3)) {
        return 0;
    }
    /*
     * when ac3 should bypass ms12 and output the AC3, ignore the MS12 ac3 output.
     */
    if (ms12->focus_is_bypass_ms12) {
        /*non dual bitstream case, we only have one spdif*/
        if (!ms12->dual_bitstream_support) {
            return 0;
        }
        /*input is ac3, we can bypass it*/
        if (ms12->focus_audioformat == AUDIO_FORMAT_AC3) {
            return 0;
        }
        /*when main stream is paused, we also doesn't need output spdif*/
        if (ms12->focus_is_paused) {
            return 0;
        }
    }

    if (is_SBR_active(adev)) {
        return 0;
    }

    if (ms12->dual_bitstream_support) {
        bitstream_id = BITSTREAM_OUTPUT_B;
    }

    bitstream_out = &ms12->bitstream_out[bitstream_id];

    if (adev->optical_format == AUDIO_FORMAT_PCM_16_BIT) {
        return 0;
    }

    if (ms12->optical_format != AUDIO_FORMAT_AC3 && !ms12->dual_bitstream_support) {
        return 0;
    }

    /*dump ms12 bitstream output*/
    if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_BITSTREAM2)) {
        dump_ms12_output_data(buffer, size, MS12_OUTPUT_BITSTREAM2_FILE);
    }

    aml_audio_trace_int("spdif_bitstream_output", size);
    ret = aml_ms12_spdif_output_new(stream_out, bitstream_out, output_format, output_format, DDP_OUTPUT_SAMPLE_RATE, 2, AUDIO_CHANNEL_OUT_STEREO, buffer, size);
    aml_audio_trace_int("spdif_bitstream_output", 0);

    return ret;
}

int mat_bitstream_output(void *buffer, void *priv_data, size_t size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int   bitstream_id = BITSTREAM_OUTPUT_A;
    struct bitstream_out_desc *bitstream_out = NULL;
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = AUDIO_FORMAT_MAT;
    bool is_earc = is_earc_connected(adev);
    int ret = 0;
    int bitstream_delay_ms = 0;

    if (adev->debug_flag > 1) {
        ALOGI("+%s() size %zu,dual_output = %d, optical_format = 0x%x, sink_format = 0x%x out total=%d main in=%d",
            __FUNCTION__, size, aml_out->dual_output_flag, adev->optical_format, adev->sink_format, ms12->bitstream_cnt, ms12->input_total_ms);
    }

    /*
     * when truehd should bypass ms12 and output the MAT, ignore the MS12 MAT output.
     */
    if (ms12->focus_is_bypass_ms12) {
        return 0;
    }

    if (is_SBR_active(adev)) {//runtime param is a little later than MS12 initialization
        return 0;
    }

    bitstream_out = &ms12->bitstream_out[bitstream_id];

    if (adev->optical_format == AUDIO_FORMAT_PCM_16_BIT || adev->optical_format == AUDIO_FORMAT_AC3) {
        return 0;
    }
#if 0
    /* amazon special audio strategy requirements, trunk doesn't need it*/
    if (is_earc && (aml_out->hal_ch >= 6 && aml_out->hal_internal_format == AUDIO_FORMAT_PCM_SUB_16_BIT)) {
        //for pcm multi channel when connected earc,
        //not use mat output and the data send to alsa/earc by mc_pcm_output, Hazel FIXME.
        return 0;
    }
#endif

    /*dump ms12 bitstream output*/
    if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_BITSTREAM_MAT)) {
        dump_ms12_output_data(buffer, size, MS12_OUTPUT_BITSTREAM_MAT_FILE);
    }

    aml_audio_trace_int("aml_mat_bitstream_output", size);
    ret = aml_ms12_spdif_output_new(stream_out, bitstream_out, output_format, output_format, DDP_OUTPUT_SAMPLE_RATE, 8, AUDIO_CHANNEL_OUT_7POINT1, buffer, size);
    aml_audio_trace_int("aml_mat_bitstream_output", 0);


    bitstream_delay_ms = aml_audio_spdifout_get_delay(bitstream_out->spdifout_handle);
    ALOGV("%s delay=%d", __func__, bitstream_delay_ms);

    return ret;
}

/*
 *@brief convert the dolby acmod to android channel mask
 */
static int acmod_convert_to_channel_mask(AML_DOLBY_ACMOD acmod, int lfeon) {
    int ch_mask = AUDIO_CHANNEL_OUT_STEREO;

    switch (acmod) {
        case AML_DOLBY_ACMOD_ONEPLUSONE: {
            ch_mask = AUDIO_CHANNEL_OUT_STEREO;
            break;
        }
        case AML_DOLBY_ACMOD_MONO: {
            ch_mask = AUDIO_CHANNEL_OUT_MONO;
            break;
        }
        case AML_DOLBY_ACMOD_STEREO: {
            if (lfeon) {
                ch_mask = AUDIO_CHANNEL_OUT_2POINT1;
            } else {
                ch_mask = AUDIO_CHANNEL_OUT_STEREO;
            }
            break;
        }
        /*this acmod can't be mapped*/
        case AML_DOLBY_ACMOD_3_0:
        case AML_DOLBY_ACMOD_2_1: {
            if (lfeon) {
                ch_mask = AUDIO_CHANNEL_OUT_3POINT1;
            } else {
                ch_mask = AUDIO_CHANNEL_OUT_TRI;
            }
            break;
        }
        /*this acmod can't be mapped*/
        case AML_DOLBY_ACMOD_3_1:
        case AML_DOLBY_ACMOD_2_2: {
            ch_mask = AUDIO_CHANNEL_OUT_QUAD;
            break;
        }
        case AML_DOLBY_ACMOD_3_2: {
            if (lfeon) {
                ch_mask = AUDIO_CHANNEL_OUT_5POINT1;
            } else {
                ch_mask = AUDIO_CHANNEL_OUT_PENTA;
            }
            break;
        }
        case AML_DOLBY_ACMOD_3_4: {
            if (lfeon) {
                ch_mask = AUDIO_CHANNEL_OUT_7POINT1;
            } else {
                ch_mask = AUDIO_CHANNEL_OUT_6POINT1;
            }
            break;
        }
        case AML_DOLBY_ACMOD_3_2_2: {
            ch_mask = AUDIO_CHANNEL_OUT_5POINT1POINT2;
            break;
        }

        default:
            break;

    }

    return ch_mask;
}

int mc_pcm_output(void *buffer, void *priv_data, size_t size, aml_ms12_dec_info_t *ms12_info)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int   bitstream_id = BITSTREAM_OUTPUT_C;
    struct bitstream_out_desc *bitstream_out = NULL;
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;
    audio_format_t output_format = ms12_info->data_type;
    int ret = 0;
    int mc_delay_ms = 0;
    int ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    int data_ch = 2;
    bool is_earc = is_earc_connected(adev);
    bool netflix_llp_mode = (adev->is_netflix && adev->aaudio_low_latency);

    if (adev->debug_flag > 1) {
        ALOGI("+%s() size %zu,dual_output = %d, optical_format = 0x%x, sink_format = 0x%x out total=%d main in=%d",
            __FUNCTION__, size, aml_out->dual_output_flag, adev->optical_format, adev->sink_format, ms12->bitstream_cnt, ms12->input_total_ms);
    }

    ALOGV("mc acmod =%d lfeon =%d, ch:%d", ms12_info->acmod, ms12_info->lfeon, ms12_info->output_ch);

    data_ch = ms12_info->output_ch;
    ch_mask = acmod_convert_to_channel_mask(ms12_info->acmod, ms12_info->lfeon);

    bitstream_out = &ms12->bitstream_out[bitstream_id];

    // mc_pcm_output conflict with dolby sdk certification(request stereo pcm output)
    if (!adev->is_netflix && !is_earc) {
        if (bitstream_out->spdifout_handle) {
            ALOGI("%s close mc spdif handle =%p", __func__, bitstream_out->spdifout_handle);
            aml_audio_spdifout_close(bitstream_out->spdifout_handle);
            bitstream_out->spdifout_handle = NULL;
        }
        if (adev->debug_flag > 1) {
            ALOGI("%s : isn't netflix, drop data\n", __FUNCTION__);
        }
        return 0;
    }

    if (adev->is_netflix && (adev->sink_max_channels >= 6)) {
        // when enter netflix llp aaudio mode, output mc pcm
        // mc pcm always has 8ch data, it is ok to fix ch_mask to 5.1
        if (adev->aaudio_low_latency || (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_PCM)) {
            if (adev->debug_flag > 1) {
                AM_LOGV("ch_mask 0x%x fix to 5.1", ch_mask);
            }
            ch_mask = AUDIO_CHANNEL_OUT_5POINT1;
        }
    }

    if ((adev->optical_format != AUDIO_FORMAT_PCM_16_BIT) || (adev->sink_max_channels < 6) || ms12->focus_is_bypass_ms12
        || (ch_mask == AUDIO_CHANNEL_OUT_STEREO)) {
        if (bitstream_out->spdifout_handle) {
            ALOGI("%s close mc spdif handle =%p", __func__, bitstream_out->spdifout_handle);
            aml_audio_spdifout_close(bitstream_out->spdifout_handle);
            bitstream_out->spdifout_handle = NULL;
        }
        return 0;
    }

    if (netflix_llp_mode && ch_mask == AUDIO_CHANNEL_OUT_STEREO) {
        /*
         * The first coming ch_mask is 2.0, then 5.1
         * ch_mask will be used to "eMixerEARC_Channel_Allocation" when earc(spdif) open.
        */
        AM_LOGV("ch_mask change 0x%x to 5.1", ch_mask);
        ch_mask = AUDIO_CHANNEL_OUT_5POINT1;
    }

    /*dump ms12 mc output*/
    if (get_ms12_dump_enable(DUMP_MS12_OUTPUT_MC_PCM)) {
        dump_ms12_output_data(buffer, size, MS12_OUTPUT_MC_PCM_FILE);
    }

    ret = aml_ms12_spdif_output_new(stream_out, bitstream_out, output_format, output_format, DDP_OUTPUT_SAMPLE_RATE, data_ch, ch_mask, buffer, size);

    mc_delay_ms = aml_audio_spdifout_get_delay(bitstream_out->spdifout_handle);
    ALOGV("%s delay=%d", __func__, mc_delay_ms);

    return ret;
}


static int ms12_debug_out_stereo_pcm_synced_frame_pts
    (struct dolby_ms12_desc *ms12
    , const void *buf
    , size_t n_bytes_buf
    , int decoder_latency
    , int64_t out_frame_pts
    , aml_ms12_dec_info_t *ms12_info)
{
    int ret = -1;
    if (ms12 && buf && (n_bytes_buf > 0) && ms12_info) {
        int pre_zero_samples = 0;
        int n_sample_in_ddp_frame = 1536;
        bool is_beep_frame = check_beep_frame(buf, n_bytes_buf, &pre_zero_samples);
        if (is_beep_frame) {
            int timems = (out_frame_pts - ms12->first_in_frame_pts - decoder_latency) / MILLISECOND_2_PTS;
            bool is_pts_available = (ms12->last_synced_frame_pts != -1);
            bool is_pts_in_one_beep = ((out_frame_pts - ms12->last_synced_frame_pts) <= MILLISECOND_2_PTS * DOLBY_MS12_AVSYNC_BEEP_DURATION);
            if (is_pts_available && is_pts_in_one_beep) {
                ALOGV("same beep frame!");
                ret = -1;
            } else {
               ms12->out_synced_frame_count++;
               int actual_synced_frame_ms = timems + pre_zero_samples / (ms12_info->output_ch * (ms12_info->output_sr / 1000));
               ALOGI("count %" PRIu64 " out_frame_pts %" PRId64 " ms decoder out synced frame at %d ms pre_zero_samples %d actual_synced_frame %d ms master_pcm_frames %" PRIu64 "",
                   ms12->out_synced_frame_count, out_frame_pts / 90, timems, pre_zero_samples, actual_synced_frame_ms, ms12->master_pcm_frames);
               ms12->last_synced_frame_pts = out_frame_pts;
               ret = 0;
            }
        }
    }

    return ret;
}

static Aml_MS12_SyncPolicy_t update_ms12_position(void *priv_data, unsigned long long u64DecOutFrame, Aml_MS12_Delay_t stDelay) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc  *main_dec_handle = aml_out->ms12_dec_handle;
    Aml_MS12_SyncPolicy_t audio_sync_policy = {MS12_SYNC_AUDIO_NORMAL_OUTPUT, 0, 0};
    struct timespec ts;
    uint64_t current_frames_positions = 0;
    int64_t  frame_diff_us =  0;
    int64_t  system_time_us = 0;
    int64_t  jitter_diff_us = 0;
    uint64_t current_time_us = 0;
    uint64_t time_diff_ms = 0;
    /*Dolby MAT is 20ms for one frame*/
    uint32_t jitter_threshold_us = 20 * MICRO_SECOND_PER_MILLISECOND;
    uint64_t decoded_frame = 0;
    int ms12_delay_frame = stDelay.u32DelayFrame;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;
    int tempo_delay_frame = speed_info->last_latency_frame;
    int total_delay_frame = 0;
    float speed = speed_info->speed;

    /*currently for truhe bypass, we drop the pcm data, otherwise it will causes conflict issue*/
    if (main_dec_handle->is_bypass_ms12 && (aml_out->hal_internal_format == AUDIO_FORMAT_DOLBY_TRUEHD)) {
        /*MAT one frame is 20ms*/
        audio_sync_policy.s32TagFrame = 960;
        audio_sync_policy.s32CurFrame = 0;
        audio_sync_policy.eSyncPolicy = MS12_SYNC_AUDIO_DROP_PCM;
        if (adev->debug_flag) {
            ALOGI("%s Drop PCM data", __func__);
        }
    }
    current_time_us = aml_audio_get_systime();
    time_diff_ms = (current_time_us - stDelay.u64DelayTimeStamp) / MICRO_SECOND_PER_MILLISECOND;

    if (speed_info->speed_handle) {
        aml_audio_speed_post_delay_t *p_post_delay = &speed_info->post_delay;

        ms12_delay_frame = aml_audio_speed_calculate_post_delay(p_post_delay, ms12_delay_frame);
        if (p_post_delay->transitioning || !is_float_equal(p_post_delay->next_speed, speed_info->speed)) {
            speed = p_post_delay->last_speed;
        } else if (speed_info->hwsync_force_update) {
            aml_stream_clear_speed_aux_info(aml_out);
            if (!(aml_out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {
                speed_info->hwsync_force_update = false;
            }
        }
    }

    total_delay_frame = ms12_delay_frame + tempo_delay_frame;
    if (adev->debug_flag) {
        ALOGI("%s dec frame =%" PRId64 " out frame =%lld tempo_delay =%d ms12 delay=%d(origin %d) total delay =%d  =%d ms time_diff =%"PRIu64"",
            __func__, decoded_frame, u64DecOutFrame, tempo_delay_frame, ms12_delay_frame, stDelay.u32DelayFrame, total_delay_frame, total_delay_frame/48, time_diff_ms);
    }

    clock_gettime(CLOCK_MONOTONIC, &ts);

    /*according to the delay and decoded frame to calculate frame position*/
    if (u64DecOutFrame > total_delay_frame) {
        current_frames_positions = u64DecOutFrame - total_delay_frame;
        main_dec_handle->ms12_position_update = true;
    } else {
        current_frames_positions = 0;
        main_dec_handle->ms12_position_update = false;
    }

    /*check whether there is any jitter between position and timestamp*/
    if (speed_info->speed_handle) {
        struct timespec start_ts = {0};
        uint64_t start_position = 0;
        if (aml_audio_speed_get_start_ts(&speed_info->start_ts, &start_ts, &start_position)) {
            frame_diff_us =  ((int64_t)current_frames_positions - (int64_t)start_position) * MICRO_SECOND_PER_MILLISECOND / 48;
            system_time_us = calc_time_interval_us(&start_ts, &ts);
            if (adev->debug_flag) {
                AM_LOGI("frame_diff_us %" PRId64 ", current_frames_positions %" PRId64 ", start_position %" PRId64 "",
                    frame_diff_us, current_frames_positions, start_position);
            }
        } else if (main_dec_handle->last_frames_position > 256) {
            frame_diff_us =  ((int64_t)current_frames_positions - (int64_t)main_dec_handle->last_frames_position) * MICRO_SECOND_PER_MILLISECOND / 48;
            system_time_us = calc_time_interval_us(&main_dec_handle->timestamp, &ts);
        }

        if (!is_float_equal(speed, 1.0f)) {
            system_time_us *= speed;
        }

        // sonic cache frames jitter large, need to expand
        jitter_threshold_us = 42 * MICRO_SECOND_PER_MILLISECOND;

        // If speed is too high, sometimes decoder thread can not generate enough data.
        // Give more time to comeback.
        if (fabs(speed - 1.99f) > 1e-06) {
            jitter_threshold_us *= 2.0;
        } else if (fabs(speed - 1.49f) > 1e-06) {
            jitter_threshold_us *= 1.5;
        }
        jitter_diff_us = frame_diff_us - system_time_us;
        if (speed_info->last_out_frame_diff_us != frame_diff_us) {
            aml_audio_speed_add_apts_gap(&speed_info->sync_apts_gap, jitter_diff_us/1000);
        }
        speed_info->last_out_frame_diff_us = frame_diff_us;
    } else if (main_dec_handle->last_frames_position > 0) {
        frame_diff_us =  ((int64_t)current_frames_positions - (int64_t)main_dec_handle->last_frames_position) * MICRO_SECOND_PER_MILLISECOND / 48;
        system_time_us = calc_time_interval_us(&main_dec_handle->timestamp, &ts);
        jitter_diff_us = frame_diff_us - system_time_us;
    }

    // sometimes ms12 starting position is not stable
    if (current_frames_positions <= 256 && adev->is_netflix) {
        main_dec_handle->ms12_position_update = false;
    }

    if  (adev->debug_flag) {
        ALOGI("%s ms12 jitter out cur pos: %"PRIu64", last pos info: %"PRIu64", sec = %ld, nanosec = %ld\n",__func__, current_frames_positions, main_dec_handle->last_frames_position,
            main_dec_handle->timestamp.tv_sec, main_dec_handle->timestamp.tv_nsec);
        ALOGI("%s jitter  system time diff %lld ms, position diff %lld ms, jitter %lld ms \n",
            __func__,system_time_us / MICRO_SECOND_PER_MILLISECOND,frame_diff_us / MICRO_SECOND_PER_MILLISECOND,jitter_diff_us / MICRO_SECOND_PER_MILLISECOND);
    }

    /*if bypass mode, increase the jitter threshold, because we drop the pcm data and pos is not accurate*/
    if (main_dec_handle->is_bypass_ms12) {
        jitter_threshold_us = 50 * MICRO_SECOND_PER_MILLISECOND;
    }
    if (llabs(jitter_diff_us) > jitter_threshold_us) {

        ALOGI("%s ms12 jitter out cur pos: %"PRIu64", last pos info: %"PRIu64", sec = %ld, nanosec = %ld\n",__func__, current_frames_positions, main_dec_handle->last_frames_position,
                       main_dec_handle->timestamp.tv_sec, main_dec_handle->timestamp.tv_nsec);
        ALOGI("%s jitter  system time diff %lld ms, position diff %lld ms, jitter %lld ms \n",
                       __func__,system_time_us / MICRO_SECOND_PER_MILLISECOND,frame_diff_us / MICRO_SECOND_PER_MILLISECOND,jitter_diff_us / MICRO_SECOND_PER_MILLISECOND);
    }
    /*currently the position is not accurate, we need compensate it*/
    if (llabs(jitter_diff_us) <= jitter_threshold_us && current_frames_positions > CONVERT_US_TO_48K_FRAME_NUM(llabs(jitter_diff_us))) {
        if (jitter_diff_us > 0) {
            current_frames_positions -= CONVERT_US_TO_48K_FRAME_NUM(jitter_diff_us);
        } else {
            current_frames_positions += CONVERT_US_TO_48K_FRAME_NUM(llabs(jitter_diff_us));
        }
    } else {
        // If jitter_diff_us too large, something unusual happened, should reset speed_start_ts
        if (current_frames_positions >= 2048) {
            aml_stream_clear_speed_aux_info(aml_out);
        }
    }

    if (main_dec_handle->ms12_position_update) {
        uint64_t max_report_frames = u64DecOutFrame;
        uint64_t last_frames_position = main_dec_handle->last_frames_position;

        // protect for specials cases : decoder thread xrun, position fallback
        if (max_report_frames > tempo_delay_frame) {
            max_report_frames -= tempo_delay_frame;
        }
        if (current_frames_positions < last_frames_position) {
            ALOGE("%s frames_position anti rollback(%" PRId64 " -> %" PRId64 ")", __func__, current_frames_positions, last_frames_position);
            current_frames_positions = last_frames_position;
        }
        if (current_frames_positions > max_report_frames) {
            ALOGE("%s frames_position too large !(%" PRId64 " truncate to %" PRId64 ")", __func__, current_frames_positions, max_report_frames);
            current_frames_positions = max_report_frames;
        }
        aml_audio_speed_update_start_ts(&speed_info->start_ts, &ts, current_frames_positions);
        main_dec_handle->last_post_buffer_frame = stDelay.u32DelayFrame;
    }

    //pthread_mutex_lock(&adev->ms12.main_apts_update_lock);
    main_dec_handle->last_frames_position = current_frames_positions;
    main_dec_handle->timestamp.tv_sec = ts.tv_sec;
    main_dec_handle->timestamp.tv_nsec = ts.tv_nsec;
    //pthread_mutex_unlock(&adev->ms12.main_apts_update_lock);
    if (!(aml_out->hwsync && aml_out->hwsync->use_mediasync)) {
        aml_out->last_dec_out_frame = u64DecOutFrame;
    }

    return audio_sync_policy;
}

Aml_MS12_SyncPolicy_t ms12_sync_callback(void *priv_data, unsigned long long u64DecOutFrame, Aml_MS12_Delay_t stDelay, Aml_MS12_SyncPolicy_t syncpolicy_status __unused) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc  *main_dec_handle = aml_out->ms12_dec_handle;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;

    uint64_t apts = 0;
    uint64_t new_apts = 0;
    uint64_t consume_payload = 0;
    uint64_t decoded_frame = 0;
    Aml_MS12_SyncPolicy_t audio_sync_policy = {MS12_SYNC_AUDIO_NORMAL_OUTPUT, 0, 0};
    int ret = 0;
    audio_format_t audio_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);
    int delay_frame = 0;
    int delay_pts_diff = 0;
    int adjust_ms = 0;
    bool disable_adjust = false;
    int64_t  system_time_diff_ms = 0;

    struct timespec ts;
    struct timespec ms12_main_ts;
    uint64_t ms12_main_position = 0;
    uint64_t main_current_frame = 0;
    uint64_t last_frames_position_used = main_dec_handle->last_frames_position_used;

    audio_sync_policy = update_ms12_position(priv_data, u64DecOutFrame, stDelay);
    if (main_dec_handle->is_bypass_ms12)
        disable_adjust = true;

    if (!aml_out->hw_sync_mode) {
        return audio_sync_policy;
    }

    int debug_enable = get_debug_value(AML_DEBUG_AUDIOHAL_HW_SYNC);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&adev->ms12.main_apts_update_lock);
    ms12_main_ts = main_dec_handle->timestamp;
    ms12_main_position = main_dec_handle->last_frames_position;
    pthread_mutex_unlock(&adev->ms12.main_apts_update_lock);
    main_dec_handle->last_frames_position_used = ms12_main_position;

    /*the time may be changed, compensate the time diff on position*/
    system_time_diff_ms = calc_time_interval_us(&ms12_main_ts, &ts) / MSEC_PER_SEC;
    if (!is_float_equal(speed_info->speed, 1.0f)) {
        system_time_diff_ms = system_time_diff_ms * speed_info->speed;
    }
    main_current_frame = ms12_main_position + system_time_diff_ms * 48;

    /*ms12 main output is not ready*/
    if (ms12_main_position == 0 || main_dec_handle->ms12_position_update == false) {
        ALOGI("%s ms12 position is not ready ms12_main_position=%" PRId64 " update =%d", __func__, ms12_main_position, ms12->ms12_position_update);
        return audio_sync_policy;
    }

    /*get decoded frame and its pts*/
    consume_payload = dolby_ms12_get_main_bytes_consumed(stream_out);
    /*main pcm is resampled out of ms12, so the payload size is changed*/
    if (audio_is_linear_pcm(aml_out->hal_internal_format) && aml_out->hal_rate != 48000) {
        consume_payload = consume_payload * aml_out->hal_rate / 48000;
    }
    ret = aml_audio_hwsync_lookup_apts(aml_out->hwsync, consume_payload, &apts);

    // Fix : several hwsync header pts is the same, result in avsync jitter and inserting zero data
    if (audio_is_linear_pcm(aml_out->hal_internal_format) && ret == 0 && aml_out->hwsync->first_apts_flag) {
        if (apts == aml_out->last_hwsync_header_pts) {
            uint64_t pts_delta = 0;
            if (consume_payload > aml_out->last_payload_offset) {
                pts_delta = (consume_payload - aml_out->last_payload_offset) * 90 /(aml_out->hal_frame_size * 48);
                if (debug_enable) {
                    AM_LOGI("apts=%"PRIu64", consume_payload=(%"PRIu64", last=%"PRIu64"), frame_size=%d, pts_delta=%"PRIu64"",
                        apts, consume_payload, aml_out->last_payload_offset, aml_out->hal_frame_size, pts_delta);
                }
                apts += pts_delta;
            }
        } else {
            aml_out->last_payload_offset = consume_payload;
            aml_out->last_hwsync_header_pts = apts;
        }
    }

    aml_ms12_decoder_getparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_MAIN_PCMOUT_FRAME, &decoded_frame, sizeof(uint64_t));

    if (aml_out->hal_rate != 48000 && aml_out->hal_rate !=0 && !audio_is_linear_pcm(aml_out->hal_internal_format)) {
        decoded_frame = decoded_frame * 48000 / aml_out->hal_rate;
    }


    /*calculate the current frame pts*/
    if (decoded_frame >= main_current_frame) {
        delay_frame = (decoded_frame - main_current_frame);
        delay_pts_diff = delay_frame * 90 / 48;
        if (debug_enable) {
            ALOGI("%s : decoded_frame %" PRId64 ",  main_current_frame %" PRId64 ", delay_frame %d %d ms", __func__, \
                  decoded_frame, main_current_frame, delay_frame, delay_frame/48);
        }
    } else {
        ALOGI("%s decoded frame=%" PRId64 " current frame=%" PRId64 "", __func__, decoded_frame, main_current_frame);
        delay_pts_diff = 0;
    }
    if (ret == 0) {
        if (apts > delay_pts_diff) {
            new_apts = apts - delay_pts_diff;
        } else {
            new_apts = 0;
        }
    } else {
        if (aml_out->last_pts != 0) {
            /*
             * Due to ms12 continuous node's flow control,
             * each 256 frame time : u64DecOutFrame may not update, and main_current_frame keeps moving.
             * // new_apts = aml_out->last_pts + (u64DecOutFrame - aml_out->last_dec_out_frame) * 90 / 48;
            */
            uint64_t main_frame_offset = 0;
            if (main_current_frame > last_frames_position_used) {
                main_frame_offset = main_current_frame - last_frames_position_used;
            } else {
                AM_LOGW("main_current_frame %" PRId64 ", last_frames_position_used %" PRId64 "", main_current_frame, last_frames_position_used);
            }
            new_apts = aml_out->last_pts + main_frame_offset * 90 / 48;
        }
    }


    aml_audio_hwsync_audio_process(aml_out->hwsync, new_apts, &adjust_ms);

    /*pts is bigger than pts, we need wait some time*/
    // SWPL-171731 : netflix stream playing --> press HOME key, then video/pcr slowly stop.
    // but audio don't receive in time pause signal due to poor system performance.
    if (adjust_ms > 0 && !adev->is_netflix && !disable_adjust) {
        uint64_t target_time = aml_audio_get_systime() + adjust_ms * 1000 + stDelay.u32DelayFrame / 48 * 1000;
        uint64_t current_time = 0;
        uint64_t time_left = 0;
        main_dec_handle->main_input_insert_zero = true;
        ALOGI("%s begin wait %d ms delay=%d", __func__, adjust_ms, stDelay.u32DelayFrame / 48);
        do {
            current_time = aml_audio_get_systime();
            if (current_time >= target_time) {
                break;
            }
            time_left = target_time - current_time;
            if (time_left >= 5 *1000) {
                aml_audio_sleep(5 * 1000);
            }
            else {
                aml_audio_sleep(time_left);
            }
        } while(1);
        ALOGI("%s wait done", __func__);
    }


    if (debug_enable) {
        ALOGI("%s ms12 pos info: %p %"PRIu64", sec = %ld, nanosec = %ld\n",__func__,
        aml_out, ms12_main_position, ms12_main_ts.tv_sec, ms12_main_ts.tv_nsec);
        ALOGI("%s dec frame =%" PRId64 " out frame =%lld total_delay =%d ms12 delay=%d",
            __func__, decoded_frame, u64DecOutFrame, delay_frame, stDelay.u32DelayFrame);
        ALOGI("%s ori %" PRId64 " new pts %" PRId64 " diff =%d ms  last pts %" PRId64 " diff =%d ms", __func__,
            apts, new_apts, (int)(apts - new_apts) / 90, aml_out->last_pts, (int)(apts - aml_out->last_pts) / 90);

    }

    aml_out->last_dec_out_frame = u64DecOutFrame;
    aml_out->last_pts = new_apts;

    return audio_sync_policy;
}

#ifdef ENABLE_DVB_PATCH
Aml_MS12_SyncPolicy_t ms12_dtv_sync_callback(void *priv_data, unsigned long long u64DecOutFrame, Aml_MS12_Delay_t stDelay, Aml_MS12_SyncPolicy_t syncpolicy_status) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    aml_dtvsync_t *aml_dtvsync = NULL;
    struct dtvsync_audio_policy *async_policy = NULL;
    uint64_t apts = 0;
    uint64_t new_apts = 0;
    uint64_t consume_payload = 0;
    uint64_t decoded_frame = 0;
    Aml_MS12_SyncPolicy_t audio_sync_policy = {MS12_SYNC_AUDIO_NORMAL_OUTPUT, 0, 0};
    int ret = 0;
    audio_format_t audio_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);
    int delay_frame = 0;
    int delay_pts_diff = 0;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;
    int tempo_delay_frame = speed_info->last_latency_frame;
    bool skip_update_pts = false;
    int sync_enable = property_get_int32("vendor.media.dtvsync.enable", 1);
    aml_out->dtvsync_enable = is_dtv_stream_out(&aml_out->stream) && sync_enable;
    aml_ms12_decoder_getparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_MAIN_PCMOUT_FRAME, &decoded_frame, sizeof(uint64_t));
    int debug_enable = get_debug_value(AML_DEBUG_AUDIOHAL_HW_SYNC);
    unsigned int ms12_delay_frame = stDelay.u32DelayFrame * speed_info->speed;
    /* In the jira SWPL-199641, position and timestamp is used to be as the flag of unmute in the ATF audio only case,
     * therefore, update_ms12_position will update the true timestamp and position to audio track in the dual path logic.
     */
    update_ms12_position(priv_data, u64DecOutFrame, stDelay);
    if (aml_out->dtvsync_enable) {
        if (aml_out->is_eos) {
            ALOGI("%s output_thread_exit", __func__);
            return audio_sync_policy;
        }

        /*when it is bypass mode, only need to update the sync policy and not update cur_outapts pts*/
        if (ms12_dec->is_bypass_ms12) {
            skip_update_pts = true;
        }

        aml_dtvsync = (aml_dtvsync_t *)aml_out->hwsync->mediasync;
        consume_payload = dolby_ms12_get_main_bytes_consumed(stream_out);

        /*aac stream may be reset during the playing, we need calculate consume basing on the offset*/
        if (is_aac_format(audio_format)) {
            consume_payload += ms12->dtv_decoder_offset_base;
        }

        if (aml_out->hal_rate != 48000 && aml_out->hal_rate !=0) {
            decoded_frame = decoded_frame * 48000 / aml_out->hal_rate;
        }
        if (decoded_frame > u64DecOutFrame) {
            delay_frame = decoded_frame - u64DecOutFrame;
        }
        delay_pts_diff = (delay_frame + ms12_delay_frame + tempo_delay_frame) * 90 / 48;

        if (debug_enable) {
            ALOGI("%s dec frame =%" PRId64 " out frame =%lld decoded_delay =%d ms12 delay=%d tempo delay=%d total delay =%d  =%d ms last_dec_out_frame =%" PRId64 "",
                __func__, decoded_frame, u64DecOutFrame, delay_frame, ms12_delay_frame, tempo_delay_frame, (delay_frame + ms12_delay_frame + tempo_delay_frame), delay_pts_diff / 90, aml_out->last_dec_out_frame);
            ALOGI("%s in policy =%d tag frame =%d cur_frame=%d", __func__, syncpolicy_status.eSyncPolicy, syncpolicy_status.s32TagFrame, syncpolicy_status.s32CurFrame);
        }

        if (syncpolicy_status.eSyncPolicy == DTVSYNC_AUDIO_NORMAL_OUTPUT && decoded_frame - aml_out->last_dec_out_pcm_frame < 1536) {
            return audio_sync_policy;
        }

        if (aml_dtvsync) {
            async_policy = &(aml_dtvsync->apolicy);
            ret = aml_audio_hwsync_lookup_apts(aml_out->hwsync, consume_payload, &apts);
            if (ret == 0 && aml_dtvsync->last_lookup_apts != apts) {
                if (get_debug_value(AML_DEBUG_AUDIOHAL_AUT)) {
                    AM_LOGI("[AUT_PRINT] pts lookup success.");
                }

                aml_dtvsync->last_lookup_apts = apts;
                if (apts > delay_pts_diff) {
                    new_apts = apts - delay_pts_diff;
                } else {
                    new_apts = 0;
                }
            } else {
                if (get_debug_value(AML_DEBUG_AUDIOHAL_AUT)) {
                    AM_LOGI("[AUT_PRINT] pts lookup fail.");
                }

                if (aml_dtvsync->cur_outapts && aml_dtvsync->cur_outapts != DTVSYNC_INIT_PTS) {
                    if (u64DecOutFrame >= aml_out->last_dec_out_frame)
                        new_apts = aml_dtvsync->cur_outapts + (u64DecOutFrame - aml_out->last_dec_out_frame) * 90 / 48;
                    else
                        new_apts = aml_dtvsync->cur_outapts;
                }
            }

            if (debug_enable) {
                ALOGI("%s original pts = 0x%" PRIx64 " =%" PRId64 " ms new pts = 0x%" PRIx64 " =%" PRId64 " ms", __func__, apts, apts / 90, new_apts, new_apts / 90);
                ALOGI("%s last pts = 0x%" PRIx64 " = %" PRId64 " ms  new = 0x%" PRIx64 " = %" PRId64 " ms diff =%" PRId64 "", __func__, aml_dtvsync->cur_outapts, aml_dtvsync->cur_outapts / 90, new_apts, new_apts/90, ((int64_t)new_apts - (int64_t)aml_dtvsync->cur_outapts) / 90);
            }

            if (new_apts == 0) {
                if (debug_enable) {
                    ALOGE("%s can't get pts", __func__);
                }

            }
            if (new_apts) {
                int ms12_tuning_delay_pts = aml_audio_dtv_get_ms12_latency(stream_out) * 1000 * MILLISECOND_2_PTS / 48000;
                int force_setting_delay_pts = 0;
                if (is_HDMI_connected(adev)) {
                    force_setting_delay_pts = aml_getprop_int(PROPERTY_LOCAL_PASSTHROUGH_LATENCY)  * MILLISECOND_2_PTS;
                }
                if (!skip_update_pts)
                    aml_dtvsync->cur_outapts = new_apts;

                if ((syncpolicy_status.eSyncPolicy == DTVSYNC_AUDIO_DROP_PCM) ||
                    (syncpolicy_status.eSyncPolicy == DTVSYNC_AUDIO_INSERT)) {
                    /*we still need to do drop or insert*/
                    if (syncpolicy_status.s32TagFrame > syncpolicy_status.s32CurFrame) {
                        aml_out->last_dec_out_frame = u64DecOutFrame;
                        audio_sync_policy.eSyncPolicy = syncpolicy_status.eSyncPolicy;
                        audio_sync_policy.s32TagFrame = syncpolicy_status.s32TagFrame;
                        audio_sync_policy.s32CurFrame = syncpolicy_status.s32CurFrame;
                        return audio_sync_policy;
                    }
                }

                if (!skip_update_pts)
                    aml_dtvsync->cur_outapts = new_apts + ms12_tuning_delay_pts + force_setting_delay_pts;
                ms12_do_dtv_sync(stream_out);
                if (!skip_update_pts)
                    aml_dtvsync->cur_outapts = new_apts;

                if (get_debug_value(AML_DEBUG_AUDIOHAL_AUT)) {
                    AM_LOGI("[AUT_PRINT] output_pts:0x%" PRIx64 ".", aml_dtvsync->cur_outapts);
                }

                if (async_policy->audiopolicy != DTVSYNC_AUDIO_NORMAL_OUTPUT)
                    ALOGI("cur policy:%d, prm1:%d, prm2:%d\n", async_policy->audiopolicy,
                        async_policy->param1, async_policy->param2);

                if (async_policy->audiopolicy == DTVSYNC_AUDIO_DROP_PCM) {
                    audio_sync_policy.eSyncPolicy = MS12_SYNC_AUDIO_DROP_PCM;
                    int drop_frames = async_policy->param1 / 1000 * 48;
                    audio_sync_policy.s32TagFrame = drop_frames;
                    audio_sync_policy.s32CurFrame = 0;
                    ALOGI("%s drop frames =%d tag frame =%d cur_frame=%d", __func__, drop_frames, audio_sync_policy.s32TagFrame, audio_sync_policy.s32CurFrame);
                } else if (async_policy->audiopolicy == DTVSYNC_AUDIO_INSERT) {
                    int insert_frames = async_policy->param1 / 1000 * 48;
                    audio_sync_policy.eSyncPolicy = MS12_SYNC_AUDIO_INSERT;
                    audio_sync_policy.s32TagFrame = insert_frames;
                    audio_sync_policy.s32CurFrame = 0;
                    ALOGI("%s insert %d ms frame =%d tag frame =%d cur_frame=%d ", __func__, async_policy->param1/1000, insert_frames, audio_sync_policy.s32TagFrame, audio_sync_policy.s32CurFrame);
                } else if (async_policy->audiopolicy == DTVSYNC_AUDIO_ADJUST_CLOCK) {
                    //aml_dtvsync_ms12_adjust_clock(stream_out, async_policy->param1);
                } else if (async_policy->audiopolicy == DTVSYNC_AUDIO_RESAMPLE) {
                    aml_dtvsync_ms12_process_resample(stream_out, async_policy);
                } else if (async_policy->audiopolicy == DTVSYNC_AUDIO_MUTE) {
                    //enable_dtv_underrun_mute(adev, true);
                } else if (async_policy->audiopolicy == DTVSYNC_AUDIO_NORMAL_OUTPUT) {
                    //
                }
            }
        }

    }
    aml_out->last_dec_out_frame = u64DecOutFrame;
    aml_out->last_dec_out_pcm_frame = decoded_frame;

    if ((audio_sync_policy.eSyncPolicy == DTVSYNC_AUDIO_DROP_PCM || audio_sync_policy.eSyncPolicy == DTVSYNC_AUDIO_INSERT)
        && (audio_sync_policy.s32TagFrame < 0 || audio_sync_policy.s32CurFrame < 0 || audio_sync_policy.s32CurFrame > audio_sync_policy.s32TagFrame)) {
        ALOGE("%s, get error policy, policy=%d, tag frame =%d, cur_frame=%d, reset sync policy.", __func__, audio_sync_policy.eSyncPolicy, audio_sync_policy.s32TagFrame, audio_sync_policy.s32CurFrame);
        audio_sync_policy.eSyncPolicy = MS12_SYNC_AUDIO_NORMAL_OUTPUT;
        audio_sync_policy.s32TagFrame = 0;
        audio_sync_policy.s32CurFrame = 0;
    }
    return audio_sync_policy;
}

void ms12_do_dtv_sync(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    aml_dtvsync_t *aml_dtvsync = NULL;

    if (aml_out->dtvsync_enable) {
        aml_dtvsync = (aml_dtvsync_t *)aml_out->hwsync->mediasync;
        if (aml_dtvsync->cur_outapts > DTVSYNC_APTS_THRESHOLD) {
            aml_dtvsync_ms12_get_policy(stream);
        } else {
            ALOGI("Invalid cur_outapts: %" PRId64 "", aml_dtvsync->cur_outapts);
            aml_dtvsync->apolicy.audiopolicy= DTVSYNC_AUDIO_NORMAL_OUTPUT;
        }
   }

}
#endif

int dolby_ms12_get_latency(struct dolby_ms12_desc *ms12, audio_format_t output_format)
{
    int ms12_total_delay_frames = 0;
    int ms12_codecbuf_delay = 0;
    audio_format_t hal_internal_format;

    if (ms12 == NULL) {
        return 0;
    }

    hal_internal_format = ms12->input_config_format;
    //get Main codecbuf buffer level
    //dolby_ms12_get_latency_for_stereo_out(&ms12_codecbuf_delay);

    if (output_format == AUDIO_FORMAT_AC3 || output_format == AUDIO_FORMAT_E_AC3) {
       ms12_total_delay_frames += 1473; //add udc delay
       ms12_total_delay_frames += 32; //add sys mixer delay
    } else if (output_format == AUDIO_FORMAT_MAT ||output_format == AUDIO_FORMAT_DOLBY_TRUEHD) {
       ms12_total_delay_frames += 40; //add thd delay
       ms12_total_delay_frames += 32; //add sys mixer delay
    }

    ms12_total_delay_frames += ms12_codecbuf_delay; //add codebuf buffer level

    if (output_format == AUDIO_FORMAT_PCM_16_BIT || output_format == AUDIO_FORMAT_PCM_32_BIT) {
        ms12_total_delay_frames += 256; //add pcmr delay
        //ms12_total_delay_frames += aml_alsa_output_get_delayframe((struct audio_stream_out*)ms12->ms12_main_stream_out->dev->ms12_out); //add alsa delay
    } else if (output_format == AUDIO_FORMAT_AC3 || output_format == AUDIO_FORMAT_E_AC3) {
        ms12_total_delay_frames += 256; //add pcmr delay
        ms12_total_delay_frames += 3072; //add total enc and output delay
    } else if (output_format == AUDIO_FORMAT_MAT) {
        ms12_total_delay_frames += 240; //add pcmr delay
        ms12_total_delay_frames += 2048; //add total enc and output delay
    }

    ALOGV("%s line %d error: ms12_codecbuf_delay %d, ms12_total_delay_frames %d, input_format 0x%x, output_format 0x%x", __func__, __LINE__, ms12_codecbuf_delay, ms12_total_delay_frames, hal_internal_format, output_format);
    return ms12_total_delay_frames;
}

static audio_format_t correct_the_output_format_for_only_dolby_truehd(audio_format_t out_format)
{
    /* for dolby truehd case, we don't support mat, only support ddp*/
    if (out_format == AUDIO_FORMAT_DOLBY_TRUEHD) {
        return AUDIO_FORMAT_E_AC3;
    } else {
        return out_format;
    }
}

static int correct_the_duration_by_align_the_mat_frame_header(char *data, size_t len)
{
    if (data && (len > 4)) {
        /* if it is the sync word of MAT, it means the beginning of MAT*/
        if ((data[0] == 0x7) && (data[1] == 0x9e) && (data[2] == 0x0) && (data[3] == 0x4)) {
            return MILLISECOND_2_PTS * 20;/* ms12 output every mat frame duration is 20ms*/
        }
        else {
            return 0;
        }
    }
    else {
        ALOGE("%s line %d error: data %p len %zu ", __func__, __LINE__, data, len);
        return 0;
    }
}

int ms12_output(void *buffer, void *priv_data, size_t size, aml_ms12_dec_info_t *ms12_info)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);

    audio_format_t hal_internal_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);
    audio_format_t output_format = (ms12_info) ? ms12_info->data_type : AUDIO_FORMAT_PCM_16_BIT;
    unsigned int main_apts_high32b = (ms12_info) ? ms12_info->main_apts_high32b : 0;
    unsigned int main_apts_low32b = (ms12_info) ? ms12_info->main_apts_low32b : 0;
    unsigned int main1_apts_high32b = (ms12_info) ? ms12_info->main1_apts_high32b : 0;
    unsigned int main1_apts_low32b = (ms12_info) ? ms12_info->main1_apts_low32b : 0;
    int ret = 0;

    if (adev->debug_flag > 1) {
        ALOGI("+%s() output size %zu,out format 0x%x. ch=%d dual_output = %d, optical_format = 0x%x, sink_format = 0x%x, out total=%d main in=%d",
            __FUNCTION__, size,output_format, ms12_info->output_ch, aml_out->dual_output_flag, adev->optical_format, adev->sink_format,
            ms12->bitstream_cnt, ms12->input_total_ms);
    }

    if (output_format == 0) {
        ALOGE("%s output format error", __func__);
        return 0;
    }

    /*when arc is connected, we need reset all the spdif output,
      because earc port to be reopened.
    */
    if (adev->arc_connected_reconfig ||
        adev->sink_format_changed ||
        ms12->focus_is_bypass_ms12 != ms12->last_focus_is_bypass_ms12) {
        if (adev->arc_connected_reconfig) {
            ALOGI("arc is reconnected, reset spdif output");
        }
        if (adev->sink_format_changed) {
            ALOGI("sink_format_changed, reset spdif output");
        }
        if (ms12->focus_is_bypass_ms12 != ms12->last_focus_is_bypass_ms12) {
            ALOGI("bypass mode change from %d tp %d", ms12->last_focus_is_bypass_ms12, ms12->focus_is_bypass_ms12);
        }
        ms12_close_all_spdifout(ms12);
        adev->arc_connected_reconfig = false;
        adev->sink_format_changed = false;
        if (ms12->focus_is_bypass_ms12 != ms12->last_focus_is_bypass_ms12) {
            ms12->last_focus_is_bypass_ms12 = ms12->focus_is_bypass_ms12;
        }
    }

    if (adev->aaudio_low_latency_updated && adev->aaudio_low_latency != ms12->aaudio_low_latency) {
        ALOGI("aaudio_low_latency_updated(%d -> %d), reset spdif output", ms12->aaudio_low_latency, adev->aaudio_low_latency);
        ms12_close_all_spdifout(ms12);
        ms12->aaudio_low_latency = adev->aaudio_low_latency;
        adev->aaudio_low_latency_updated = false;
    }

    /*update the master pcm frame, which is used for av sync*/
    if (audio_is_linear_pcm(output_format) && ms12_info) {
        if (ms12_info->pcm_type == NORMAL_LPCM && (ms12_info->output_ch == 8 || ms12_info->output_ch == 6)) {
            ms12_info->pcm_type = MC_LPCM;
        }
        if (ms12_info->pcm_type == DAP_LPCM) {
            if (is_dolbyms12_dap_enable(aml_out)) {
                ms12->master_pcm_frames += size / (audio_bytes_per_sample(output_format) * ms12_info->output_ch);
            }
        } else if (ms12_info->pcm_type == NORMAL_LPCM) {
            if (!is_dolbyms12_dap_enable(aml_out)) {
                ms12->master_pcm_frames += size / (audio_bytes_per_sample(output_format) * ms12_info->output_ch);
            }
        }
    }

    if (audio_is_linear_pcm(output_format) && ms12_info) {
        if (ms12_info->pcm_type == MC_LPCM) {
            audio_continuous_standby_attachframe(ms12->continuous_standby_handle, buffer, size, STANDBY_REPEAT_FORMAT_MCH, ms12_info);
            mc_pcm_output(buffer, priv_data, size, ms12_info);
        } else if (ms12_info->pcm_type == DAP_LPCM) {
            if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
                check_audio_level("ms12_dap_pcm", buffer, size);
            }
            audio_continuous_standby_attachframe(ms12->continuous_standby_handle, buffer, size, STANDBY_REPEAT_FORMAT_DAP, ms12_info);
            dap_pcm_output(buffer, priv_data, size, ms12_info);
        } else {
            if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
                check_audio_level("ms12_stereo_pcm", buffer, size);
            }
            audio_continuous_standby_attachframe(ms12->continuous_standby_handle, buffer, size, STANDBY_REPEAT_FORMAT_PCM, ms12_info);
            stereo_pcm_output(buffer, priv_data, size, ms12_info);
        }
    } else {
        if (output_format == AUDIO_FORMAT_E_AC3) {
            audio_continuous_standby_attachframe(ms12->continuous_standby_handle, buffer, size, STANDBY_REPEAT_FORMAT_DDP, ms12_info);
            bitstream_output(buffer, priv_data, size);
        } else if (output_format == AUDIO_FORMAT_AC3) {
            audio_continuous_standby_attachframe(ms12->continuous_standby_handle, buffer, size, STANDBY_REPEAT_FORMAT_DD, ms12_info);
            spdif_bitstream_output(buffer, priv_data, size);
        } else if (output_format == AUDIO_FORMAT_MAT) {
            if (correct_the_duration_by_align_the_mat_frame_header(buffer, size)) {
                audio_continuous_standby_attachframe(ms12->continuous_standby_handle, buffer, size, STANDBY_REPEAT_FORMAT_MAT_UPPER, ms12_info);
            } else {
                audio_continuous_standby_attachframe(ms12->continuous_standby_handle, buffer, size, STANDBY_REPEAT_FORMAT_MAT_LOWER, ms12_info);
            }
            mat_bitstream_output(buffer, priv_data, size);
        } else {
            ALOGE("%s  abnormal output_format:0x%x", __func__, output_format);
        }
    }
    return ret;
}

/*
 * data type: 32bit float little-endian non-interleaved
 * data type: 32bit float little-endian non-interleaved
 */
int ms12_scaletempo(void *priv_data, void *info) {
    if (priv_data == NULL || info == NULL) {
        return -1;
    }

    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);

    if (ms12->scaletempo == NULL) {
        ALOGE("%s %d: error parameters ", __func__, __LINE__);
        return -1;
    }

    hal_scaletempo_process(ms12->scaletempo, (aml_scaletempo_info_t *)info);

    return 0;
}

int ms12_tempo_callback(void *priv_data, void *info) {
    if (priv_data == NULL || info == NULL) {
        return -1;
    }

    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    Aml_MS12_TempoInfo_t *pstTempoInfo = (Aml_MS12_TempoInfo_t *)info;
    int in_sample_size = pstTempoInfo->s32InSampleSize;
    audio_speed_config_t speed_config;
    void *speed_out_buffer = NULL;
    size_t speed_out_bytes = 0;
    float final_speed = ms12_dec->tempo_speed;
    bool apts_gap_easing_done = false;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;
    aml_audio_speed_apts_gap_ease_t *gap_ease = &speed_info->apts_gap_ease;

    bool debug_enable = get_debug_value(AML_DEBUG_AUDIOHAL_SPEED);
    int tempo_latency_frame = 0;
    bool dtv_stream_flag = is_dtv_stream_out((struct audio_stream_out *)aml_out);

    if ((!is_float_equal(ms12_dec->tempo_speed, 1.0f) || speed_info->speed_handle) && (in_sample_size == 2 || in_sample_size == 4)) {
        if (debug_enable) {
            ALOGI("%s InBufferSize=%d, InSampleSize=%u, tempo_speed=%f Channel=%d\n", __FUNCTION__,
                pstTempoInfo->u32InBufferSize, in_sample_size, ms12_dec->tempo_speed, pstTempoInfo->s32Channel);
        }
        aml_audio_speed_update_post_delay(&speed_info->post_delay, ms12_dec->tempo_speed, ms12_dec->last_post_buffer_frame);
        if (!dtv_stream_flag) {
            ms12_stream_config_apts_gap_easing(aml_out);
        }

        if (gap_ease->target_frames > 0 && !dtv_stream_flag) {
            int diff_frames = 0;

            final_speed = gap_ease->speed;
            gap_ease->current_frames += (pstTempoInfo->u32InBufferSize/(pstTempoInfo->s32InSampleSize * pstTempoInfo->s32Channel));
            diff_frames = gap_ease->current_frames - gap_ease->target_frames;
            tempo_latency_frame = speed_info->last_latency_frame;
            if (abs(diff_frames) <= 2048 && tempo_latency_frame <= 256 && tempo_latency_frame > 0) {
                apts_gap_easing_done = true;
            } else if (diff_frames > 2048) {
                apts_gap_easing_done = true;
            }
        }
        if (apts_gap_easing_done) {
            gap_ease->speed = speed_info->speed;
            gap_ease->target_frames = 0;
            gap_ease->current_frames = 0;
            gap_ease->start = false;
        }

        speed_config.aformat = AUDIO_FORMAT_PCM_FLOAT;
        speed_config.speed = final_speed;
        speed_config.input_sr = pstTempoInfo->s32SampleRate;
        speed_config.channels = pstTempoInfo->s32Channel;

        ret = aml_audio_speed_process_wrapper(&speed_info->speed_handle, pstTempoInfo->pu8InBuffer, \
                                pstTempoInfo->u32InBufferSize, &speed_out_buffer, &speed_out_bytes, &speed_config);
        if (ret != 0) {
            ALOGE("aml_audio_speed_process_wrapper failed");
        } else {
            pstTempoInfo->s32OutSampleSize = 4;   // ms12 always use float data
            pstTempoInfo->pu8OutBuffer = speed_out_buffer;
            pstTempoInfo->u32OutBufferSize = speed_out_bytes;

            // _Aml_MS12_CodecBufNodeActivate : syncPolicyProcess is ahead of tempo_process
            tempo_latency_frame = aml_audio_speed_get_latency_frames(speed_info->speed_handle);
            if (tempo_latency_frame < 0) {
                tempo_latency_frame = 0;
            }
            speed_info->last_latency_frame = tempo_latency_frame;
            if (debug_enable) {
                int in_frame_bytes = pstTempoInfo->s32InSampleSize * pstTempoInfo->s32Channel;
                int out_frame_bytes = pstTempoInfo->s32OutSampleSize * pstTempoInfo->s32Channel;
                ALOGI("%s : speed %.3f in_frame %d, out_frame %d, latency_frame %d", __FUNCTION__, ms12_dec->tempo_speed, \
                    pstTempoInfo->u32InBufferSize/in_frame_bytes, pstTempoInfo->u32OutBufferSize/out_frame_bytes, speed_info->last_latency_frame);
            }
        }
    } else {
        if (speed_info->speed_handle) {
            aml_audio_speed_close(speed_info->speed_handle);
            speed_info->speed_handle = NULL;
        }
        pstTempoInfo->s32OutSampleSize = pstTempoInfo->s32InSampleSize;
        pstTempoInfo->pu8OutBuffer = pstTempoInfo->pu8InBuffer;
        pstTempoInfo->u32OutBufferSize = pstTempoInfo->u32InBufferSize;
        speed_info->last_latency_frame = 0;
    }
    pstTempoInfo->f32TempoSpeed = ms12_dec->tempo_speed;
    return ret;
}

int ms12_process_callback(void *priv_data, void *info) {
    if (priv_data == NULL || info == NULL) {
        return -1;
    }

    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    Aml_MS12_ProcessInfo_t *pstProcessInfo = (Aml_MS12_ProcessInfo_t *)info;

    ALOGV("FrameType %d, InBuffer %p, InBufferSize %d", pstProcessInfo->s32InFrameType, pstProcessInfo->pu8InBuffer, pstProcessInfo->u32InBufferSize);
    ms12_decoder_volume_process(aml_out, pstProcessInfo);
    ms12_decoder_sound_mode_process(aml_out, pstProcessInfo);

    pstProcessInfo->s32OutFrameType = pstProcessInfo->s32InFrameType;
    pstProcessInfo->pu8OutBuffer = pstProcessInfo->pu8InBuffer;
    pstProcessInfo->u32OutBufferSize = pstProcessInfo->u32InBufferSize;

    return ret;
}

static int audio_enhancment_process(struct aml_audio_device *adev, Aml_MS12_ProcessInfo_t *pstProcessInfo) {
    Aml_MS12_ProcessInfo_t *Info = pstProcessInfo;
    aml_audio_enhancement_module_t *pAudioEnhancementModule = adev->native_postprocess.audio_enhancment_handle;

    if (!pAudioEnhancementModule || !pAudioEnhancementModule->audio_enhancement_enable || !pAudioEnhancementModule->channel_width) {
        return 0;
    }

    audio_channel_mask_t channel_mask = acmod_convert_to_channel_mask(Info->as32Acmod[0], Info->as32Acmod[1]);
    aml_update_audio_channel_mask(&adev->native_postprocess, channel_mask);

    audio_buffer_t in_buf;
    audio_buffer_t out_buf;
    pAudioEnhancementModule->channel_width = Info->s32Channel;
    int frames = Info->u32InBufferSize / sizeof(float) / Info->s32Channel;
    in_buf.frameCount =  out_buf.frameCount = frames;
    in_buf.raw = out_buf.raw = Info->pu8InBuffer;

    memcpy_to_i32_from_float(in_buf.raw, in_buf.raw, Info->u32InBufferSize / sizeof(float));
    aml_audio_enhancement_module_process(pAudioEnhancementModule, &in_buf, &out_buf);
    memcpy_to_float_from_i32(out_buf.raw, out_buf.raw, Info->u32InBufferSize / sizeof(float));

    return 0;
}

static int audio_aloop_write_delay(struct aml_audio_device *adev, Aml_MS12_ProcessInfo_t *pstProcessInfo) {
    Aml_MS12_ProcessInfo_t *Info = pstProcessInfo;
    pcm_record_delay_t *pcm_record = &adev->aml_pcm_record_delay;

    if (!pcm_record->aloop_write_enable) {
        return 0;
    }

    audio_channel_mask_t channel_mask = acmod_convert_to_channel_mask(Info->as32Acmod[0], Info->as32Acmod[1]);
    void *buffer = (void *) Info->pu8InBuffer;
    int bytes = Info->u32InBufferSize;
    pcm_record->channel_width = Info->s32Channel;
    pcm_record->channel_mask = channel_mask;
    pcm_record->format = AUDIO_FORMAT_PCM_FLOAT;

    aml_audio_aloop_write(pcm_record, buffer, bytes);
    aml_audio_data_delay(pcm_record, buffer, bytes);

    return 0;
}

int ms12_content_process_callback(void *priv_data, void *info) {
    if (priv_data == NULL || info == NULL) {
        return -1;
    }

    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)priv_data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    Aml_MS12_ProcessInfo_t *pstProcessInfo = (Aml_MS12_ProcessInfo_t *)info;

    ALOGV("%s FrameType %d, InBuffer %p, InBufferSize %d", __FUNCTION__,
        pstProcessInfo->s32InFrameType, pstProcessInfo->pu8InBuffer, pstProcessInfo->u32InBufferSize);

    audio_enhancment_process(adev, pstProcessInfo);
    audio_aloop_write_delay(adev, pstProcessInfo);

    pstProcessInfo->s32OutFrameType = pstProcessInfo->s32InFrameType;
    pstProcessInfo->pu8OutBuffer = pstProcessInfo->pu8InBuffer;
    pstProcessInfo->u32OutBufferSize = pstProcessInfo->u32InBufferSize;

    return ret;
}

static void dolby_ms12_sleep(struct aml_audio_device *adev, struct dolby_ms12_desc *ms12, int alsa_delay_frame)
{
    int sleep_time_us = 1000;

    if (ms12 == NULL) {
        return;
    }
    if (adev->ms12_dynamic_sleep == false) {
        if (ms12->scheduler_sleep_enable != true) {
            set_ms12_scheduler_sleep(ms12, true);
        }
        return;
    } else {
        if (ms12->scheduler_sleep_enable != false) {
            set_ms12_scheduler_sleep(ms12, false);
        }
    }

    if (ms12->scheduler_run_count <= 300) {
        // ms12 output is not stable, use default value
    } else if (adev->aaudio_low_latency == true) {
        // low_latency mode, need alsa buffer level more stable.
        sleep_time_us = 1000;
    } else if (alsa_delay_frame > ms12->alsa_limit_frame/2) {
        sleep_time_us = 2000;
    } else if (alsa_delay_frame <= 6*48) {
        // alsa buffer level too low ( <= 6ms), need to speed up
        sleep_time_us = 0;
    }

    if (sleep_time_us > 0) {
        usleep(sleep_time_us);
    }
}

static void *dolby_ms12_threadloop(void *data)
{
    ALOGI("+%s() ", __FUNCTION__);
    struct aml_stream_out *aml_out = (struct aml_stream_out *)data;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    if (ms12 == NULL) {
        ALOGE("%s ms12 pointer invalid!", __FUNCTION__);
        goto Error;
    }

    if (ms12->dolby_ms12_enable) {
        dolby_ms12_set_quit_flag(ms12->dolby_ms12_thread_exit);
    }

    prctl(PR_SET_NAME, (unsigned long)"DOLBY_MS12");
    aml_set_thread_priority("DOLBY_MS12", ms12->dolby_ms12_threadID);

    /*affinity the thread to cpu/apu which has few IRQ*/
    aml_audio_set_cpu_affinity(true);

    while ((ms12->dolby_ms12_thread_exit == false) && (ms12->dolby_ms12_enable)) {
        ALOGV("%s() goto dolby_ms12_scheduler_run", __FUNCTION__);
        if (ms12->dolby_ms12_ptr) {
            int delayframe = aml_alsa_output_get_delayframe((struct audio_stream_out*)adev->ms12_out);
            /*if alsa is not running, set the delay to 0 and ms12 will feed more data*/
            if (!adev->ms12_out->alsa_running_status) {
                delayframe = 0;
                ALOGI("%s alsa is not running", __func__);
            }
            //if alsa status error, ms12 still continuous output.
            if (delayframe < 0) {
                delayframe = 0;
            }
            if (audio_continuous_standby_check(ms12->continuous_standby_handle)) {
                audio_continuous_standby_run(ms12->continuous_standby_handle, delayframe);
            } else {
                dolby_ms12_set_alsa_delay_frame(delayframe);
                dolby_ms12_scheduler_run(ms12->dolby_ms12_ptr);
                ms12->scheduler_run_count++;
                dolby_ms12_sleep(adev, ms12, delayframe);
            }
        } else {
            ALOGE("%s() ms12->dolby_ms12_ptr is NULL, fatal error!", __FUNCTION__);
            break;
        }
        ALOGV("%s() dolby_ms12_scheduler_run end", __FUNCTION__);
        if (ms12->ms12_continuous_state == MS12_SCHEDULER_STANDBY) {
            ALOGD("%s  ms12 continuous start standby wait ....\n", __FUNCTION__);
            if (sem_wait(&ms12->standby_sem)) {
                ALOGE("%s wait ms12 semaphore failed\n", __FUNCTION__);
            } else {
                ALOGD("%s wait ms12 semaphore successful, currently wakedup.\n", __FUNCTION__);
            }
            aml_out->trace_last_write_time_ms = 0;
        }
    }
    ALOGI("%s remove   ms12 stream %p", __func__, aml_out);
    if (continuous_mode(adev)) {
        pthread_mutex_lock(&adev->alsa_pcm_lock);
        aml_alsa_output_close((struct audio_stream_out*)aml_out);
        pthread_mutex_unlock(&adev->alsa_pcm_lock);
    }
    ALOGI("-%s(), exit dolby_ms12_thread\n", __FUNCTION__);
    return ((void *)0);

Error:
    ALOGI("-%s(), exit dolby_ms12_thread, because of error input params\n", __FUNCTION__);
    return ((void *)0);
}

int set_system_app_mixing_status(struct aml_stream_out *aml_out, int stream_status)
{
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int system_app_mixing_status = SYSTEM_APP_SOUND_MIXING_OFF;
    int ret = 0;

    if (STREAM_STANDBY == stream_status) {
        system_app_mixing_status = SYSTEM_APP_SOUND_MIXING_OFF;
    } else {
        system_app_mixing_status = SYSTEM_APP_SOUND_MIXING_ON;
    }

    adev->system_app_mixing_status = system_app_mixing_status;

    //when under continuous_audio_mode, system app sound mixing always on.
    if (adev->continuous_audio_mode) {
        system_app_mixing_status = SYSTEM_APP_SOUND_MIXING_ON;
    }

    if (adev->debug_flag) {
        ALOGI("%s stream-status %d set system-app-audio-mixing %d current %d continuous_audio_mode %d\n", __func__,
              stream_status, system_app_mixing_status, dolby_ms12_get_system_app_audio_mixing(), adev->continuous_audio_mode);
    }

    dolby_ms12_set_system_app_audio_mixing(system_app_mixing_status);

    if (ms12->dolby_ms12_enable) {
        set_dolby_ms12_runtime_system_mixing_enable(ms12, system_app_mixing_status);
        ALOGI("%s return %d stream-status %d set system-app-audio-mixing %d\n",
              __func__, ret, stream_status, system_app_mixing_status);
        return ret;
    }

    return 1;
}


static int nbytes_of_dolby_ms12_downmix_output_pcm_frame()
{
    int pcm_out_channels = 2;
    int bytes_per_sample = 2;

    return pcm_out_channels*bytes_per_sample;
}

void dolby_ms12_create_dec_handle(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct dolby_ms12_dec_desc *dec_handle = NULL;
    if (aml_out->ms12_dec_handle == NULL) {
        dec_handle = (struct dolby_ms12_dec_desc *) calloc(1, sizeof(struct dolby_ms12_dec_desc));
        if (dec_handle == NULL) {
            ALOGE("%s malloc failed\n", __FUNCTION__);
            return;
        }
        aml_out->ms12_dec_handle = dec_handle;
    }
}

void dolby_ms12_release_dec_handle(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    if (aml_out->ms12_dec_handle) {
        free(aml_out->ms12_dec_handle);
        aml_out->ms12_dec_handle = NULL;
    }
}

int dolby_ms12_main_open(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    int ret = 0, associate_audio_mixing_enable = 0 , media_presentation_id = -1, mixing_level = 0,ad_vol = 100;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    uint32_t dtv_decoder_offset_base = 0;
    unsigned int sample_rate = aml_out->hal_rate;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;
    bool is_asdk_test = property_get_bool("persist.vendor.audio.ms12.default.values", false);

    AML_MS12_CodecInfo_t codec_info;

    memset(&codec_info, 0, sizeof(AML_MS12_CodecInfo_t));

    ms12_dec = aml_out->ms12_dec_handle;
    if (is_float_equal(ms12_dec->tempo_speed, 0)) {
        ms12_dec->tempo_speed = 1.0f;
    }
    ALOGI("%s ms12_dec->tempo_speed = %f", __FUNCTION__, ms12_dec->tempo_speed);

#ifdef ENABLE_DVB_PATCH
    aml_dtv_audiopara_t * dtv_audio_info = NULL;
    aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)aml_out->audio_buffer;
    aml_audio_buffer_t *audioBuffer = pBuffer->inBuffer;
    if (audioBuffer) {
        dtv_audio_info = (aml_dtv_audiopara_t *)audioBuffer->privObject;
    }
    bool dtv_stream_flag = is_dtv_stream_out(stream);
#endif

    audio_format_t hal_internal_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);

    /*
    when HDMITX send pause frame,we treated as INVALID format.
    for MS12,we treat it as LPCM and mute the frame
    */
    if (hal_internal_format == AUDIO_FORMAT_INVALID ||
        !is_dolby_ms12_support_compression_format(hal_internal_format)) {
        hal_internal_format = AUDIO_FORMAT_PCM_16_BIT;
    }
    get_sink_format (stream);
    {
        /*SWPL-173341: if the deef buffer is active, this main stream can preempt it*/
        //checking preempt loggic was moved to stream manager code.
        aml_out->is_preempt_system_audio_usage_media_stream = aml_is_preempt_deep_buffer_stream(aml_out);
    }

    aml_out->is_ms12_main_decoder = true;

    ms12_dec->is_bypass_ms12 = is_ms12_passthrough(stream);
    ms12_dec->resume_state = MS12_RESUME_NONE;

    codec_info.u32AudioFormat = hal_internal_format;
    if (aml_out->virtual_buf_handle == NULL) {
        uint64_t buf_ns_begin  = MS12_MAIN_INPUT_BUF_NONEPCM_NS;
        uint64_t buf_ns_target = MS12_MAIN_INPUT_BUF_NONEPCM_NS;
        if (audio_is_linear_pcm(aml_out->hal_internal_format)) {
            buf_ns_begin  = MS12_MAIN_INPUT_BUF_PCM_NS;
            buf_ns_target = MS12_MAIN_INPUT_BUF_PCM_NS_TARGET;
        }
        audio_virtual_buf_open(&aml_out->virtual_buf_handle
            , "ms12 main input"
            , buf_ns_begin
            , buf_ns_target
            , 0
            , MS12_MAIN_BUF_INCREASE_TIME_MS);
    }

    if (hal_internal_format == AUDIO_FORMAT_PCM_16_BIT) {
        sample_rate = DDP_OUTPUT_SAMPLE_RATE;
    }

#ifdef ENABLE_DVB_PATCH
    if (hal_internal_format == AUDIO_FORMAT_AC3 ||
        hal_internal_format == AUDIO_FORMAT_E_AC3 ||
        hal_internal_format == AUDIO_FORMAT_AC4 ||
        hal_internal_format == AUDIO_FORMAT_AAC ||
        hal_internal_format == AUDIO_FORMAT_AAC_LATM) {
        if (dtv_audio_info) {
            aml_dec_config_t *dec_config  = &aml_out->dec_config;
            codec_info.s32AdInput = 1;//dtv_audio_info->dual_decoder_support;
            /*dec_config need default values for dolby decoder to do runtime setting*/
            associate_audio_mixing_enable = dec_config->ad_mixing_enable = dtv_audio_info->associate_audio_mixing_enable;
            mixing_level = dec_config->mixer_level = dtv_audio_info->mixing_level;
            ad_vol = dec_config->advol_level = dtv_audio_info->advol_level;
            media_presentation_id = dtv_audio_info->media_presentation_id;
            dtv_decoder_offset_base = aml_out->hwsync->payload_offset;
            /*for ac4, there is only one input case*/
            if (hal_internal_format == AUDIO_FORMAT_AC4) {
                codec_info.s32AdInput = 0;
            }
       } else {
            codec_info.s32AdInput = 0;
            associate_audio_mixing_enable = 0;
       }
    } else {
        codec_info.s32AdInput = 0;
        associate_audio_mixing_enable = 0;
    }

    ALOGI("+%s() ad enable %d mixing_level %d ad_vol %d optical =0x%x sink =0x%x\n",
        __FUNCTION__, codec_info.s32AdInput, mixing_level, ad_vol, ms12->optical_format, ms12->sink_format);

#endif

    if (hal_internal_format == AUDIO_FORMAT_AC4) {
        set_ms12_ac4_presentation_group_index(stream, media_presentation_id);
    }

    if (patch && patch->input_src == AUDIO_DEVICE_IN_HDMI) {
        if ((hal_internal_format == AUDIO_FORMAT_AC3) || (hal_internal_format == AUDIO_FORMAT_E_AC3)) {
            codec_info.s32EnforceTimeslice = 1;
            ALOGI("hdmi in ddp/dd case, use enforce timeslice");
        }
    }

    if (hal_internal_format == AUDIO_FORMAT_AAC || hal_internal_format == AUDIO_FORMAT_AAC_LATM) {
        dolby_ms12_set_heaac_default_dialnorm_value(adev->loudness_level);
    }

    if ((adev->dolby_ms12_dap_init_mode == 1) && (adev->board_config.dolby_ms12_audio_config == MS12_CONFIG_X)) {
        codec_info.s32DapContProc = 1;
    }

    ms12->dap_pcm_frames = 0;
    ms12->stereo_pcm_frames = 0;
    ms12->measure_last_frame_us = 0;
    ms12->measure_new_frame_us = 0;
    codec_info.s32HeaacAribMode = 0;
    codec_info.s32RestrictedAd = 0;

    aml_ms12_main_decoder_open(ms12, aml_out->ms12_dec_handle, &codec_info);

    if (hal_internal_format == AUDIO_FORMAT_AC4) {
        AM_LOGI("%s() ac4_de=%d lang=%s lang2=%s at=%d pat=%d\n",__func__, adev->ms12.ac4_de,  adev->ms12.lang, adev->ms12.lang2, adev->ms12.at, adev->ms12.pat);
        AM_LOGI("%s() media_presentation_id=%d\n",__func__, media_presentation_id);
        set_ms12_ac4_dialogue_enhancement(stream, adev->ms12.ac4_de);
        set_ms12_ac4_1st_preferred_language_code(stream, adev->ms12.lang);
        set_ms12_ac4_2nd_preferred_language_code(stream, adev->ms12.lang2);
        if ((adev->ms12.at) >= 1 && (adev->ms12.at <= 3)) {
            set_ms12_ac4_preferred_associated_type(stream, adev->ms12.at);
        }
        set_ms12_ac4_prefer_presentation_selection_by_associated_type_over_language(stream, adev->ms12.pat);
        set_ms12_ac4_presentation_group_index(stream, media_presentation_id);
    }

    set_start_threshold_for_ms12(aml_out);

    set_ms12_content_volume_leveler(stream, adev->ms12.dap_leveler);
    set_ms12_content_dialogue_enhancer(stream, adev->ms12.dap_dialogue_enhancer);

    aml_out->ms12_dec_handle->ms12_main_consume_bytes = 0;
    set_ms12_ad_mixing_enable(stream, associate_audio_mixing_enable);
    set_ms12_ad_mixing_level(stream, mixing_level);
    set_ms12_ad_vol(&aml_out->stream, ad_vol);
#ifdef ENABLE_DVB_PATCH
    //this command effect the DRC process in MS12-pcmr, not the decoder process.
    bool is_aac = (hal_internal_format == AUDIO_FORMAT_AAC || hal_internal_format == AUDIO_FORMAT_AAC_LATM);
    adev->is_dtg_case = is_locale_at_United_Kingdom_device();
    set_ms12_is_dtg_case(ms12, is_aac ? adev->is_dtg_case : 0);

    if (dtv_stream_flag) {
        aml_ms12_decoder_register_callback(ms12, aml_out->ms12_dec_handle, MS12_CODEC_CALLBACK_SYNC, ms12_dtv_sync_callback, (void *)stream);
        aml_out->b_install_sync_callback = true;
        ALOGI("%s set dtv sync callback %p", __func__, stream);
    } else
#endif
    {
        aml_ms12_decoder_register_callback(ms12, aml_out->ms12_dec_handle, MS12_CODEC_CALLBACK_SYNC, ms12_sync_callback, (void *)stream);
        aml_out->b_install_sync_callback = true;
    }


    if (hal_internal_format == AUDIO_FORMAT_AAC || hal_internal_format == AUDIO_FORMAT_AAC_LATM) {
        //dolby_ms12_set_heaac_default_dialnorm_value(adev->loudness_level);
    }

#ifdef ENABLE_DVB_PATCH
    if (dtv_stream_flag) {
        set_dolby_ms12_main_speed(stream, 1.0f);
        aml_ms12_decoder_register_callback(ms12, aml_out->ms12_dec_handle, MS12_CODEC_CALLBACK_TEMPO, ms12_tempo_callback, (void *)stream);
        if (aml_out->output_speed != 1.0) {
            set_dolby_ms12_main_speed(stream, (double)aml_out->output_speed);
            ALOGI("%s(), aml_out->output_speed %f", __FUNCTION__,aml_out->output_speed);
        }
    } else
#endif
    {
        aml_ms12_decoder_register_callback(ms12, aml_out->ms12_dec_handle, MS12_CODEC_CALLBACK_TEMPO, ms12_tempo_callback, (void *)stream);
    }

    aml_ms12_decoder_register_callback(ms12, aml_out->ms12_dec_handle, MS12_CODEC_CALLBACK_PROCESS, ms12_process_callback, (void *)stream);

    if (aml_out->is_netflix_src_stream) {
        set_ms12_decoder_sleep_time(stream, 2000);
    }

    if (is_asdk_test) {
        aml_volume_shaper_set_delay_frames(&aml_out->volume_shaper, 96 * 48);
    } else {
        aml_volume_shaper_set_delay_frames(&aml_out->volume_shaper, 64 * 48);
    }
    aml_volume_shaper_enable_debug(&aml_out->volume_shaper, get_debug_value(AML_DEBUG_AUDIOHAL_VOLUME_SHAPER) != 0 ? true : false);

    aml_ac3_parser_open(&ms12_dec->ac3_parser_handle);
    aml_ac3_parser_open(&ms12_dec->info_ac3_parser_handle);
    aml_truehd_parser_open(&ms12_dec->truehd_parser_handle);
    aml_spdif_decoder_open(&ms12_dec->spdif_dec_handle);
    aml_spdif_decoder_open(&ms12_dec->info_spdif_dec_handle);
    aml_ms12_bypass_open(&ms12_dec->ms12_bypass_handle);

    adev->focus_ms12_stream = aml_out;

    if (is_iec61937_format(stream)) {
        if (ms12_dec->spdif_dec_handle) {
            aml_spdif_decoder_reset(ms12_dec->spdif_dec_handle);
        }
    }

    if (ms12->dolby_ms12_enable) {
        bool is_dtv_mp2or3 = is_same_patch_src(adev, SRC_DTV) && patch && is_mpeg_lay2or3_audio(aml_out->hal_internal_format);
        audio_format_t drc_hal_internal_format  = (is_dtv_mp2or3 == false) ? hal_internal_format : aml_out->hal_internal_format;

        set_ms12_drc_params_for_stereo_and_dap_multi_pcm_output(
            adev
            , ms12
            , drc_hal_internal_format);

        // fix case : main audio decoder is paused
        dolby_ms12_main_resume(stream);
    }

    return 0;
}

int dolby_ms12_main_close(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;

    pthread_mutex_lock(&ms12_dec->main_lock);
    /*after the mutex, we need check whether it is released*/
    if (aml_out->is_ms12_main_decoder) {

        if (aml_out->virtual_buf_handle) {
            audio_virtual_buf_close(&aml_out->virtual_buf_handle);
        }

        if (aml_out->b_install_sync_callback) {
            aml_ms12_decoder_unregister_callback(ms12, aml_out->ms12_dec_handle, MS12_CODEC_CALLBACK_SYNC);
            ALOGI("%s set sync callback NULL", __func__);
        }

        aml_ms12_decoder_unregister_callback(ms12, aml_out->ms12_dec_handle, MS12_CODEC_CALLBACK_TEMPO);

        aml_truehd_parser_close(ms12_dec->truehd_parser_handle);
        ms12_dec->truehd_parser_handle = NULL;

        aml_ac3_parser_close(ms12_dec->ac3_parser_handle);
        ms12_dec->ac3_parser_handle = NULL;
        aml_spdif_decoder_close(ms12_dec->spdif_dec_handle);
        ms12_dec->spdif_dec_handle = NULL;

        aml_ac3_parser_close(ms12_dec->info_ac3_parser_handle);
        ms12_dec->info_ac3_parser_handle = NULL;
        aml_spdif_decoder_close(ms12_dec->info_spdif_dec_handle);
        ms12_dec->info_spdif_dec_handle = NULL;

        aml_ms12_bypass_close(ms12_dec->ms12_bypass_handle);
        ms12_dec->ms12_bypass_handle = NULL;
        ms12_dec->is_bypass_ms12 = false;


        aml_ms12_main_decoder_close(ms12, aml_out->ms12_dec_handle);
        //set_ms12_main_audio_mute(ms12, false, 0);
        ms12_dec->mat_stream_profile = 0;
        ms12_dec->is_bypass_ms12 = false;

        if (adev->focus_ms12_stream == aml_out) {
            adev->focus_ms12_stream = NULL;
        }

        /*the main stream is closed, we should update the sink format now*/
        if (adev->active_outputs[STREAM_PCM_NORMAL] && (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS) && !get_dev_patch(adev)) {
            get_sink_format(&adev->active_outputs[STREAM_PCM_NORMAL]->stream);
        }

        if (ms12->dolby_ms12_enable) {
            set_ms12_drc_params_for_stereo_and_dap_multi_pcm_output(
                adev
                , ms12
                , AUDIO_FORMAT_PCM_16_BIT //treat as PCM format when stream is end.
                );
        }
        /*all ms12 resource is released, set the flag to false*/
        aml_out->is_ms12_main_decoder = false;
    }

    pthread_mutex_unlock(&ms12_dec->main_lock);

    return 0;
}

int dolby_ms12_main_pause(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    int ms12_runtime_update_ret = 0;

    pthread_mutex_lock(&ms12_dec->main_lock);
    ms12_runtime_update_ret = aml_ms12_decoder_pause(ms12, aml_out->ms12_dec_handle);
    ms12_dec->is_paused = true;

    ALOGV("%s  ms12_runtime_update_ret:%d", __func__, ms12_runtime_update_ret);

    //1.audio easing duration is 32ms,
    //2.one loop for schedule_run cost about 32ms(contains the hardware costing),
    //3.if [pause, flush] too short, means it need more time to do audio easing
    //so, the delay time for 32ms(pause is completed after audio easing is done) is enough.
    if (!aml_out->is_callback_pending) {
        aml_audio_sleep(32000);
        ALOGI("%s  sleep 32ms finished", __func__);
    }
    ALOGI("%s stream %p, callback_pending %d", __func__, aml_out, aml_out->is_callback_pending);

    if (aml_out->hw_sync_mode && aml_out->tsync_status != TSYNC_STATUS_PAUSED && aml_out->hwsync) {
        ALOGI("%s end of frame =%d", __func__, aml_out->hwsync->end_of_hwsync_frame);
        /*if we are end of frame now, we don't need to pause pcr*/
        if (!aml_out->hwsync->end_of_hwsync_frame) {
            aml_hwsync_wrap_set_pause(aml_out->hwsync);
            aml_out->tsync_status = TSYNC_STATUS_PAUSED;
        }

        aml_out->hwsync->first_apts_flag = false;
        aml_out->hwsync->wait_video_done = false;
        // prepare for the next wait_video_drop function
        if (aml_out->restore_vmaster) {
            aml_out->restore_vmaster = false;
            aml_hwsync_wrap_set_amaster(aml_out->hwsync, false);
        }


        ALOGD("%s tsync pause finished", __func__);
    }
    pthread_mutex_unlock(&ms12_dec->main_lock);

    return 0;
}

int dolby_ms12_main_resume(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    int ms12_runtime_update_ret = 0;

    /*fly audio of NTS appear freeze ~1.5s fail, as send the resume
    **message to ms12 in flush/close_stream interface when exit stream.
    **here do tsync resume, this lead to video pcr not pause.
    **so add ms12_resume_state to distinguish resume/flush/close resume message.
    **In addition, just only do tsync resume from resume interface message.
    */
    /*coverity[missing_lock]*/
    if (aml_out->hw_sync_mode
        && (ms12_dec->resume_state == MS12_RESUME_FROM_RESUME)) {
        aml_hwsync_wrap_set_resume(aml_out->hwsync);
        aml_out->tsync_status = TSYNC_STATUS_RUNNING;
        ALOGV("%s(), tsync resume finished", __func__);
    }
    dolby_ms12_set_pause_flag(false);
    ms12_runtime_update_ret = aml_ms12_decoder_resume(ms12, aml_out->ms12_dec_handle);
    ms12_dec->is_paused = false;
    ALOGI("%s  ms12_runtime_update_ret:%d", __func__, ms12_runtime_update_ret);
    return 0;
}

int dolby_ms12_main_flush(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    aml_out->main_input_ns = 0;
    /*coverity[missing_lock]*/
    aml_out->ms12_dec_handle->ms12_main_consume_bytes = 0;

    pthread_mutex_lock(&ms12_dec->main_lock);

    aml_out->main_input_ns = 0;

    aml_ms12_decoder_flush(ms12, ms12_dec);

    if (ms12_dec->spdif_dec_handle) {
        aml_spdif_decoder_reset(ms12_dec->spdif_dec_handle);
    }

    if (ms12_dec->ac3_parser_handle) {
        aml_ac3_parser_reset(ms12_dec->ac3_parser_handle);
    }
    if (ms12_dec->ms12_bypass_handle) {
        aml_ms12_bypass_reset(ms12_dec->ms12_bypass_handle);
    }
    ms12_dec->last_post_buffer_frame = 0;

    pthread_mutex_unlock(&ms12_dec->main_lock);
    ALOGI("%s exit", __func__);
    return 0;
}

void set_ms12_mat_enforce_single_oa_element(struct dolby_ms12_desc *ms12, bool enforce_single_oa_element)
{
    char parm[64] = "";
    snprintf(parm, sizeof(parm), "%s %d", "-enforce_single_oa_element", enforce_single_oa_element);

    if ((strlen(parm)) > 0 && ms12)
        aml_ms12_update_runtime_params(ms12, parm);
}


/* This API only changes the encoder graph's config, but now we only have ms12->output_config,
 * so when change the output_config, we need keep the continuous graph's config
 */
int dolby_ms12_encoder_reconfig(struct dolby_ms12_desc *ms12) {
    struct aml_audio_device *adev = NULL;
    int output_config = MS12_OUTPUT_MASK_STEREO;
    bool current_mat_encoder_enable = false;
    bool current_ddp_encoder_enable = false;
    bool current_dd_encoder_enable  = false;
    bool b_reset = 0;
    bool b_encoder_enable = false;
    struct aml_arc_hdmi_desc* hdmi_descs = NULL;

    ALOGI("+%s()", __FUNCTION__);
    if (!ms12) {
        return -EINVAL;
    }

    pthread_mutex_lock(&ms12->lock);
    current_mat_encoder_enable = ms12->output_config & MS12_OUTPUT_MASK_MAT;
    current_ddp_encoder_enable = ms12->output_config & MS12_OUTPUT_MASK_DDP;
    current_dd_encoder_enable  = ms12->output_config & MS12_OUTPUT_MASK_DD;
    b_encoder_enable = current_mat_encoder_enable | current_ddp_encoder_enable | current_dd_encoder_enable;
    adev = ms12_to_adev(ms12);
    hdmi_descs = get_arc_hdmi_cap(adev);

    /*pcm only output, disable encoder*/
    if (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_PCM) {
        if (adev->sink_format == AUDIO_FORMAT_PCM_16_BIT &&
            b_encoder_enable != 0) {
            /*only enable the pcm output*/
            output_config = MS12_OUTPUT_MASK_STEREO | MS12_OUTPUT_MASK_SPEAKER;
            b_reset = 1;
        }
    } else {
        if (adev->sink_capability == AUDIO_FORMAT_MAT) {
            output_config = MS12_OUTPUT_MASK_STEREO | MS12_OUTPUT_MASK_MAT;
            if (!current_mat_encoder_enable) {
                b_reset = 1;
            }
        } else if (adev->sink_capability == AUDIO_FORMAT_E_AC3 || adev->sink_capability == AUDIO_FORMAT_DOLBY_TRUEHD) {
            /*for sink only support truehd, it can't support MAT, so need to convert DDP*/
            output_config = MS12_OUTPUT_MASK_DDP | MS12_OUTPUT_MASK_STEREO | MS12_OUTPUT_MASK_SPEAKER;
            if (!current_ddp_encoder_enable) {
                b_reset = 1;
            }
            /*dual spdif for ott case, optical_format == AUDIO_FORMAT_AC3 for tv case*/
            if (adev->dual_spdif_support || adev->optical_format == AUDIO_FORMAT_AC3) {
                output_config = output_config | MS12_OUTPUT_MASK_DD;
                if (!current_dd_encoder_enable) {
                    b_reset = 1;
                }
            }
        } else if (adev->sink_capability == AUDIO_FORMAT_AC3) {
            output_config = MS12_OUTPUT_MASK_DD | MS12_OUTPUT_MASK_STEREO | MS12_OUTPUT_MASK_SPEAKER;
            if (!current_dd_encoder_enable || current_ddp_encoder_enable) {
                b_reset = 1;
            }
        } else if (adev->sink_capability == AUDIO_FORMAT_PCM_16_BIT) {
            output_config = MS12_OUTPUT_MASK_STEREO | MS12_OUTPUT_MASK_SPEAKER;
            if (current_ddp_encoder_enable || current_mat_encoder_enable) {
                b_reset = 1;
            }
            if (adev->optical_format == AUDIO_FORMAT_AC3) {
                output_config = output_config | MS12_OUTPUT_MASK_DD;
                if (!current_dd_encoder_enable) {
                    b_reset = 1;
                }
            }
        }
    }

    if (adev->is_netflix && adev->aaudio_low_latency) {
        // LLP only request pcm, turn off encoder to reduce cpu loading.
        output_config = MS12_OUTPUT_MASK_STEREO;
        b_reset = 1;
    }

    /*only enable mc output when it supports multi channel*/
    // For netflix apk, DDP/MAT and mc-pcm will not exist at the same time.
    if ((hdmi_descs->pcm_fmt.max_channels >= 6 || is_earc_connected(adev))
        && !(output_config & (MS12_OUTPUT_MASK_MAT|MS12_OUTPUT_MASK_DDP))) {
        output_config |= MS12_OUTPUT_MASK_MC;
        set_ms12_mch_enable(ms12, true);
    } else {
        set_ms12_mch_enable(ms12, false);
    }

    /*IIDK v281 cmd -enforce_single_oa_element <int> 0|1 */
    //Enforce single OA element in OAMD for MAT output when
    //connected downstream AVR requires MAT hashing via HDMI SAD signaling.
    //Degrades experience, but ensures interoperability with first generation Atmos capable AVR's (default: 0)*/
    if (output_config & MS12_OUTPUT_MASK_MAT) {
        //set_ms12_mat_enforce_single_oa_element(ms12, hdmi_descs->mat_fmt.enforce_single_oa_element);
    }


    /* SWPL-152241 [legacyDevice] play Dolby_Atmos_ChannelCheck_321_ddp.mp4 Lb/Rb no silent */
    /* dumpsys media.audio_flinger
       Output devices: 0x40000 (AUDIO_DEVICE_OUT_HDMI_ARC)
       cur_out_devices   :    0x40000
       hdmi_descs->ddp_fmt.atmos_supported 0 out_device 0x80002 cur_out_devices 0x40000
       this leads -legacy_ddplus_out ddp5.1 output setting wrong. */

    bool is_atmos_supported = is_platform_supported_ddp_atmos(hdmi_descs->ddp_fmt.atmos_supported, adev->cur_out_devices, is_TV(adev));
    if (dolby_ms12_get_ddp_5_1_out() != !is_atmos_supported) {
        set_ms12_out_ddp_5_1(AUDIO_FORMAT_E_AC3, is_atmos_supported);
        b_reset = 1;
    }

    if (adev->dolby_ms12_dap_init_mode) {
        output_config |= MS12_OUTPUT_MASK_DAP;
    }
    audio_continuous_standby_set(ms12->continuous_standby_handle, STANDBY_SET_OUTPUT_PORT, output_config);

    if (b_reset) {
        if (is_TV(adev) && (output_config & MS12_OUTPUT_MASK_DDP)) {
            // reduce ddp encoder latency (phase 90 shifted : disable)
            dolby_ms12_set_hdmi_output_type(HDMI_ARC_OUTPUT);
        } else {
            dolby_ms12_set_hdmi_output_type(FULL_HDMI_OUTPUT);
        }

        ms12->optical_format = adev->optical_format;
        ms12->sink_format    = adev->sink_format;

        /*keep the original speaker config in encoder reconfig*/
        if (ms12->output_config & MS12_OUTPUT_MASK_SPEAKER) {
            output_config |= MS12_OUTPUT_MASK_SPEAKER;
        }
        ALOGI("%s new out config =0x%x", __func__, output_config);

        aml_ms12_main_encoder_reconfig(ms12, output_config);
        ms12->b_encoder_reset = true;
    }
    pthread_mutex_unlock(&ms12->lock);


    return 0;
}

void dolby_ms12_app_flush()
{
    dolby_ms12_flush_app_input_buffer();
}

void dolby_ms12_enable_debug()
{
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int level = 0;
    int ret = -1;

    ret = property_get("vendor.audio.dolbyms12.debug", buf, NULL);
    if (ret > 0)
    {
        level = strtol (buf, NULL, 0);
        dolby_ms12_set_debug_level(level);
    }
}

bool is_ms12_continuous_mode(struct aml_audio_device *adev)
{
    if ((eDolbyMS12Lib == adev->dolby_lib_type) && (adev->continuous_audio_mode)) {
        return true;
    } else {
        return false;
    }
}

bool is_dolby_ms12_main_stream(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    audio_format_t hal_internal_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);
    bool is_bitstream_stream = !audio_is_linear_pcm(hal_internal_format);
    bool is_hwsync_pcm_stream = (audio_is_linear_pcm(hal_internal_format) && (aml_out->flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC));
    if (is_bitstream_stream || is_hwsync_pcm_stream) {
        return true;
    } else {
        return false;
    }
}

bool is_support_ms12_reset(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct aml_arc_hdmi_desc * hdmi_descs = get_arc_hdmi_cap(adev);
    bool is_atmos_supported = is_platform_supported_ddp_atmos(hdmi_descs->ddp_fmt.atmos_supported, adev->cur_out_devices, is_TV(adev));
    bool need_reset_ms12_out = !is_ms12_out_ddp_5_1_suitable(is_atmos_supported);
    bool is_pcm_mode = (adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_PCM);
    /* we meet 3 conditions:
     * 1. edid atmos support not match with currently ms12 output
     * 2. it is the main stream
     * 3. it has write some data
     */
    if (is_dolby_ms12_main_stream(stream) && need_reset_ms12_out && !is_pcm_mode) {
        return true;
    }

    return false;
}

/*
 *The audio data(direct/offload/hwsync) should bypass Dolby MS12,
 *if Audio Mixing is Off, and (Sink & Output) format are both EAC3,
 *specially, the dual decoder is false and continuous audio mode is false.
 *because Dolby MS12 is working at LiveTV+(Dual Decoder) or Continuous Mode.
 */
bool is_bypass_dolbyms12(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    audio_format_t hal_internal_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);
    bool is_dts = is_dts_format(hal_internal_format);
    bool is_dolby_audio = is_dolby_format(hal_internal_format);
    bool is_mpegh = is_mpegh_format(hal_internal_format);

    return (is_dts
            || is_mpegh
            || (is_iec61937_format(stream) && !aml_out->is_tv_src_stream)
            || is_high_rate_pcm(stream)
            || (is_multi_channel_pcm(stream) && (aml_out->current_digital_audio_format == AML_DIGITAL_AUDIO_MODE_BYPASS)));
}

bool is_audio_postprocessing_add_dolbyms12_dap(struct aml_audio_device *adev)
{
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    bool is_dap_enable = ((adev->cur_out_devices & AUDIO_DEVICE_OUT_SPEAKER) != 0) && (!adev->ms12.dap_bypass_enable);
    //ALOGI("%s cur_out_devices %#x SPEAKER %#x dap_bypass_enable %d is_dap_enable %d is_ui_force_dap_disable %d!",
    //    __func__, adev->cur_out_devices, AUDIO_DEVICE_OUT_SPEAKER,  adev->ms12.dap_bypass_enable, is_dap_enable, adev->is_ui_force_dap_disable);

    if (adev->is_ui_force_dap_disable == true) {
        is_dap_enable =  false;
        ALOGV("DAPV2.4 debug ui is off that make dap disable");
    }
    else {
        /* Dolby MS12 V2 uses DAP Tuning file */
        if (adev->is_ms12_tuning_dat) {
            //ALOGI("%s dolby_ms12_enable %d is_dap_enable %d output_config & MS12_OUTPUT_MASK_SPEAKER %#x",
            //    __func__, ms12->dolby_ms12_enable, is_dap_enable, ms12->output_config & MS12_OUTPUT_MASK_SPEAKER );
            if (ms12->dolby_ms12_enable && is_dap_enable && (ms12->output_config & MS12_OUTPUT_MASK_SPEAKER)) {
                is_dap_enable =  true;
            }
            else {
                is_dap_enable =  false;
            }
        }
        else {
            is_dap_enable =  false;
        }
    }

    if (is_SBR(adev) && (adev->enable_soundbar_mode == 0)) {
        is_dap_enable =  false;
    }
    //ALOGI("%s is_SBR %d is_SBR_active %d is_dap_enable %d!", __func__, is_SBR(adev), is_SBR_active(adev), is_dap_enable);

    return is_dap_enable;
}

bool is_dolbyms12_dap_enable(struct aml_stream_out *aml_out) {
    struct aml_audio_device *adev = aml_out->dev;

#if 0
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    bool is_dap_enable = (adev->active_outport == OUTPORT_SPEAKER) && (!adev->ms12.dap_bypass_enable);
    is_dap_enable = (ms12->dolby_ms12_enable && is_dap_enable && (ms12->output_config & MS12_OUTPUT_MASK_SPEAKER)) ? true : false;
    return is_dap_enable;
#else
    return is_audio_postprocessing_add_dolbyms12_dap(adev);
#endif
}

bool get_ms12_dap_virtual_bass_enable(void) {
    return dolby_ms12_get_dap2_virtual_bass_enable();
}


int dolby_ms12_output_insert_oneframe(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = 0;
    char *mute_pcm_buffer = 0;
    char *mute_raw_buffer = 0;
    int  pcm_buffer_size = MS12_PCM_FRAME_SIZE;
    int  raw_buffer_size = MS12_DDP_FRAME_SIZE;
    size_t output_buffer_bytes = 0;
    audio_data_info_t data_info = { 0 };
    size_t raw_size = 0;
    audio_format_t output_format = AUDIO_FORMAT_PCM_16_BIT;

    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
    bool b_raw_out = false;

    mute_pcm_buffer =  aml_audio_calloc(1, pcm_buffer_size);
    mute_raw_buffer =  aml_audio_calloc(1, raw_buffer_size);

    if (mute_pcm_buffer == NULL ||
        mute_raw_buffer == NULL) {
        ret = -1;
        goto exit;
    }

    if (ms12->optical_format == AUDIO_FORMAT_AC3 || ms12->optical_format == AUDIO_FORMAT_E_AC3) {
        output_format = ms12->optical_format;
        b_raw_out = true;
        if (output_format == AUDIO_FORMAT_AC3) {
            raw_size = sizeof(ms12_muted_dd_raw);
            memcpy(mute_raw_buffer, ms12_muted_dd_raw, raw_size);
        } else {
            raw_size = sizeof(ms12_muted_ddp_raw);
            memcpy(mute_raw_buffer, ms12_muted_ddp_raw, raw_size);
        }
    }


    data_info.audio_format = AUDIO_FORMAT_PCM_16_BIT;
    data_info.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    ret = aml_audio_pcm_output((struct audio_stream_out *)aml_out, mute_pcm_buffer, pcm_buffer_size, &data_info);


    /*insert raw data*/
    if (b_raw_out) {
        ret = aml_ms12_spdif_output_new(stream, bitstream_out, output_format, output_format, DDP_OUTPUT_SAMPLE_RATE, 2, AUDIO_CHANNEL_OUT_STEREO, mute_raw_buffer, raw_size);
    }

exit:
    if (mute_pcm_buffer) {
        aml_audio_free(mute_pcm_buffer);
    }
    if (mute_raw_buffer) {
        aml_audio_free(mute_raw_buffer);
    }

    return ret;
}

uint64_t dolby_ms12_get_main_bytes_consumed(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    uint64_t main_bytes_consumed = 0;
    aml_ms12_decoder_getparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_MAIN_CONSUMED, &main_bytes_consumed, sizeof(uint64_t));
    if (adev->debug_flag > 1) {
        ALOGI("%s consumed =%" PRId64,
            __func__, main_bytes_consumed);
    }
    return main_bytes_consumed;
}

uint64_t dolby_ms12_get_main_pcm_generated(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    uint64_t pcm_frame_generated = 0;
    uint64_t main_input_offset_frame = 0;
    int latency_frames = 0;

    pcm_frame_generated = dolby_ms12_get_continuous_nframes_pcm_output(ms12->dolby_ms12_ptr, MAIN_INPUT_STREAM);

    if (adev->debug_flag) {
        ALOGI("%s main offset =%" PRId64 " pcm_frame_generated=%" PRId64 " total =%" PRId64 "", __func__, main_input_offset_frame, pcm_frame_generated, (main_input_offset_frame + pcm_frame_generated));
    }
    return (main_input_offset_frame + pcm_frame_generated);
}

bool is_rebuild_the_ms12_pipeline(    audio_format_t main_input_fmt, audio_format_t hal_internal_format)
{
    ALOGD("%s line %d main_input_fmt %#x hal_internal_format %#x\n",__func__, __LINE__, main_input_fmt, hal_internal_format);

    bool is_ac4_alive = (main_input_fmt == AUDIO_FORMAT_AC4);
    bool is_mat_alive = (main_input_fmt == AUDIO_FORMAT_MAT);
    bool is_aac_alive = ((main_input_fmt == AUDIO_FORMAT_AAC) || \
                        (main_input_fmt == AUDIO_FORMAT_HE_AAC_V1) || \
                        (main_input_fmt == AUDIO_FORMAT_HE_AAC_V2) || \
                        (main_input_fmt == AUDIO_FORMAT_AAC_LATM));
    bool is_ott_format_alive = (main_input_fmt == AUDIO_FORMAT_AC3) || \
                                ((main_input_fmt & AUDIO_FORMAT_E_AC3) == AUDIO_FORMAT_E_AC3) || \
                                (main_input_fmt == AUDIO_FORMAT_PCM_16_BIT);
    ALOGD("%s line %d is_ac4_alive %d is_mat_alive %d is_aac_alive %d is_ott_format_alive %d\n",
        __func__, __LINE__, is_ac4_alive, is_mat_alive, is_aac_alive, is_ott_format_alive);

    bool request_ac4_alive = (hal_internal_format == AUDIO_FORMAT_AC4);
    /* if switch from MAT to Dolby-TrueHD, need to confirm it works well. */
    bool request_mat_alive = ((hal_internal_format == AUDIO_FORMAT_MAT) || (hal_internal_format == AUDIO_FORMAT_DOLBY_TRUEHD));
    bool request_aac_alive = ((hal_internal_format == AUDIO_FORMAT_AAC) || \
                        (hal_internal_format == AUDIO_FORMAT_HE_AAC_V1) || \
                        (hal_internal_format == AUDIO_FORMAT_HE_AAC_V2) || \
                        (hal_internal_format == AUDIO_FORMAT_AAC_LATM));
    bool request_ott_format_alive = (hal_internal_format == AUDIO_FORMAT_AC3) || \
                                ((hal_internal_format & AUDIO_FORMAT_E_AC3) == AUDIO_FORMAT_E_AC3) || \
                                (hal_internal_format == AUDIO_FORMAT_PCM_16_BIT);
    ALOGD("%s line %d request_ac4_alive %d request_mat_alive %d request_aac_alive %d request_ott_format_alive %d\n",
        __func__, __LINE__, request_ac4_alive, request_mat_alive, request_aac_alive, request_ott_format_alive);

    if (request_ac4_alive && (is_ac4_alive^request_ac4_alive)) {
        //new AC4 stream appears when last stream played MAT/DD/DDP/AAC
        ALOGD("%s line %d main_input_fmt %#x hal_internal_format %#x request_ac4_alive^is_mat_alive %d request_ac4_alive^is_ott_format_alive %d (request_ac4_alive^is_aac_alive) %d\n",
            __func__, __LINE__, main_input_fmt, hal_internal_format,
            request_ac4_alive^is_mat_alive, request_ac4_alive^is_ott_format_alive, request_ac4_alive^is_aac_alive);
        /*coverity[dead_error_line]*/
        return (request_ac4_alive^is_mat_alive) || (request_ac4_alive^is_ott_format_alive) || (request_ac4_alive^is_aac_alive);
    }
    else if (request_mat_alive && (is_mat_alive^request_mat_alive)) {
        //new MAT stream appears when last steam played AC4/DD/DDP/AAC
        ALOGD("%s line %d main_input_fmt %#x hal_internal_format %#x (request_mat_alive^is_ac4_alive) %d (request_mat_alive^is_ott_format_alive) %d (request_mat_alive^is_aac_alive) %d\n",
            __func__, __LINE__, main_input_fmt, hal_internal_format, (request_mat_alive^is_ac4_alive), (request_mat_alive^is_ott_format_alive), (request_mat_alive^is_aac_alive));
        /*coverity[dead_error_line]*/
        return (request_mat_alive^is_ac4_alive) || (request_mat_alive^is_ott_format_alive) || (request_mat_alive^is_aac_alive);
    }
    else if (request_aac_alive && (is_aac_alive^request_aac_alive)) {
        //new aac stream appears when last steam played AC4/DD/DDP/MAT
        ALOGD("%s line %d main_input_fmt %#x hal_internal_format %#x (request_aac_alive^is_ac4_alive) %d (request_aac_alive^is_ott_format_alive) %d (request_aac_alive^is_mat_alive) %d\n",
            __func__, __LINE__, main_input_fmt, hal_internal_format,
            (request_aac_alive^is_ac4_alive), (request_aac_alive^is_ott_format_alive), (request_aac_alive^is_mat_alive));
        /*coverity[dead_error_line]*/
        return (request_aac_alive^is_ac4_alive) || (request_aac_alive^is_ott_format_alive) || (request_aac_alive^is_mat_alive);
    }
    else if (request_ott_format_alive && (is_ott_format_alive^request_ott_format_alive)){
        //new ott(dd/ddp/ddp_joc) format appears when last stream played AC4/MAT/AAC
        ALOGD("%s line %d main_input_fmt %#x hal_internal_format %#x (request_ott_format_alive^is_ac4_alive) %d (request_ott_format_alive^is_mat_alive) %d (request_ott_format_alive^is_aac_alive) %d\n",
            __func__, __LINE__, main_input_fmt, hal_internal_format,
            (request_ott_format_alive^is_ac4_alive), (request_ott_format_alive^is_mat_alive), (request_ott_format_alive^is_aac_alive));
        /*coverity[dead_error_line]*/
        return (request_ott_format_alive^is_ac4_alive) || (request_ott_format_alive^is_mat_alive) || (request_ott_format_alive^is_aac_alive);
    }
    else {
        ALOGE("%s line %d main_input_fmt %#x hal_internal_format %#x return false\n",
            __func__, __LINE__, main_input_fmt, hal_internal_format);
        return false;
    }
}

bool is_need_reset_ms12_continuous(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    return false;
}

bool is_ms12_output_compatible(struct audio_stream_out *stream, audio_format_t new_sink_format, audio_format_t new_optical_format) {
    bool is_compatible = false;
    int  output_config = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);

    if ((aml_out->hal_internal_format != AUDIO_FORMAT_AC4 && adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_BYPASS  && !netflix_request_dd_output()) ||
        adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_PCM) {
        /*for bypass case and pcm case, it is always compatible*/
        return true;
    }
    output_config = get_ms12_output_mask(new_sink_format, new_optical_format, false);
    /*The stereo bit does not compare*/
    is_compatible = ((ms12->output_config & ~MS12_OUTPUT_MASK_STEREO) & (output_config & ~MS12_OUTPUT_MASK_STEREO));
    ALOGI("ms12 current out=%#x new output=%#x is_compatible=%d", ms12->output_config, output_config, is_compatible);
    return is_compatible;

}

int dolby_ms12_main_pipeline_latency_frames(struct audio_stream_out *stream) {
    int latency_frames = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    /*udc/tunnel pcm decoded frames */
    uint64_t decoded_frame = 0;
    /*ms12 output total frames*/
    uint64_t main_mixer_consume = 0;
    audio_format_t audio_format = AUDIO_FORMAT_DEFAULT;
    audio_format_t hal_internal_format = ms12_get_audio_hal_format(aml_out->hal_internal_format);
    if (aml_out->hwsync && aml_out->hwsync->aout)
        audio_format = aml_out->hwsync->aout->hal_internal_format;
    else {
        audio_format = hal_internal_format;
    }

    /*the decoded pcm frame - mixer consumed frame, it is the delay*/
    aml_ms12_decoder_getparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_MAIN_PCMOUT_FRAME, &decoded_frame, sizeof(uint64_t));

    /*pcm data is resampled before ms12*/
    if (aml_out->hal_rate != 48000 && aml_out->hal_rate != 0 && hal_internal_format != AUDIO_FORMAT_PCM_16_BIT) {
        decoded_frame = decoded_frame * 48000 / aml_out->hal_rate;
    }
    main_mixer_consume = dolby_ms12_get_continuous_nframes_pcm_output(ms12->dolby_ms12_ptr, MAIN_INPUT_STREAM);

    if (decoded_frame >= main_mixer_consume) {
        latency_frames += (decoded_frame - main_mixer_consume);
    } else {
        // when sonic(speed) enable, main_mixer_consume no longer aligns to decoded_frame.
        if (aml_out->speed_info.speed_handle == NULL) {
            ALOGE("wrong ms12 pipe line delay decode =%" PRId64 " mixer =%" PRId64 "", decoded_frame, main_mixer_consume);
        }
    }

    ALOGV("%s decoded_frame = %" PRId64 " main_mixer_consume = %" PRId64 " latency_frames=%d %d ms", __func__, decoded_frame, main_mixer_consume, latency_frames, latency_frames / 48);
    return latency_frames;
}

static int ms12_update_decoded_info_process(struct audio_stream_out *stream
    , void *input_buffer
    , size_t input_bytes
    , int *ddp_1st_frame_size
    , int *ddp_1st_numblks)
{

    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    int32_t temp_spdif_dec_used_size = 0;
    void *main_frames_buffer = input_buffer;
    int main_frames_size = input_bytes;
    int temp_used_size = 0;
    void * temp_main_frame_buffer = NULL;
    int temp_main_frame_size = 0;
    struct ac3_parser_info ac3_info = { 0 };
    uint64_t decoded_frames = 0;
    unsigned int decoded_err = 0;
    int sample_rate = 0;
    int ch_num = 0;

    if ((aml_out->hal_format == AUDIO_FORMAT_AC3) ||
        (aml_out->hal_format == AUDIO_FORMAT_E_AC3) ||
        (aml_out->hal_format == AUDIO_FORMAT_IEC61937)) {

        if (aml_out->hal_format == AUDIO_FORMAT_IEC61937) {
            void * inbuf = NULL;
            int32_t buf_size = 0;
            aml_spdif_decoder_process(ms12_dec->info_spdif_dec_handle, input_buffer, input_bytes, &temp_spdif_dec_used_size, &main_frames_buffer, &main_frames_size);
            if (main_frames_size == 0) {
                return -1;
            }
            inbuf = main_frames_buffer;
            buf_size = main_frames_size;
            aml_ac3_parser_process(ms12_dec->info_ac3_parser_handle, inbuf, buf_size, &temp_used_size, &temp_main_frame_buffer, &temp_main_frame_size, &ac3_info);
        } else {
            aml_ac3_parser_process(ms12_dec->info_ac3_parser_handle, input_buffer, input_bytes, &temp_used_size, &temp_main_frame_buffer, &temp_main_frame_size, &ac3_info);
        }

        *ddp_1st_frame_size = temp_main_frame_size;
        if (ddp_1st_frame_size) {
            *ddp_1st_numblks = ac3_info.numblks;
        }

        if (temp_main_frame_size != 0) {
            aml_out->ddp_frame_nblks = ac3_info.numblks;
            aml_out->total_ddp_frame_nblks += aml_out->ddp_frame_nblks;
            decoded_frames = aml_out->total_ddp_frame_nblks * SAMPLE_NUMS_IN_ONE_BLOCK;
            sample_rate = ac3_info.sample_rate;
            ch_num = ac3_info.channel_num;
            //Fixme: errcount is temporarily unavailable when using MS12
            decoded_err = 0;
            if (get_audio_info_enable(DUMP_AUDIO_INFO_DECODE)) {
                UpdateDecodedInfo_DecodedFrames(decoded_frames);
                UpdateDecodedInfo_DecodedErr(decoded_err);
                UpdateDecodedInfo_SampleRate_ChannelNum_ChannelConfiguration(sample_rate, ch_num);
            }

        }

    } else if (aml_out->hal_format == AUDIO_FORMAT_HE_AAC_V1 ||
        aml_out->hal_format == AUDIO_FORMAT_HE_AAC_V2 ||
        aml_out->hal_format == AUDIO_FORMAT_AAC ||
        aml_out->hal_format == AUDIO_FORMAT_AAC_LATM) {
        decoded_frames = aml_out->last_dec_out_frame;
        //Fixme: errcount is temporarily unavailable when using MS12
        decoded_err = 0;
        ch_num = popcount(ms12->config_channel_mask);
        sample_rate = ms12->config_sample_rate;
        if (get_audio_info_enable(DUMP_AUDIO_INFO_DECODE)) {
            UpdateDecodedInfo_DecodedFrames(decoded_frames);
            UpdateDecodedInfo_DecodedErr(decoded_err);
            UpdateDecodedInfo_SampleRate_ChannelNum_ChannelConfiguration(sample_rate, ch_num);
        }
    }
    return 0;

}

int dolby_ms12_main_resume_prepare(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
#if 0
    //uint64_t ms12_dec_out_nframes = dolby_ms12_get_decoder_nframes_pcm_output(adev->ms12.dolby_ms12_ptr, aml_out->hal_internal_format, MAIN_INPUT_STREAM);
    uint64_t ms12_dec_out_nframes = 0;
    aml_ms12_decoder_getparameter(ms12, aml_out->ms12_dec_handle, MS12_CODEC_PARAMETER_MAIN_PCMOUT_FRAME, &ms12_dec_out_nframes, sizeof(uint64_t));

    ms12->main_output_ns = ms12_dec_out_nframes * 1000000LL / 48;
    /*why we add 1ms
     *because the main_input_ns is not accurate enough, it lost the decimal part
     *so we add 1ms to compensate
     */
    uint64_t main_buffer_duration_ns = (ms12->main_input_ns + NANO_SECOND_PER_MILLISECOND - ms12->main_output_ns);
    ALOGI("%s main in =%" PRId64 " main out =%" PRId64 "", __func__, ms12->main_input_ns, ms12->main_output_ns);
    ALOGI("%s main buffer duration =%d ms main buffer =%d ms", __func__, (int)(main_buffer_duration_ns / 1000000), (int)(MS12_MAIN_INPUT_BUF_NONEPCM_NS / 1000000));
    /* after pause/resume, the virtual buf will begin calcuale from start point,
     * but the buffer is not empty, then it is not match between virtual and real buf,
     * now when resume we check the real ms12 buf duration and reset the virtual buf
     */
    if (main_buffer_duration_ns <= MS12_MAIN_INPUT_BUF_NONEPCM_NS) {
        audio_virtual_buf_reset(aml_out->virtual_buf_handle);
        audio_virtual_buf_process(aml_out->virtual_buf_handle, main_buffer_duration_ns);
    } else {
        audio_virtual_buf_reset(aml_out->virtual_buf_handle);
        audio_virtual_buf_process(aml_out->virtual_buf_handle, MS12_MAIN_INPUT_BUF_NONEPCM_NS);
    }
#endif
    return 0;
}

void set_ms12_set_compressor_profile(struct dolby_ms12_desc *ms12, int profile)
{
    char parm[64] = "";

    sprintf(parm, "%s %d", "-rp", profile);
    if ((strlen(parm)) > 0 && ms12) {
        dolby_ms12_set_pcm_compressor_profile(profile);
        aml_ms12_update_runtime_params(ms12, parm);
    }
}

void set_ms12_alsa_limit_frame(struct dolby_ms12_desc *ms12, int limit_frame)
{
    if (ms12 && limit_frame >= 0) {
        dolby_ms12_set_alsa_limit_frame(limit_frame);
        ms12->alsa_limit_frame = limit_frame;
    }
}

void set_ms12_scheduler_sleep(struct dolby_ms12_desc *ms12, bool enable_sleep)
{
    if (ms12) {
        dolby_ms12_set_scheduler_sleep(enable_sleep);
        ms12->scheduler_sleep_enable = enable_sleep;
    }
}

void set_ms12_set_main_start_threshold(struct audio_stream_out *stream, int start_threshold)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ret = -1;
    if (ms12 && aml_out->ms12_dec_handle) {
        ret = aml_ms12_decoder_setparameter(ms12, aml_out->ms12_dec_handle,
            MS12_CODEC_PARAMETER_MAIN_START_THRESHOLD, &start_threshold, sizeof(start_threshold));
    }
}

/*
 *@brief get dap prepared
 */
int aml_dap_open(
    struct aml_stream_out *aml_out
    , audio_format_t input_format
    , audio_channel_mask_t input_channel_mask
    , int input_sample_rate)
{
    ALOGI("+%s()  aml_out:%p input_format %#x\n", __FUNCTION__, aml_out, input_format);
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    struct aml_stream_out *out;
    int output_config = MS12_OUTPUT_MASK_STEREO;
    unsigned int sink_max_channels = 2;
    int ret = 0;
    ms12->tv_tuning_flag = false;

    ALOGI("\n+%s()", __FUNCTION__);
    pthread_mutex_lock(&ms12->lock);
    ALOGI("++%s(), locked", __FUNCTION__);
    ms12->optical_format = adev->optical_format;
    ms12->sink_format    = adev->sink_format;
    sink_max_channels    = adev->sink_max_channels;

    //input_format = AUDIO_FORMAT_PCM_16_BIT;
    set_audio_main_format(input_format);
    dolby_ms12_set_dap_only(1);
    set_dolby_ms12_continuous_mode(false);

    /*set the dolby ms12 debug level*/
    dolby_ms12_enable_debug();


    /* create  the ms12 output stream here */
    /*************************************/
    out = aml_out;

    ms12->ms12_timer_id = aml_audio_timer_create(ms12_timer_callback_handler);
    ALOGI("func:%s  timer_id:%d", __func__, ms12->ms12_timer_id);

    struct audio_board_config *bd_config = &adev->board_config;
    ms12->dual_bitstream_support = adev->dual_spdif_support;
    output_config = MS12_OUTPUT_MASK_SPEAKER;
    set_dolby_ms12_drc_parameters(input_format, output_config, ms12);

    aml_ms12_config(ms12, input_format, input_channel_mask, input_sample_rate, output_config, get_ms12_path());
    if (ms12->dolby_ms12_enable) {
        //register Dolby MS12 callback
        dolby_ms12_register_output_callback(ms12_output, (void *)out);

        ms12->device = usecase_device_adapter_with_ms12(out->device,AUDIO_FORMAT_PCM_16_BIT);
        ALOGI("%s out [dual_output_flag %d] adev [format sink %#x optical %#x] ms12 [output-format %#x device %d]",
              __FUNCTION__, out->dual_output_flag, adev->sink_format, adev->optical_format, ms12->output_config, ms12->device);
        memcpy((void *) & (adev->ms12_config), (const void *) & (out->config), sizeof(struct pcm_config));
        get_hardware_config_parameters(
            &(adev->ms12_config)
            , AUDIO_FORMAT_PCM_16_BIT
            , bd_config->default_alsa_ch
            , ms12->output_samplerate
            , out->is_tv_platform
            , false
            , is_game_mode(adev));

        //n bytes of downmix output pcm frame, 16bits_per_sample / stereo, it value is 4 bytes.
        ms12->nbytes_of_dmx_output_pcm_frame = nbytes_of_dolby_ms12_downmix_output_pcm_frame();
        ms12->ms12_digital_audio_format = adev->digital_audio_mode;
    }
    ms12->sys_audio_base_pos = adev->sys_audio_frame_written;
    ms12->deep_buf_audio_base_pos = adev->deep_buf_audio_frame_written;
    ms12->sys_audio_skip = 0;
    ms12->dap_pcm_frames = 0;
    ms12->stereo_pcm_frames = 0;
    ms12->master_pcm_frames = 0;
    ms12->ms12_main_input_size = 0;
    ms12->do_easing = false;
    ms12->is_muted = false;
    ms12->b_legacy_ddpout = dolby_ms12_get_ddp_5_1_out();
    ms12->dap_only_enable = true;
    set_ms12_main_volume(ms12, 1.0f);
    ALOGI("%s line %d set ms12 main volume as 1.0\n", __func__, __LINE__);
    ms12->dtv_decoder_offset_base = 0;
    ALOGI("set ms12 sys pos =%" PRId64 "", ms12->sys_audio_base_pos);

    ret = ring_buffer_init(&ms12->spdif_ring_buffer, ms12->dolby_ms12_out_max_size);
    if (ret != 0) {
        ALOGW("[%s:%d] init is error", __func__, __LINE__);
        pthread_mutex_unlock(&ms12->lock);
        return -1;
    }
    ms12->dolby_ms12_init_flags = true;
    adev->doing_reinit_ms12 = false;
    ms12->debug_synced_frame_pts_flag = get_debug_value(AML_DEBUG_AUDIOHAL_SYNCPTS);
    adev->ms12.dolby_ms12_enable = false;
    set_ms12_full_dap_disable(ms12, false);

    ALOGI("--%s(), locked", __FUNCTION__);
    pthread_mutex_unlock(&ms12->lock);

    ALOGI("-%s()\n\n", __FUNCTION__);

    return ret;
}

/*
 *@brief get dolby ms12 cleanup
 */
int aml_dap_close(struct dolby_ms12_desc *ms12)
{
    int is_quit = 1;
    int i = 0;
    struct aml_audio_device *adev = NULL;
    unsigned int remaining_time = 0;
    ALOGI("+%s()", __FUNCTION__);
    if (!ms12) {
        ALOGI("-%s()  exit.", __FUNCTION__);
        return -EINVAL;
    }
    adev = ms12_to_adev(ms12);
    pthread_mutex_lock(&ms12->lock);

    if (!ms12->dap_only_enable) {
        ALOGI("dap is not init, don't need cleanup");
        goto exit;
    }

    ALOGI("++%s(), locked", __FUNCTION__);

    /* check timers is running or not,
    ** timer should be stopped if running.
    **/
    remaining_time = audio_timer_remaining_time(ms12->ms12_timer_id);
    if (remaining_time > 0) {
        audio_timer_stop(ms12->ms12_timer_id);
    }
    int ret = aml_audio_timer_delete(ms12->ms12_timer_id);
    ALOGD("func:%s timer_id:%d  ret:%d",__func__, ms12->ms12_timer_id, ret);

    ALOGI("%s() dolby_ms12_set_quit_flag %d", __FUNCTION__, is_quit);
    dolby_ms12_set_quit_flag(is_quit);

    set_audio_system_format(AUDIO_FORMAT_INVALID);
    set_audio_app_format(AUDIO_FORMAT_INVALID);
    set_audio_main_format(AUDIO_FORMAT_INVALID);
    dolby_ms12_config_params_set_system_flag(false);
    dolby_ms12_config_params_set_app_flag(false);
    dolby_ms12_set_enforce_timeslice(false);
    dolby_ms12_set_tv_tuning_flag(false);
    dolby_ms12_set_dap_only(0);
    aml_ms12_cleanup(ms12);
    ms12->output_config = 0;
    ms12->dolby_ms12_enable = false;
    ms12->input_total_ms = 0;
    ms12->bitstream_cnt = 0;
    ms12->nbytes_of_dmx_output_pcm_frame = 4; //2ch * 16bit, set a default one
    ms12->last_frames_position = 0;
    ms12->dolby_ms12_init_flags = false;
    ms12->dtv_decoder_offset_base = 0;
    ms12->dap_only_enable = false;

    ring_buffer_release(&ms12->spdif_ring_buffer);
    ms12->ms12_scheduler_state = MS12_SCHEDULER_NONE;
    ms12->last_scheduler_state = MS12_SCHEDULER_NONE;
    //ms12->ms12_resume_state = MS12_RESUME_NONE;  // dap single node require hwsync processing ?
    ms12_close_all_spdifout(ms12);

exit:
    ALOGI("--%s(), locked", __FUNCTION__);
    pthread_mutex_unlock(&ms12->lock);
    ALOGI("-%s()", __FUNCTION__);
    return 0;
}

/*
 *@brief dolby ms12 main process
 *
 * input parameters
 *     stream: audio_stream_out handle
 *     buffer: data buffer address
 *     bytes: data size
 * output parameters
 *     use_size: buffer used size
 */

int aml_dap_process(
    struct audio_stream_out *stream
    , const void *buffer
    , size_t bytes
    , size_t *use_size)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int ms12_output_size = 0;
    int n_consumed_bytes = 0;
    void *output_buffer = NULL;
    size_t output_buffer_bytes = 0;

    void *input_buffer = (void *)buffer;
    size_t input_bytes = bytes;
    void *main_frame_buffer = input_buffer;/*input_buffer as default*/
    int main_frame_size = input_bytes;/*input_bytes as default*/
    int ret = 0;

    if (adev->debug_flag >= 2) {
        ALOGI("\n%s() in continuous %d input ms12 bytes %d input bytes %zu\n",
              __FUNCTION__, adev->continuous_audio_mode, n_consumed_bytes, input_bytes);
    }

    pthread_mutex_lock(&ms12->lock);

    if (ms12->dap_only_enable) {
        //ms12 input main
        int dual_input_ret = 0;

        if (main_frame_buffer && (main_frame_size > 0)) {
            /*input main frame*/
            int main_format = ms12->input_config_format;
            int main_channel_num = audio_channel_count_from_out_mask(ms12->config_channel_mask);
            int main_sample_rate = ms12->config_sample_rate;
            n_consumed_bytes = dolby_ms12_dap_process(
                                                ms12->dolby_ms12_ptr
                                                , main_frame_buffer
                                                , main_frame_size
                                                , main_format
                                                , main_channel_num
                                                , main_sample_rate);

            if (adev->debug_flag >= 2) {
                ALOGI("%s line %d main_format %#x ret main_channel_num %d main_sample_rate %d in %d n_consumed_bytes %d\n",
                    __func__, __LINE__, main_format, main_channel_num, main_sample_rate, main_frame_size, n_consumed_bytes);
            }

            /*set the dolby ms12 debug level*/
            dolby_ms12_enable_debug();
            if (n_consumed_bytes > 0) {
                /* Passthrough Mode, only get the MAIN data as the single input */
                if (adev->debug_flag >= 2) {
                    ALOGI("%s() continuous %d n_consumed_bytes %d input bytes %zu sr %d main size %d \n",
                          __FUNCTION__, adev->continuous_audio_mode, n_consumed_bytes, input_bytes,
                          ms12->config_sample_rate, main_frame_size);
                }
                *use_size = n_consumed_bytes;
            }
        } else {
            *use_size = input_bytes;
        }
exit:
        if (get_ms12_dump_enable(DUMP_MS12_INPUT_MAIN)) {
            dump_ms12_output_data((void*)buffer, *use_size, MS12_INPUT_SYS_MAIN_FILE);
        }
        ms12->ms12_main_input_size += *use_size;
        ret = 0;
    } else {
        ret = -1;
    }
    pthread_mutex_unlock(&ms12->lock);
    return ret;
}


static bool ms12_audio_data_detect_is_zero(const void *in_data, int samples, audio_format_t format)
{
    /* value 100 for filter noise data,
    ** this value can be adjusted according logs.
    */
    const int MS12_DATA_DETECT_THRESHOLD = 100;

    int ret = 0;
    size_t i = 0;
    int32_t buf_value = 0, buf_max = 0;

    if (in_data == NULL || samples <= 0) {
        AM_LOGE("%s in_data %p, samples %d", __func__, in_data, samples);
        return true;
    }

    if (format == AUDIO_FORMAT_PCM_FLOAT) {
        const float *pFloat = (const float *)in_data;
        for (i = 0; i < samples; i++) {
            buf_value = clamp32_from_float(pFloat[i]);
            if (buf_value > buf_max) {
                //ALOGD("%s  i:%u, buf_value:%lu, buf_max:%lu", __func__, i, buf_value, buf_max);
                buf_max = buf_value;
            }
        }
    } else if (format == AUDIO_FORMAT_PCM_32_BIT) {
        const int32_t *pInt32 = (const int32_t *)in_data;
        for (i = 0; i < samples; i++) {
            buf_value = pInt32[i];
            if (buf_value > buf_max) {
                buf_max = buf_value;
            }
        }
    } else if (format == AUDIO_FORMAT_PCM_16_BIT) {
        const int16_t *pInt16 = (const int16_t *)in_data;
        for (i = 0; i < samples; i++) {
            buf_value = pInt16[i];
            if (buf_value > buf_max) {
                buf_max = buf_value;
            }
        }
    } else {
        AM_LOGE("not support format %d", format);
        return false;
    }

    if (buf_max <= MS12_DATA_DETECT_THRESHOLD) {
        ret = true; //audio data is zero, return true
    } else {
        ret = false;
    }
    ALOGD("%s ret:%d, samples:%d buf_value:%d", __func__, ret, samples, buf_max);
    return ret;
}


static bool ms12_config_decoder_mute(
    struct aml_stream_out *aml_out, const char *p_data, const int data_frames, aml_data_format_t *p_data_format)
{
    int duration_ms = 0;
    float current_volume = 1.0;
    bool is_zero_data = false;
    ease_setting_t ease_setting;
    struct dolby_ms12_dec_desc *ms12_dec = NULL;

    if (aml_out == NULL || p_data_format == NULL) {
        return false;
    }
    ms12_dec = aml_out->ms12_dec_handle;

    if (ms12_dec->is_muted == aml_out->is_decoder_muted) {
        return false;
    }
    ms12_dec->is_muted = aml_out->is_decoder_muted;
    duration_ms = aml_out->decoder_mute_duration;
    current_volume = aml_audio_ease_get_current_volume(&aml_out->volume_easing);

    memset(&ease_setting, 0, sizeof(ease_setting));
    ease_setting.ease_type = EaseLinear;
    ease_setting.ease_frames = (int64_t)duration_ms * p_data_format->sr / 1000;
    if (ms12_dec->is_muted) {
        ease_setting.start_volume = current_volume;
        ease_setting.target_volume = 0.0f;
    } else {
        ease_setting.start_volume = 0.0f;
        ease_setting.target_volume = current_volume;
    }

    is_zero_data = ms12_audio_data_detect_is_zero(p_data, data_frames * p_data_format->ch, p_data_format->format);
    if (ease_setting.ease_frames <= 0 && !is_zero_data) {
        AM_LOGI("stream:%p, mute %d, detected non_zero_data, ease_frames(%d) change to 512",
            aml_out, ms12_dec->is_muted, ease_setting.ease_frames);
        ease_setting.ease_frames = 512;
    }

    AM_LOGI("stream:%p, mute %d, ease_frames=%d", aml_out, ms12_dec->is_muted, ease_setting.ease_frames);
    aml_audio_ease_config_frame(&aml_out->volume_easing, &ease_setting, p_data_format);
    return true;
}

static bool ms12_netflix_config_decoder_volume(
    struct aml_stream_out *aml_out, const char *p_data, const int data_frames, aml_data_format_t *p_data_format)
{
    int duration_ms = 0;
    float current_volume = 1.0;
    bool is_zero_data = false;
    ease_setting_t ease_setting;
    bool first_process = false;
    bool volume_updated = false;
    bool b_moving = false;
    int vol_write_ms = 0;
    float next_volume = AML_AUDIO_GAIN_FLOAT_INVALID;
    uint64_t curr_ease_frame = 0;
    struct dolby_ms12_dec_desc *ms12_dec = NULL;

    // If use 256 frames, volume fading will be too short.
    // 768 : netflix mediavol tuning value
    const int ease_frames = 768;

    if (aml_out == NULL || aml_out->ms12_dec_handle == NULL || p_data_format == NULL) {
        return false;
    }
    next_volume = aml_out->volume_l;

    ms12_dec = aml_out->ms12_dec_handle;
    b_moving = aml_volume_shaper_update_moving_frame(&aml_out->volume_shaper, data_frames);
    curr_ease_frame = aml_volume_shaper_get_current_frame(&aml_out->volume_shaper);
    current_volume = aml_audio_ease_get_current_volume(&aml_out->volume_easing);

    if (!(aml_out->volume_easing.do_easing || b_moving)) {
        if (aml_volume_shaper_get(&aml_out->volume_shaper, ease_frames, &next_volume, &vol_write_ms) == 0) {
            ALOGV("%s aml_volume_shaper_get OK, next_volume %f, ease_frames %d", __func__, next_volume, ease_frames);
        }
        if (aml_volume_shaper_check_sanity(next_volume) && !aml_volume_shaper_check_equal(current_volume, next_volume)) {
            volume_updated = true;
        }
    }
    if (ms12_dec->is_muted) {
        next_volume = 0.0f;
        if (!aml_volume_shaper_check_equal(current_volume, next_volume) &&
            !(aml_volume_shaper_check_equal(aml_out->volume_easing.target_volume, next_volume)
            && aml_out->volume_easing.do_easing)) {
            volume_updated = true;
        }
    }

    if (aml_out->volume_easing.data_format.ch == 0) {
        first_process = true;
    }
    if (!volume_updated && !first_process) {
        return false;
    }

    memset(&ease_setting, 0, sizeof(ease_setting));
    ease_setting.ease_type = EaseLinear;
    ease_setting.ease_frames = ease_frames;
    ease_setting.start_volume = current_volume;
    ease_setting.target_volume = next_volume;

    if (curr_ease_frame == 0) {
        /* why we need set volume immediately, because the easing duration is 5.33ms, if the app
         * want to set volume quickly before any data playing, then we should set the volume directly.
         * otherwise the volume changing will not be smooth
        */
        is_zero_data = ms12_audio_data_detect_is_zero(p_data, data_frames * p_data_format->ch, p_data_format->format);
        if (is_zero_data) {
            ease_setting.ease_frames = 0;  // shouldn't delay...
        }
    }
    if (!is_zero_data && vol_write_ms > 2) {
        // 2ms is for system scheduler jitter
        int adjust_frames = (int64_t)(vol_write_ms - 2) * p_data_format->sr / 1000;
        adjust_frames = ALIGN(adjust_frames, data_frames);
        if (adjust_frames > ease_setting.ease_frames) {
            ease_setting.ease_frames = adjust_frames;
            AM_LOGI("stream:%p vol_write_ms %d, adjust_frames %d", aml_out, vol_write_ms, adjust_frames);
        }
    }

    if (!aml_volume_shaper_check_equal(current_volume, next_volume)) {
        AM_LOGI("stream:%p change volume %f to %f, mute %d, ease_frames=%d",
            aml_out, current_volume, next_volume, ms12_dec->is_muted, ease_setting.ease_frames);
    }
    aml_audio_ease_config_frame(&aml_out->volume_easing, &ease_setting, p_data_format);

    return true;
}

static bool ms12_config_decoder_volume(struct aml_stream_out *aml_out, aml_data_format_t *p_data_format)
{
    int duration_ms = 0;
    float current_volume = 1.0f;
    float target_volume = 1.0f;
    ease_setting_t ease_setting;
    float next_volume = AML_AUDIO_GAIN_FLOAT_INVALID;
    struct dolby_ms12_dec_desc *ms12_dec = NULL;
    const int ease_frames = 1536;

    if (aml_out == NULL || p_data_format == NULL) {
        return false;
    }
    ms12_dec = aml_out->ms12_dec_handle;
    current_volume = aml_audio_ease_get_current_volume(&aml_out->volume_easing);
    target_volume = aml_out->volume_easing.target_volume;
    //when Dolby MS12 use not 1.0 volume
    //the PCM Render can not output at a same volume for both DDP and AC4.
    //AC4 should use the 1.0 volume and control the volume through the PCM output.
    //In the STB, PCM output will be always without DAP device processing.
    //will not call the dap_pcm_output().
    if (!is_AC4_stream_with_pcm_sink_on_stb(aml_out)) {
        next_volume = aml_out->volume_l;
    }
    else {
        next_volume = 1.0f;
    }

    if (ms12_dec->is_muted) {
        next_volume = 0.0f;
    }

    if (!aml_volume_shaper_check_sanity(next_volume)
        || (aml_volume_shaper_check_equal(target_volume, next_volume) && aml_out->volume_easing.do_easing)) {
        return false;
    }

    memset(&ease_setting, 0, sizeof(ease_setting));
    ease_setting.ease_type = EaseLinear;
    ease_setting.ease_frames = ease_frames;
    ease_setting.start_volume = current_volume;
    ease_setting.target_volume = next_volume;

    if (!aml_volume_shaper_check_equal(current_volume, next_volume)) {
        AM_LOGI("stream:%p change volume %f to %f, mute %d, ease_frames=%d",
              aml_out, current_volume, next_volume, ms12_dec->is_muted, ease_frames);
    }
    aml_audio_ease_config_frame(&aml_out->volume_easing, &ease_setting, p_data_format);

    return true;
}


static int ms12_decoder_volume_process(struct aml_stream_out *aml_out, Aml_MS12_ProcessInfo_t *pstProcessInfo)
{
    if (aml_out == NULL || pstProcessInfo == NULL || pstProcessInfo->s32Channel <= 0) {
        return -1;
    }

    int sample_size = 2;
    int frame_size = 0;
    int n_data_frames = 0;
    int n_frames_offset = 0;
    char *pu8Data = NULL;
    aml_data_format_t data_format;
    struct dolby_ms12_dec_desc *ms12_dec = aml_out->ms12_dec_handle;
    const int PROCESS_FRAMES = 256;  // process 256 samples each time, so that zero data detect more accurate.
    int debug_value = get_debug_value(AML_DEBUG_AUDIOHAL_VOLUME_SHAPER);
    char dump_path[64];
    aml_audio_ease_t *p_volume_ease = &aml_out->volume_easing;

    memset(&data_format, 0, sizeof(data_format));
    data_format.sr = pstProcessInfo->s32SampleRate;
    data_format.ch = pstProcessInfo->s32Channel;
    data_format.endian = 0;

    switch (pstProcessInfo->s32InFrameType) {
        case PCM_INT16:
            sample_size = 2;
            data_format.format = AUDIO_FORMAT_PCM_16_BIT;
            break;
        case PCM_INT32:
            sample_size = 4;
            data_format.format = AUDIO_FORMAT_PCM_32_BIT;
            break;
        case PCM_FLOAT32:
            sample_size = 4;
            data_format.format = AUDIO_FORMAT_PCM_FLOAT;
            break;
        default:
            sample_size = 2;
            break;
    }
    frame_size = pstProcessInfo->s32Channel * sample_size;
    n_data_frames = pstProcessInfo->u32InBufferSize / frame_size;

    while (n_frames_offset < n_data_frames) {
        int handle_frames = n_data_frames - n_frames_offset;
        if (handle_frames >= PROCESS_FRAMES) {
            handle_frames = PROCESS_FRAMES;
        }
        pu8Data = pstProcessInfo->pu8InBuffer + n_frames_offset * frame_size;

        // volume configure
        if (ms12_config_decoder_mute(aml_out, pu8Data, handle_frames, &data_format) == false) {
            if (aml_volume_shaper_get_enable(&aml_out->volume_shaper)) {
                ms12_netflix_config_decoder_volume(aml_out, pu8Data, handle_frames, &data_format);
            } else {
                ms12_config_decoder_volume(aml_out, &data_format);
            }
        }

        // About 2.1s(256*400 frames) print once stream volume information.
        aml_out->volume_shaper.s32RunCount++;
        if ((aml_out->volume_shaper.s32RunCount % 400) == 0) {
            debug_value |= 1;
        }

        if (debug_value != 0) {
            float current = aml_audio_ease_get_current_volume(p_volume_ease);
            float next = p_volume_ease->target_volume;
            int ease_frames = p_volume_ease->ease_frames;
            AM_LOGD("stream:%p mute %d, current %f, next %f, s32InFrameType %d, s32Channel %d, ease_frames %d", aml_out,
                ms12_dec->is_muted, current, next, pstProcessInfo->s32InFrameType, pstProcessInfo->s32Channel, ease_frames);
        }

        if (debug_value & AML_VOLUME_DEBUG_DUMP_MASK) {
            memset(dump_path, 0, sizeof(dump_path));
            snprintf(dump_path, sizeof(dump_path)-1, "%s/before_vol.pcm", AUDIO_HAL_DUMP_DEFAULT_PATH);
            aml_dump_audio_bitstreams(dump_path, pu8Data, handle_frames * frame_size);
        }

        // volume process
        if ((debug_value & AML_VOLUME_DEBUG_BYPASS_MASK) == AML_VOLUME_DEBUG_BYPASS_MASK) {
            AM_LOGD("stream:%p volume debug bypass enable ! (volume or mute request are ignored)", aml_out);
        } else {
            // reduce cpu loading
            bool skip_easing = false;
            if (is_float_equal(p_volume_ease->start_volume, p_volume_ease->target_volume) || aml_audio_ease_done(p_volume_ease)) {
                if (is_float_equal(p_volume_ease->target_volume, 1.0f)) {
                    skip_easing = true;
                    if (debug_value) {
                        ALOGD("%s stream %p volume is 1.0f, skip easing !", __func__, aml_out);
                    }
                } else if (is_float_equal(p_volume_ease->target_volume, 0.0f)) {
                    skip_easing = true;
                    memset(pu8Data, 0, handle_frames * frame_size);
                }
            }

            if (skip_easing) {
                p_volume_ease->ease_frames_elapsed = p_volume_ease->ease_frames;
                p_volume_ease->do_easing = false;
            } else {
                aml_audio_ease_process(p_volume_ease, pu8Data, handle_frames * frame_size, true);
            }
        }

        if (debug_value & AML_VOLUME_DEBUG_DUMP_MASK) {
            memset(dump_path, 0, sizeof(dump_path));
            snprintf(dump_path, sizeof(dump_path)-1, "%s/after_vol.pcm", AUDIO_HAL_DUMP_DEFAULT_PATH);
            aml_dump_audio_bitstreams(dump_path, pu8Data, handle_frames * frame_size);
        }

        n_frames_offset += handle_frames;
    }
    return 0;
}

static int ms12_decoder_sound_mode_process(struct aml_stream_out *aml_out, Aml_MS12_ProcessInfo_t *pstProcessInfo)
{
    if (aml_out == NULL || pstProcessInfo == NULL || pstProcessInfo->s32Channel <= 0) {
        return -1;
    }
    if (is_dolby_ms12_support_compression_format (aml_out->hal_internal_format)) {
        return -1;
    }

    int sample_size = 2;
    struct aml_audio_device *adev = aml_out->dev;
    AM_AOUT_OutputMode_t cur_sound_track_mode = adev->sound_track_mode;

#ifdef ENABLE_DVB_PATCH
    bool is_dtv_patch = is_dtv_stream_out(&aml_out->stream);
    if (is_dtv_patch) {
        aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)aml_out->audio_buffer;
        aml_audio_buffer_t *audioBuffer = pBuffer->inBuffer;
        aml_dtv_audiopara_t *dtv_audio_info = audioBuffer->privObject;
        cur_sound_track_mode = dtv_audio_info->output_mode;
    }
#endif

    /* For local play or dtv input, analog audio output channel should be switched by User setting*/
    if (cur_sound_track_mode > AM_AOUT_OUTPUT_STEREO) {
        audio_format_t output_format;
        switch (pstProcessInfo->s32InFrameType) {
        case PCM_INT16:
            sample_size = 2;
            output_format = AUDIO_FORMAT_PCM_16_BIT;
            break;
        case PCM_INT32:
            sample_size = 4;
            output_format = AUDIO_FORMAT_PCM_32_BIT;
            break;
        case PCM_FLOAT32:
            sample_size = 4;
            output_format = AUDIO_FORMAT_PCM_FLOAT;
            break;
        default:
            output_format = AUDIO_FORMAT_PCM_16_BIT;
            break;
        }

        if (adev->debug_flag >= 2) {
            AM_LOGI("cur_sound_track_mode %d as32Acmod=%#x u32InBufferSize: %#x\n", cur_sound_track_mode, pstProcessInfo->as32Acmod[0], pstProcessInfo->u32InBufferSize);
            AM_LOGI("output_format=%#x", output_format);
        }
        if (pstProcessInfo->as32Acmod[0] == AML_DOLBY_ACMOD_STEREO) {
            size_t in_buff_chans = 8;
            size_t out_buff_chans = 2;
            pstProcessInfo->u32InBufferSize = adjust_channels((const void* )pstProcessInfo->pu8InBuffer, in_buff_chans,
                             (void*)pstProcessInfo->pu8InBuffer, out_buff_chans,
                             sample_size, pstProcessInfo->u32InBufferSize);

            if (get_ms12_dump_enable(DUMP_MS12_CALLBACK_PROCESS)) {
                dump_ms12_output_data((void*)pstProcessInfo->pu8InBuffer, pstProcessInfo->u32InBufferSize, MS12_CALLBACK_IN_FILE);
            }
            aml_audio_switch_output_mode((int16_t *)pstProcessInfo->pu8InBuffer, pstProcessInfo->u32InBufferSize, output_format, cur_sound_track_mode);
            if (get_ms12_dump_enable(DUMP_MS12_CALLBACK_PROCESS)) {
                dump_ms12_output_data((void*)pstProcessInfo->pu8InBuffer, pstProcessInfo->u32InBufferSize, MS12_CALLBACK_OUT_FILE);
            }
            in_buff_chans = 2;
            out_buff_chans = 8;
            pstProcessInfo->u32InBufferSize = adjust_channels((const void* )pstProcessInfo->pu8InBuffer, in_buff_chans,
                             (void*)pstProcessInfo->pu8InBuffer, out_buff_chans,
                             sample_size, pstProcessInfo->u32InBufferSize);
        }
    }
    return 0;
}

static void ms12_stream_config_apts_gap_easing(struct aml_stream_out *aml_out)
{
    int average_gap_ms = 0;
    int easing_frames = 0;
    float easing_speed = 1.0f;
    float speed_gain = 0.0f;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;
    struct timespec *gap_start_ts = &speed_info->sync_apts_gap.start_ts;
    aml_audio_speed_apts_gap_ease_t *gap_ease = &speed_info->apts_gap_ease;

    // apts gap easing, wait it complete
    if (gap_ease->target_frames > 0) {
        aml_audio_speed_reset_apts_gap(&speed_info->sync_apts_gap, AML_AUDIO_SPEED_DETECT_GAP_TIME_MS);
        return;
    }

    // speed change or stream pause/flush event just happen, wait a moment
    if (calc_time_interval_us(&speed_info->start_ts.ts, &aml_out->lasttimestamp)/1000 < 80) {
        return;
    }

    // If sonic internal cache frames large, may affect adjustment effect
    if (speed_info->last_latency_frame > 48*4) {
        int timeout_ms = AML_AUDIO_SPEED_DETECT_GAP_TIME_MS * 1.5;
        int past_time_ms = calc_time_interval_us(gap_start_ts, &aml_out->lasttimestamp)/1000;
        if ((gap_start_ts->tv_sec || gap_start_ts->tv_nsec ) && past_time_ms >= timeout_ms) {
            AM_LOGI("last_latency_frame %d, past_time_ms %d, timeout_ms %d, check apts_gap_average",
                speed_info->last_latency_frame, past_time_ms, timeout_ms);
        } else {
            return;
        }
    }

    if (!aml_audio_speed_get_apts_gap_average(&speed_info->sync_apts_gap, &aml_out->lasttimestamp, &average_gap_ms)) {
        return;
    }

    /*
     * The following viewpoints are observed by our local test:
     *
     * 1. For classic speed (like 0.25, 0.50, 1.0, 1.25, 1.5, ...)
     *    sonic internal latency calculation(output/speed - input) is precise, generate data size also precise.
     *
     * 2. Casual speed's latency calculation may not be good, so we pick up some special value for netflix(0.95, 1.05)
     *    other scenarios use default value 0.02.
    */
    if (abs(average_gap_ms) > 50) {
        AM_LOGE("average_gap_ms %d, too large !", average_gap_ms);
    } else if (abs(average_gap_ms) >= 7) {
        // jitter_diff_us = frame_diff_us - system_time_us;
        if (is_float_equal(speed_info->speed, 0.95)) {
            if (average_gap_ms > 0) {
                easing_speed = 0.935;
            } else {
                easing_speed = 0.965;
            }
        } else if  (is_float_equal(speed_info->speed, 1.05)) {
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

        easing_frames = abs(average_gap_ms) * 48 / fabs(speed_info->speed - easing_speed);
        easing_frames *= speed_info->speed;

        gap_ease->speed = easing_speed;
        gap_ease->target_frames = easing_frames;
        gap_ease->current_frames = 0;
        gap_ease->start = true;
        AM_LOGI("average_gap_ms %d, easing_speed %.3f, easing_frames %d", average_gap_ms, easing_speed, easing_frames);
    }
    aml_audio_speed_reset_apts_gap(&speed_info->sync_apts_gap, AML_AUDIO_SPEED_DETECT_GAP_TIME_MS);
}
