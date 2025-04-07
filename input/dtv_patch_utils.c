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
#define LOG_TAG "audio_hw_input_dtv"
//#define LOG_NDEBUG 0

#include <cutils/atomic.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <errno.h>
#include <fcntl.h>
#include <hardware/hardware.h>
#include <inttypes.h>
#include <linux/ioctl.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/system_properties.h>
#include <system/audio.h>
#include <time.h>
#include <utils/Timers.h>
#include "aml_malloc_debug.h"
#include "dtv_patch_utils.h"
#include "dtv_patch.h"

const unsigned int mute_dd_frame[] = {
    0x5d9c770b, 0xf0432014, 0xf3010713, 0x2020dc62, 0x4842020, 0x57100404, 0xf97c3e1f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xfb7c3e9f, 0xf97c75fe, 0x9fcfe7f3,
    0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xfb7c3e9f, 0x3e5f9dff, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0x48149ff2, 0x2091,
    0x361e0000, 0x78bc6ddb, 0xbbbbe3f1, 0xb8, 0x0, 0x0, 0x0, 0x77770700, 0x361e8f77, 0x359f6fdb, 0xd65a6bad, 0x5a6badb5, 0x6badb5d6, 0xa0b5d65a, 0x1e000000, 0xbc6ddb36,
    0xbbe3f178, 0xb8bb, 0x0, 0x0, 0x0, 0x77070000, 0x1e8f7777, 0x9f6fdb36, 0x5a6bad35, 0xa6b5d6, 0x0, 0xb66de301, 0x1e8fc7db, 0x80bbbb3b, 0x0, 0x0,
    0x0, 0x0, 0x78777777, 0xb66de3f1, 0xd65af3f9, 0x5a6badb5, 0x6badb5d6, 0xadb5d65a, 0x5a6b, 0x6de30100, 0x8fc7dbb6, 0xbbbb3b1e, 0x80, 0x0, 0x0, 0x0,
    0x77777700, 0x6de3f178, 0x5af3f9b6, 0x6badb5d6, 0x605a, 0x1e000000, 0xbc6ddb36, 0xbbe3f178, 0xb8bb, 0x0, 0x0, 0x0, 0x77070000, 0x1e8f7777, 0x9f6fdb36, 0x5a6bad35,
    0x6badb5d6, 0xadb5d65a, 0xb5d65a6b, 0xa0, 0x6ddb361e, 0xe3f178bc, 0xb8bbbb, 0x0, 0x0, 0x0, 0x7000000, 0x8f777777, 0x6fdb361e, 0x6bad359f, 0xa6b5d65a, 0x0,
    0x6de30100, 0x8fc7dbb6, 0xbbbb3b1e, 0x80, 0x0, 0x0, 0x0, 0x77777700, 0x6de3f178, 0x5af3f9b6, 0x6badb5d6, 0xadb5d65a, 0xb5d65a6b, 0x5a6bad, 0xe3010000, 0xc7dbb66d,
    0xbb3b1e8f, 0x80bb, 0x0, 0x0, 0x0, 0x77770000, 0xe3f17877, 0xf3f9b66d, 0xadb5d65a, 0x605a6b, 0x0, 0x6ddb361e, 0xe3f178bc, 0xb8bbbb, 0x0, 0x0,
    0x0, 0x7000000, 0x8f777777, 0x6fdb361e, 0x6bad359f, 0xadb5d65a, 0xb5d65a6b, 0xd65a6bad, 0xa0b5, 0xdb361e00, 0xf178bc6d, 0xb8bbbbe3, 0x0, 0x0, 0x0, 0x0,
    0x77777707, 0xdb361e8f, 0xad359f6f, 0xb5d65a6b, 0x10200a6, 0x0, 0xdbb6f100, 0x8fc7e36d, 0xc0dddd1d, 0x0, 0x0, 0x0, 0x0, 0xbcbbbb3b, 0xdbb6f178, 0x6badf97c,
    0xadb5d65a, 0xb5d65a6b, 0xd65a6bad, 0xadb5, 0xb6f10000, 0xc7e36ddb, 0xdddd1d8f, 0xc0, 0x0, 0x0, 0x0, 0xbbbb3b00, 0xb6f178bc, 0xadf97cdb, 0xb5d65a6b, 0x4deb00ad
};

const unsigned int mute_ddp_frame[] = {
    0x7f01770b, 0x20e06734, 0x2004, 0x8084500, 0x404046c, 0x1010104, 0xe7630001, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0xce7f9fcf, 0x7c3e9faf,
    0xcfe7f3f9, 0xf97c3e9f, 0x9fcfe7f3, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0xf37f9fcf, 0x9fcfe7ab, 0xf3f97c3e, 0x3e9fcfe7, 0xe7f3f97c, 0x7c3e9fcf, 0xcfe7f3f9, 0xf97c3e9f, 0x53dee7f3, 0xf0e9,
    0x6d3c0000, 0xf178dbb6, 0x7777c7e3, 0x70, 0x0, 0x0, 0x0, 0xeeee0e00, 0x6d3c1eef, 0x6b3edfb6, 0xadb5d65a, 0xb5d65a6b, 0xd65a6bad, 0x406badb5, 0x3c000000, 0x78dbb66d,
    0x77c7e3f1, 0x7077, 0x0, 0x0, 0x0, 0xee0e0000, 0x3c1eefee, 0x3edfb66d, 0xb5d65a6b, 0x20606bad, 0x0, 0xdbb66d3c, 0xc7e3f178, 0x707777, 0x0, 0x0,
    0x0, 0xe000000, 0x1eefeeee, 0xdfb66d3c, 0xd65a6b3e, 0x5a6badb5, 0x6badb5d6, 0xadb5d65a, 0x406b, 0xb66d3c00, 0xe3f178db, 0x707777c7, 0x0, 0x0, 0x0, 0x0,
    0xefeeee0e, 0xb66d3c1e, 0x5a6b3edf, 0x6badb5d6, 0x2060, 0x6d3c0000, 0xf178dbb6, 0x7777c7e3, 0x70, 0x0, 0x0, 0x0, 0xeeee0e00, 0x6d3c1eef, 0x6b3edfb6, 0xadb5d65a,
    0xb5d65a6b, 0xd65a6bad, 0x406badb5, 0x3c000000, 0x78dbb66d, 0x77c7e3f1, 0x7077, 0x0, 0x0, 0x0, 0xee0e0000, 0x3c1eefee, 0x3edfb66d, 0xb5d65a6b, 0x20606bad, 0x0,
    0xdbb66d3c, 0xc7e3f178, 0x707777, 0x0, 0x0, 0x0, 0xe000000, 0x1eefeeee, 0xdfb66d3c, 0xd65a6b3e, 0x5a6badb5, 0x6badb5d6, 0xadb5d65a, 0x406b, 0xb66d3c00, 0xe3f178db,
    0x707777c7, 0x0, 0x0, 0x0, 0x0, 0xefeeee0e, 0xb66d3c1e, 0x5a6b3edf, 0x6badb5d6, 0x2060, 0x6d3c0000, 0xf178dbb6, 0x7777c7e3, 0x70, 0x0, 0x0,
    0x0, 0xeeee0e00, 0x6d3c1eef, 0x6b3edfb6, 0xadb5d65a, 0xb5d65a6b, 0xd65a6bad, 0x406badb5, 0x3c000000, 0x78dbb66d, 0x77c7e3f1, 0x7077, 0x0, 0x0, 0x0, 0xee0e0000,
    0x3c1eefee, 0x3edfb66d, 0xb5d65a6b, 0x20606bad, 0x0, 0xdbb66d3c, 0xc7e3f178, 0x707777, 0x0, 0x0, 0x0, 0xe000000, 0x1eefeeee, 0xdfb66d3c, 0xd65a6b3e, 0x5a6badb5,
    0x6badb5d6, 0xadb5d65a, 0x406b, 0xb66d3c00, 0xe3f178db, 0x707777c7, 0x0, 0x0, 0x0, 0x0, 0xefeeee0e, 0xb66d3c1e, 0x5a6b3edf, 0x6badb5d6, 0x40, 0x7f227c55,
};
#define mixing_level_base (32)

const float mixing_coefficient[65] = {
    -100, -58, -45.5,   -43, -40.5, -38.5, -36.8,  -35, -33,   -31, -29.5, -27.5,  -26,//[-32 to -20]
    -24,  -22,   -20, -18.5,   -17,   -15,   -13,  -11,  -9,    -6,    -5, -4.5,    -4,//[-19 to  -7]
    -3.5,  -3,  -2.5,    -2,  -1.5,    -1,     0,  0.6,  1.2,  1.8,   2.4,    3,   3.6,//[-6  to   6]
     4.2, 4.8,   5.4,     6,   6.3,   6.6,   6.9,  7.2,  7.5,  7.8,   8.1,   8.4,  8.7,//[7   to  19]
     9.0, 9.2,   9.4,   9.6,   9.8,  10.0,  10.2, 10.4, 10.6, 10.8,   11.0, 11.5, 12.0,//[20  to  32]
};

int adapt_mixing_level_db(int mixing_level)
{
    return mixing_coefficient[mixing_level + mixing_level_base];

}


int dtv_package_list_flush(package_list *list)
{
    pthread_mutex_lock(&(list->tslock));
    struct package * dtv_package = NULL;
    while (list->pack_num && list->first) {
        dtv_package = list->first;
        list->first = list->first->next;
        if (dtv_package->data) {
            aml_audio_free(dtv_package->data);
            dtv_package->data = NULL;
        }
        if (dtv_package->ad_data) {
            aml_audio_free(dtv_package->ad_data);
            dtv_package->ad_data = NULL;
        }
        aml_audio_free(dtv_package);
        dtv_package = NULL;
        list->pack_num--;
    }
    pthread_mutex_unlock(&(list->tslock));
    return 0;
}

int dtv_package_list_init(package_list *list)
{
    pthread_mutex_init(&list->tslock, NULL);
    pthread_mutex_lock(&list->tslock);
    list->first = NULL;
    list->pack_num = 0;
    list->current = NULL;
    pthread_mutex_unlock(&(list->tslock));
    return 0;
}
int dtv_package_add(package_list *list, struct package *p)
{
    pthread_mutex_lock(&list->tslock);
    if (list->pack_num == INPUT_PACKAGE_MAXCOUNT) { //enough
        ALOGI("list->pack_num %d",list->pack_num);
        pthread_mutex_unlock(&(list->tslock));
        return -2;
    }
    if (list->pack_num == 0) { //first package
        list->first = p;
        list->current = p;
        list->pack_num = 1;
    } else {
        list->current->next = p;
        list->current = p;
        list->pack_num++;
    }
    pthread_mutex_unlock(&list->tslock);
    return 0;
}

bool dtv_package_is_full(package_list *list)
{
    bool ret = false;
    pthread_mutex_lock(&list->tslock);
    ret = list->pack_num == INPUT_PACKAGE_MAXCOUNT;
    pthread_mutex_unlock(&list->tslock);
    return ret;
}
bool dtv_package_is_empty(package_list *list)
{
    bool ret = false;
    pthread_mutex_lock(&list->tslock);
    ret = list->pack_num == 0;
    pthread_mutex_unlock(&list->tslock);
    return ret;
}


struct package *dtv_package_get(package_list *list)
{
    pthread_mutex_lock(&(list->tslock));
    if (list->pack_num == 0) {
        pthread_mutex_unlock(&list->tslock);
        return NULL;
    }
    struct package *p = list->first;
    if (list->pack_num == 1) {
        list->first = NULL;
        list->pack_num = 0;
        list->current = NULL;
    } else if (list->pack_num > 1) {
        list->first = list->first->next;
        list->pack_num--;
    }
    pthread_mutex_unlock(&list->tslock);
    return p;
}

void init_cmd_list(struct cmd_node *dtv_cmd_list)
{
    dtv_cmd_list->next = NULL;
    dtv_cmd_list->cmd = -1;
    dtv_cmd_list->cmd_num = 0;
    dtv_cmd_list->used = 0;
    dtv_cmd_list->initd = 1;
    pthread_mutex_init(&dtv_cmd_list->dtv_cmd_mutex, NULL);
}

void deinit_cmd_list(struct cmd_node *dtv_cmd_list)
{
    struct cmd_node *dtv_cmd = NULL;
    pthread_mutex_lock(&dtv_cmd_list->dtv_cmd_mutex);
    while (dtv_cmd_list->next) {
        dtv_cmd = dtv_cmd_list->next;
        if (dtv_cmd != NULL) {
            dtv_cmd_list->next = dtv_cmd->next;
            dtv_cmd_list->cmd_num--;
        }
        dtv_cmd_list = dtv_cmd_list->next;
        aml_audio_free(dtv_cmd);
    }
    pthread_mutex_unlock(&dtv_cmd_list->dtv_cmd_mutex);
    pthread_mutex_destroy(&dtv_cmd_list->dtv_cmd_mutex);
}

int dtv_audio_add_cmd(struct cmd_node *dtv_cmd_list,int cmd, int path_id)
{
    struct cmd_node *list = NULL;
    struct cmd_node *new_cmd_node = NULL;
    int index = 0;
    if (!dtv_cmd_list || dtv_cmd_list->initd == 0) {
        return 0;
    }

    pthread_mutex_lock(&dtv_cmd_list->dtv_cmd_mutex);
    new_cmd_node = aml_audio_malloc(sizeof(struct cmd_node));
    if (!new_cmd_node ) {
        ALOGE("new_cmd_node aml_audio_malloc failed");
        pthread_mutex_unlock(&dtv_cmd_list->dtv_cmd_mutex);
        return -1;
    }
    new_cmd_node->cmd = cmd;
    new_cmd_node->path_id = path_id;
    new_cmd_node->next = NULL;
    new_cmd_node->used = 1;
    list = dtv_cmd_list;
    while (list->next != NULL) {
        list = list->next;
    }
    list->next = new_cmd_node;
    dtv_cmd_list->cmd_num++;
    pthread_mutex_unlock(&dtv_cmd_list->dtv_cmd_mutex);
    ALOGI("add by live dtv_patch_add_cmd the cmd is %d \n", cmd);
    return 0;
}

int dtv_audio_get_cmd(struct cmd_node *dtv_cmd_list,int *cmd, int *path_id)
{
    struct cmd_node *dtv_cmd = NULL;
    ALOGI("enter dtv_patch_get_cmd function now\n");
    pthread_mutex_lock(&dtv_cmd_list->dtv_cmd_mutex);
    dtv_cmd = dtv_cmd_list->next;
    if (dtv_cmd != NULL) {
        dtv_cmd_list->next = dtv_cmd->next;
        dtv_cmd_list->cmd_num--;
    } else {
        pthread_mutex_unlock(&dtv_cmd_list->dtv_cmd_mutex);
        return -1;
    }

    dtv_cmd->used = 0;
    *cmd = dtv_cmd->cmd;
    *path_id = dtv_cmd->path_id;
    aml_audio_free(dtv_cmd);
    pthread_mutex_unlock(&dtv_cmd_list->dtv_cmd_mutex);
    ALOGI("leave dtv_patch_get_cmd the cmd is %d  path %d\n", *cmd, *path_id);
    return 0;
}
int dtv_audio_cmd_is_empty(struct cmd_node *dtv_cmd_list)
{
    pthread_mutex_lock(&dtv_cmd_list->dtv_cmd_mutex);
    if (dtv_cmd_list->next == NULL) {
        pthread_mutex_unlock(&dtv_cmd_list->dtv_cmd_mutex);
        return 1;
    }
    pthread_mutex_unlock(&dtv_cmd_list->dtv_cmd_mutex);
    return 0;
}

AD_PACK_STATUS_T check_ad_package_status(int64_t main_pts, int64_t ad_pts, aml_dtv_audiopara_t *dtv_audio_info)
{

    AD_PACK_STATUS_T ad_status = dtv_audio_info->ad_package_status;
    struct aml_audio_device *aml_dev = aml_adev_get_handle();

    if (dtv_audio_info->ad_package_status == -1) {
       ad_status = AD_PACK_STATUS_NORMAL;
    }

    int drop_threshold_ms,drop_start_threshold_ms,hold_start_threshold_ms,hold_threshold_ms;
    bool is_dolby_format = (dtv_audio_info->main_fmt == ACODEC_FMT_AC3 ||
                            dtv_audio_info->main_fmt == ACODEC_FMT_EAC3||
                            dtv_audio_info->main_fmt == ACODEC_FMT_AC4);
    bool is_aac_format = (dtv_audio_info->main_fmt == ACODEC_FMT_AAC ||
                          dtv_audio_info->main_fmt == ACODEC_FMT_AAC_LATM);

    if (is_dolby_format) {
       drop_threshold_ms = AD_PACK_STATUS_DROP_THRESHOLD_MS;
       drop_start_threshold_ms = AD_PACK_STATUS_DROP_START_THRESHOLD_MS;
       hold_threshold_ms = AD_PACK_STATUS_HOLD_THRESHOLD_MS;
       hold_start_threshold_ms = AD_PACK_STATUS_HOLD_START_THRESHOLD_MS;
    } else {
       drop_threshold_ms = NON_DOLBY_AD_PACK_STATUS_DROP_THRESHOLD_MS;
       drop_start_threshold_ms = NON_DOLBY_AD_PACK_STATUS_DROP_START_THRESHOLD_MS;
       hold_threshold_ms = NON_DOLBY_AD_PACK_STATUS_HOLD_THRESHOLD_MS;
       hold_start_threshold_ms = NON_DOLBY_AD_PACK_STATUS_HOLD_START_THRESHOLD_MS;

    }

    int timems_diff = llabs(main_pts - ad_pts) / 90;
    if (ad_pts == 0) {
       if (ad_status == AD_PACK_STATUS_HOLD) {
           return AD_PACK_STATUS_NORMAL;
       } else {
            return ad_status;
       }
    } else if (main_pts == 0) {
        return ad_status;
    }

    if (timems_diff > AD_PACK_STATUS_UNNORMAL_THRESHOLD_MS) {
        if (is_dolby_format || is_aac_format) {
            ALOGI("timems_diff %d it is impossible so drop", timems_diff);
            return AD_PACK_STATUS_DROP;
        } else {
            ALOGI("timems_diff %d it is impossible so do not check", timems_diff);
            return AD_PACK_STATUS_NORMAL;
        }
    }

    switch (ad_status) {
        case AD_PACK_STATUS_NORMAL:
            if (main_pts >= ad_pts) {
                timems_diff = (main_pts - ad_pts) / 90;
                if ( timems_diff > drop_threshold_ms) {
                    ALOGI("main and ad timems_diff %d ms  need drop ", timems_diff);
                    ad_status = AD_PACK_STATUS_DROP;
                } else if (timems_diff < hold_start_threshold_ms) {
                    if (is_dolby_format) {
                        ALOGI("main and ad timems_diff %d ms  need hold ", timems_diff);
                        ad_status = AD_PACK_STATUS_HOLD;
                    }
                } else {
                    if (is_aac_format) {
                       if (timems_diff > drop_start_threshold_ms) {
                            ad_status = AD_PACK_STATUS_DROP;
                        }
                    }
                }
            } else {
                timems_diff = (ad_pts - main_pts) / 90;
                if (timems_diff > hold_threshold_ms) {
                    ALOGI("ad ahead of main timems_diff %d ", timems_diff);
                    ad_status = AD_PACK_STATUS_HOLD;
                }
            }

            break;
        case AD_PACK_STATUS_DROP:
            if (main_pts > ad_pts) {
                timems_diff = (main_pts - ad_pts) / 90;
                if (timems_diff > drop_start_threshold_ms) {
                    ALOGI("main and ad timems_diff %d ms  need drop ", timems_diff);
                    ad_status = AD_PACK_STATUS_DROP;
                } else {
                    if (is_aac_format) {
                        if (timems_diff > hold_threshold_ms) {
                            ad_status = AD_PACK_STATUS_DROP;
                        } else {
                            ad_status = AD_PACK_STATUS_HOLD;
                        }
                    } else {
                        ad_status = AD_PACK_STATUS_HOLD;
                    }
                }
            } else {
                ad_status = AD_PACK_STATUS_HOLD;
            }
            break;

        case AD_PACK_STATUS_HOLD:

            if (main_pts < ad_pts) {
                timems_diff = (ad_pts - main_pts) / 90;
                if (timems_diff >= 0) {
                    ALOGI("ad ahead of main timems_diff %d ", timems_diff);
                }
                ad_status = AD_PACK_STATUS_HOLD;
            } else {
                timems_diff = (main_pts - ad_pts) / 90;
                if (timems_diff > hold_start_threshold_ms
                    && timems_diff < hold_threshold_ms) {
                    ad_status = AD_PACK_STATUS_NORMAL;
                } else if (timems_diff >= hold_threshold_ms) {
                    if (is_aac_format) {
                        if (timems_diff > drop_start_threshold_ms) {
                           ad_status = AD_PACK_STATUS_DROP;
                        } else {
                           ad_status = AD_PACK_STATUS_NORMAL;
                        }
                    } else {
                       ad_status = AD_PACK_STATUS_DROP;
                    }
                } else {
                    ad_status = AD_PACK_STATUS_HOLD;
                }
            }
            break;
        default:
            ALOGI("invalid status %d ", ad_status);

    }
    if (aml_dev->debug_flag) {
        ALOGI("main_pts %" PRId64 " ad_pts %" PRId64 " pre ad status %d now ad_status %d time_diff %d",
            main_pts, ad_pts, dtv_audio_info->ad_package_status, ad_status, timems_diff);
    }

    return ad_status;
}

void dtv_convert_language_to_string(int language_int, char *language_string)
{
   char *ptr = (char *)(&language_int);
   for (int i = 0; i < DVB_MEDIA_LANG_SIZE; i ++ ) {
          language_string[i] = ptr[DVB_MEDIA_LANG_SIZE - i -1];
   }
}

void dtv_audio_copy_raw_mute_frame(void *buffer, int raw_format) {

    switch (raw_format) {
       case AUDIO_FORMAT_AC3:
         memcpy(buffer, mute_dd_frame, sizeof(mute_dd_frame));
         break;
       case AUDIO_FORMAT_E_AC3:
         memcpy(buffer, mute_ddp_frame, sizeof(mute_ddp_frame));
         break;
       default:
         ALOGW("do not support the format %d",raw_format);
    }
}
const char* mediasyncAudiopolicyType2Str(audio_policy type)
{
    ENUM_TYPE_TO_STR_START("MEDIASYNC_AUDIO_");
    ENUM_TYPE_TO_STR(MEDIASYNC_AUDIO_NORMAL_OUTPUT)
    ENUM_TYPE_TO_STR(MEDIASYNC_AUDIO_DROP_PCM)
    ENUM_TYPE_TO_STR(MEDIASYNC_AUDIO_INSERT)
    ENUM_TYPE_TO_STR(MEDIASYNC_AUDIO_HOLD)
    ENUM_TYPE_TO_STR(MEDIASYNC_AUDIO_MUTE)
    ENUM_TYPE_TO_STR(MEDIASYNC_AUDIO_RESAMPLE)
    ENUM_TYPE_TO_STR(MEDIASYNC_AUDIO_ADJUST_CLOCK)
    ENUM_TYPE_TO_STR_END
}

const char* dtvAudioPatchCmd2Str(AUDIO_DTV_PATCH_CMD_TYPE type)
{
    ENUM_TYPE_TO_STR_START("AUDIO_DTV_PATCH_");
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_NULL)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_START)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_PAUSE)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_RESUME)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_STOP)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_AD_SUPPORT)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_VOLUME)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_MUTE)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_OUTPUT_MODE)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_PRE_GAIN)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_PRE_MUTE)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_OPEN)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_CLOSE)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_DEMUX_INFO)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_SECURITY_MEM_LEVEL)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_HAS_VIDEO)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_CONTROL)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_PID)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_FMT)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_AD_PID)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_AD_FMT)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_AD_ENABLE)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_AD_MIX_LEVEL)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_AD_VOL_LEVEL)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_MEDIA_SYNC_ID)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_MEDIA_PRESENTATION_ID)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_MEDIA_FIRST_LANG)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_MEDIA_SECOND_LANG)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_SPDIF_PROTECTION_MODE)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_ES_PTS_DTS_FLAG)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_SET_PLAYBACK_MODE)
    ENUM_TYPE_TO_STR(AUDIO_DTV_PATCH_CMD_NUM)
    ENUM_TYPE_TO_STR_END
}

audio_format_t aml_fmt_convert_to_android_fmt(int aml_fmt)
{

    switch (aml_fmt) {
        case ACODEC_FMT_AAC:
             return AUDIO_FORMAT_AAC;
        case ACODEC_FMT_AAC_LATM:
            return AUDIO_FORMAT_AAC_LATM;
        case ACODEC_FMT_AC3:
            return AUDIO_FORMAT_AC3;
        case ACODEC_FMT_EAC3:
            return AUDIO_FORMAT_E_AC3;
        case ACODEC_FMT_MPEG:
            return AUDIO_FORMAT_MP3;
        case ACODEC_FMT_MPEG1:
        case ACODEC_FMT_MPEG2:
            return AUDIO_FORMAT_MP2;
        case ACODEC_FMT_PCM_S16LE:
            return AUDIO_FORMAT_PCM;
        case ACODEC_FMT_AC4:
            return AUDIO_FORMAT_AC4;
        case ACODEC_FMT_DRA:
            return AUDIO_FORMAT_DRA;
        case ACODEC_FMT_NULL:
            return AUDIO_FORMAT_INVALID;
        default:
            return AUDIO_FORMAT_INVALID;
    }
}

bool non_dolby_format(int audio_format) {
    return !(audio_format == ACODEC_FMT_AC3 ||
            audio_format == ACODEC_FMT_EAC3 ||
            audio_format == ACODEC_FMT_AC4 ||
            audio_format == ACODEC_FMT_TRUEHD||
            audio_format == ACODEC_FMT_NULL);
}

void set_dtv_audio_clk_tuning(struct audio_hw_device *dev, int en)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;

    aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_DTV_CLK_TUNING, !!en);
}
void dtv_audio_set_spdif_protection_mode(int mode)
{
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    if (mode == SPDIF_PROTECTION__MODE_NEVER) {
        aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_SPDIF_B_OUT_CHANNEL_STATUS, SPDIF_PROTECTION_ENABLE);
        aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_SPDIF_OUT_CHANNEL_STATUS, SPDIF_PROTECTION_ENABLE);
    } else if (mode == SPDIF_PROTECTION__MODE_ONCE  || mode == SPDIF_PROTECTION__MODE_NONE){
        aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_SPDIF_B_OUT_CHANNEL_STATUS, SPDIF_PROTECTION_DISABLE);
        aml_mixer_ctrl_set_int(&aml_dev->alsa_mixer, AML_MIXER_ID_SPDIF_OUT_CHANNEL_STATUS, SPDIF_PROTECTION_DISABLE);
    } else {
        ALOGI("unsupport mode %d", mode);
    }
}
audio_dual_mono_mode_t convert2_android_dual_mono_mode(AM_AOUT_OutputMode_t mode)
{
    audio_dual_mono_mode_t android_mode = AUDIO_DUAL_MONO_MODE_OFF;
    switch (mode) {
    case AM_AOUT_OUTPUT_DUAL_LEFT:
        android_mode = AUDIO_DUAL_MONO_MODE_LL;
        break;
    case AM_AOUT_OUTPUT_DUAL_RIGHT:
        android_mode = AUDIO_DUAL_MONO_MODE_RR;
        break;
    case AM_AOUT_OUTPUT_LRMIX:
        android_mode = AUDIO_DUAL_MONO_MODE_LR;
        break;
    case AM_AOUT_OUTPUT_STEREO:
        android_mode = AUDIO_DUAL_MONO_MODE_OFF;
        break;
    default :
        ALOGI("%s do not support mode %d",__FUNCTION__, mode);
        break;
    }
    return android_mode;
}
AM_AOUT_OutputMode_t convert2_aml_dual_mono_mode(audio_dual_mono_mode_t mode)
{
    AM_AOUT_OutputMode_t aml_mode = AM_AOUT_OUTPUT_STEREO;
    switch (mode) {
    case AUDIO_DUAL_MONO_MODE_LL:
        aml_mode = AM_AOUT_OUTPUT_DUAL_LEFT;
        break;
    case AUDIO_DUAL_MONO_MODE_RR:
        aml_mode = AM_AOUT_OUTPUT_DUAL_RIGHT;
        break;
    case AUDIO_DUAL_MONO_MODE_LR:
        aml_mode = AM_AOUT_OUTPUT_LRMIX;
        break;
    case AUDIO_DUAL_MONO_MODE_OFF:
        aml_mode = AM_AOUT_OUTPUT_STEREO;
        break;
    default :
        ALOGI("%s do not support mode %d",__FUNCTION__, mode);
        break;
    }
    return aml_mode;
}
