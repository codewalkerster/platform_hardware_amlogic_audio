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

#ifndef _DTV_PATCH_UTILS_H_
#define _DTV_PATCH_UTILS_H_

#include "aml_dec_api.h"
#include "dtv_patch_dtvsync.h"

#define AUDIO_PTS_DISCONTINUE_THRESHOLD (90000 * 5)
#define AC3_IEC61937_FRAME_SIZE 6144
#define EAC3_IEC61937_FRAME_SIZE 24576
#define DOLBY_FRAME_PTS_DURATION (32 * 90)
#define TIME_UNIT90K 90000
#define DTV_AUDIO_DATA_JITTERMS_THRESHOLD (400)
#define DTV_AUDIO_REPLAY_NEED_CACHE_MS (800 * 90)
#define DTV_AD_BUFFER_SIZE (1024 * 16)

#define DEFAULT_DTV_ADJUST_CLOCK    (1000)
#define DEFAULT_DTV_MIN_OUT_CLOCK   (1000*1000-100*1000)
#define DEFAULT_DTV_MAX_OUT_CLOCK   (1000*1000+100*1000)
#define DEFAULT_I2S_OUTPUT_CLOCK    (256*48000)
#define DEFAULT_EARC_OUTPUT_CLOCK   (128*5*48000)

#define AUDIO_ADSUBFRAME_CHECKED_SIZE 2048

#define DTV_DD_MUTE_FRAME_SIZE   768
#define DTV_DDP_MUTE_FRAME_SIZE  768


#define MAX_BUFF_LEN 36
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define DIFF_ABS(a, b) ((a) > (b) ? (a -b ) : (b -a))


#define INPUT_PACKAGE_MAXCOUNT 200


#define AD_PACK_STATUS_UNNORMAL_THRESHOLD_MS 4000

#define AD_PACK_STATUS_DROP_THRESHOLD_MS 660
#define AD_PACK_STATUS_DROP_START_THRESHOLD_MS 60

#define AD_PACK_STATUS_HOLD_THRESHOLD_MS 400
#define AD_PACK_STATUS_HOLD_START_THRESHOLD_MS 40


#define NON_DOLBY_AD_PACK_STATUS_DROP_THRESHOLD_MS 3000
#define NON_DOLBY_AD_PACK_STATUS_DROP_START_THRESHOLD_MS 2000

#define NON_DOLBY_AD_PACK_STATUS_HOLD_THRESHOLD_MS 600
#define NON_DOLBY_AD_PACK_STATUS_HOLD_START_THRESHOLD_MS 100

#define ENUM_TYPE_TO_STR_DEFAULT_STR            "INVALID_ENUM"
#define ENUM_TYPE_TO_STR_START(prefix)                      \
    const char *pStr = ENUM_TYPE_TO_STR_DEFAULT_STR;        \
    int prefixLen = strlen(prefix);                         \
    switch (type) {
#define ENUM_TYPE_TO_STR(x)                                 \
    case x:                                                 \
        pStr = #x;                                          \
        pStr += prefixLen;                                  \
        if (strlen(#x) - prefixLen > 70) {                  \
            pStr += 70;                                     \
        }                                                   \
        break;
#define ENUM_TYPE_TO_STR_END                                \
    default:                                                \
        break;                                              \
    }                                                       \
    return pStr;

/* refer to AudioSystemCmdManager */
typedef enum {
    AUDIO_DTV_PATCH_CMD_NULL        = 0,
    AUDIO_DTV_PATCH_CMD_START       = 1,    /* AUDIO_SERVICE_CMD_START_DECODE */
    AUDIO_DTV_PATCH_CMD_PAUSE       = 2,    /* AUDIO_SERVICE_CMD_PAUSE_DECODE */
    AUDIO_DTV_PATCH_CMD_RESUME      = 3,    /* AUDIO_SERVICE_CMD_RESUME_DECODE */
    AUDIO_DTV_PATCH_CMD_STOP        = 4,    /* AUDIO_SERVICE_CMD_STOP_DECODE */
    AUDIO_DTV_PATCH_CMD_SET_AD_SUPPORT  = 5,    /* AUDIO_SERVICE_CMD_SET_DECODE_AD */
    AUDIO_DTV_PATCH_CMD_SET_VOLUME  = 6,    /*AUDIO_SERVICE_CMD_SET_VOLUME*/
    AUDIO_DTV_PATCH_CMD_SET_MUTE    = 7,    /*AUDIO_SERVICE_CMD_SET_MUTE*/
    AUDIO_DTV_PATCH_CMD_SET_OUTPUT_MODE = 8,/*AUDIO_SERVICE_CMD_SET_OUTPUT_MODE */
    AUDIO_DTV_PATCH_CMD_SET_PRE_GAIN  = 9,    /*AUDIO_SERVICE_CMD_SET_PRE_GAIN */
    AUDIO_DTV_PATCH_CMD_SET_PRE_MUTE  = 10,  /*AUDIO_SERVICE_CMD_SET_PRE_MUTE */
    AUDIO_DTV_PATCH_CMD_OPEN        = 12,   /*AUDIO_SERVICE_CMD_OPEN_DECODER */
    AUDIO_DTV_PATCH_CMD_CLOSE       = 13,   /*AUDIO_SERVICE_CMD_CLOSE_DECODER */
    AUDIO_DTV_PATCH_CMD_SET_DEMUX_INFO = 14, /*AUDIO_SERVICE_CMD_SET_DEMUX_INFO ;*/
    AUDIO_DTV_PATCH_CMD_SET_SECURITY_MEM_LEVEL = 15,/*AUDIO_SERVICE_CMD_SET_SECURITY_MEM_LEVEL*/
    AUDIO_DTV_PATCH_CMD_SET_HAS_VIDEO   = 16,/*AUDIO_SERVICE_CMD_SET_HAS_VIDEO */
    AUDIO_DTV_PATCH_CMD_CONTROL       = 17,
    AUDIO_DTV_PATCH_CMD_SET_PID       = 18,
    AUDIO_DTV_PATCH_CMD_SET_FMT        = 19,
    AUDIO_DTV_PATCH_CMD_SET_AD_PID      = 20,
    AUDIO_DTV_PATCH_CMD_SET_AD_FMT      = 21,
    AUDIO_DTV_PATCH_CMD_SET_AD_ENABLE      = 22,
    AUDIO_DTV_PATCH_CMD_SET_AD_MIX_LEVEL   = 23,
    AUDIO_DTV_PATCH_CMD_SET_AD_VOL_LEVEL   = 24,
    AUDIO_DTV_PATCH_CMD_SET_MEDIA_SYNC_ID   = 25,
    AUDIO_DTV_PATCH_CMD_SET_MEDIA_PRESENTATION_ID   = 26,
    AUDIO_DTV_PATCH_CMD_SET_DTV_DEMUX_ID = 27,
    AUDIO_DTV_PATCH_CMD_SET_MEDIA_FIRST_LANG  = 29,
    AUDIO_DTV_PATCH_CMD_SET_MEDIA_SECOND_LANG = 30,
    AUDIO_DTV_PATCH_CMD_SET_SPDIF_PROTECTION_MODE  = 31,
    AUDIO_DTV_PATCH_CMD_ES_PTS_DTS_FLAG  = 32,
    AUDIO_DTV_PATCH_CMD_SET_PLAYBACK_MODE  = 33,
    AUDIO_DTV_PATCH_CMD_NUM             = 34,
} AUDIO_DTV_PATCH_CMD_TYPE;

typedef enum  {
    AD_PACK_STATUS_NORMAL,
    AD_PACK_STATUS_DROP,
    AD_PACK_STATUS_HOLD,
} AD_PACK_STATUS_T;

struct cmd_list {
    struct cmd_list *next;
    int cmd;
    int cmd_num;
    int used;
    int initd;
};

struct cmd_node {
    struct cmd_node *next;
    int cmd;
    int cmd_num;
    int used;
    int initd;
    int path_id;
    pthread_mutex_t dtv_cmd_mutex;
};

struct package {
    char *data;//buf ptr
    int size;  //package size
    char *ad_data;//ad buf ptr
    int  ad_size;//ad package size
    struct package * next;//next ptr
    int64_t pts;
    uint8_t pts_dts_flag;
    uint64_t ad_pts;
    int split_frame_size;
    uint8_t adfade;
    uint8_t adpan;
};

typedef struct {
    struct package *first;
    int pack_num;
    struct package *current;
    pthread_mutex_t tslock;
} package_list;

#define  DVB_MEDIA_LANG_SIZE 3
typedef enum  {
   NORMAL_MODE = 0,
   CACHE_MODE = 1,
} DTV_AUDIO_PLAYBACK_MODE;


typedef struct aml_dtv_audiopara {
    int demux_id;
    int security_mem_level;
    int output_mode;
    bool has_video;
    int main_fmt;
    int main_pid;
    int ad_fmt;
    int ad_pid;
    int dual_decoder_support;
    int associate_audio_mixing_enable;
    int mixing_level;
    int advol_level;
    int media_sync_id;
    int media_presentation_id;
    int ad_package_status;
    int media_first_lang;
    int media_second_lang;
    bool tv_mute;
    char *ad_data;//ad buf ptr
    int  ad_size;//ad package size
    uint8_t ad_fade;
    uint8_t ad_pan;
    uint8_t ad_placement;
    float  volume;
	int playback_mode;
} aml_dtv_audiopara_t;

int dtv_package_list_flush(package_list *list);

int dtv_package_list_init(package_list *list);
int dtv_package_add(package_list *list, struct package *p);
bool dtv_package_is_full(package_list *list);
bool dtv_package_is_empty(package_list *list);

struct package * dtv_package_get(package_list *list);

void init_cmd_list(struct cmd_node *dtv_cmd_list);

void deinit_cmd_list(struct cmd_node *dtv_cmd_list);
int dtv_audio_add_cmd(struct cmd_node *dtv_cmd_list,int cmd, int path_id);


int dtv_audio_get_cmd(struct cmd_node *dtv_cmd_list,int *cmd, int *path_id);
int dtv_audio_cmd_is_empty(struct cmd_node *dtv_cmd_list);

AD_PACK_STATUS_T check_ad_package_status(int64_t main_pts, int64_t ad_pts, aml_dtv_audiopara_t *dtv_audiopara);
void dtv_convert_language_to_string(int language_int, char * language_string);
void dtv_audio_copy_raw_mute_frame(void *buffer, int raw_format);

const char* mediasyncAudiopolicyType2Str(audio_policy type);
const char* dtvAudioPatchCmd2Str(AUDIO_DTV_PATCH_CMD_TYPE type);
audio_format_t aml_fmt_convert_to_android_fmt(int aml_fmt);
bool non_dolby_format(int audio_format);
int adapt_mixing_level_db(int mixing_level);
void dtv_audio_set_spdif_protection_mode(int mode);
audio_dual_mono_mode_t convert2_android_dual_mono_mode(AM_AOUT_OutputMode_t mode);
AM_AOUT_OutputMode_t convert2_aml_dual_mono_mode(audio_dual_mono_mode_t mode);

#endif  /* _DTV_PATCH_UTILS_H_ */
