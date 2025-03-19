/*
 * Copyright (C) 2010 Amlogic Corporation.
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



#define LOG_TAG "audio_hw_hal_hwsync"
//#define LOG_NDEBUG 0
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <cutils/log.h>
#include <string.h>
#include <sys/utsname.h>
#include <cutils/properties.h>
#include "audio_hw_utils.h"
#include "audio_hwsync.h"
#include "audio_hw.h"
#include "dolby_lib_api.h"
#include "audio_hwsync_wrap.h"
#include "aml_audio_ms12_sync.h"
#include "aml_audio_timer.h"
#include "audio_hw_ms12.h"
#include "audio_mediasync_wrap.h"
#include "audio_hwsync_wrap.h"
#include "audio_tsync_wrap.h"

static int aml_audio_get_hwsync_flag()
{
    int debug_flag = 0;
    debug_flag = get_debug_value(AML_DEBUG_AUDIOHAL_HW_SYNC);
    return debug_flag;
}

void aml_mediasync_init(void *pSyncHandle)
{
    media_sync_info_t *pMediaSyncTable = (media_sync_info_t *)pSyncHandle;
    int index = 0;
    for (index = 0; index < HW_SYNC_MAX; ++index) {
        pMediaSyncTable[index].handle = NULL;
        pMediaSyncTable[index].id = -1;
    }
}

void aml_add_mediasync_info(void *pSyncHandle, void *pItem, int32_t id)
{
    media_sync_info_t *pMediaSyncTable = (media_sync_info_t *)pSyncHandle;
    int index = 0;
    for (index = 0; index < HW_SYNC_MAX; ++index) {
        if (pMediaSyncTable[index].handle == NULL) {
            pMediaSyncTable[index].handle = pItem;
            pMediaSyncTable[index].id = id;
            pMediaSyncTable[index].ref_count = 0;
            break;
        }
    }
    if (index >= HW_SYNC_MAX) {
        AM_LOGW("no enough space to store mediasync info in MediaSyncTable");
    }
}

// should be used with adev->mediasync_lock
// return the media sync item ref_count
int aml_remove_mediasync_info(void *pSyncHandle, void *pItem)
{
    media_sync_info_t *pMediaSyncTable = (media_sync_info_t *)pSyncHandle;
    int index = 0;
    int ref_count = 0;
    for (index = 0; index < HW_SYNC_MAX; ++index) {
        if (pMediaSyncTable[index].handle == pItem) {
            pMediaSyncTable[index].ref_count--;
            ref_count = pMediaSyncTable[index].ref_count;
            if (ref_count <= 0) {
                pMediaSyncTable[index].handle = NULL;
                pMediaSyncTable[index].id = -1;
            }
            break;
        }
    }
    return ref_count;
}

// should be used with adev->mediasync_lock
void aml_add_mediasync_ref_count(void *pSyncHandle, int32_t id)
{
    media_sync_info_t *pMediaSyncTable = (media_sync_info_t *)pSyncHandle;
    int index = 0;
    for (index = 0; index < HW_SYNC_MAX; ++index) {
        if (pMediaSyncTable[index].id == id) {
            pMediaSyncTable[index].ref_count++;
            break;
        }
    }
}

void *aml_lookup_mediasync_handle(void *pSyncHandle, int32_t id)
{
    media_sync_info_t *pMediaSyncTable = (media_sync_info_t *)pSyncHandle;
    int index = 0;
    void *pHandle = NULL;
    for (index = 0; index < HW_SYNC_MAX; ++index) {
        if (pMediaSyncTable[index].id == id) {
            pHandle = pMediaSyncTable[index].handle;
            break;
        }
    }

    return pHandle;
}


void aml_audio_hwsync_init(audio_hwsync_t *p_hwsync, struct aml_stream_out  *out)
{
    ALOGI("%s p_hwsync %p out %p\n", __func__, p_hwsync, out);
    int fd = -1;
    if (p_hwsync == NULL) {
        return;
    }
    p_hwsync->first_apts_flag = false;
    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
    p_hwsync->hw_sync_header_cnt = 0;
    p_hwsync->hw_sync_frame_size = 0;
    p_hwsync->bvariable_frame_size = 0;
    p_hwsync->version_num = 0;
    p_hwsync->wait_video_done = false;
    p_hwsync->play_rate = 0.0f;

    memset(p_hwsync->pts_tab, 0, sizeof(apts_tab_t)*HWSYNC_APTS_NUM);
    pthread_mutex_init(&p_hwsync->lock, NULL);
    p_hwsync->payload_offset = 0;
    p_hwsync->aout = out;

    if (p_hwsync->tsync_fd < 0 && p_hwsync->hwsync_id == 12345678) {
        fd = open(TSYNC_PCRSCR, O_RDONLY);
        p_hwsync->tsync_fd = fd;
        ALOGI("%s open tsync fd %d", __func__, fd);
    }

    out->tsync_status = TSYNC_STATUS_INIT;
    clock_gettime(CLOCK_MONOTONIC, &p_hwsync->last_hwsync_timestamp);
    ALOGI("%s done", __func__);
    return;
}

//return bytes cost from input,
int aml_audio_hwsync_find_frame(audio_hwsync_t *p_hwsync,
        const void *in_buffer, size_t in_bytes, uint64_t *cur_pts, int *outsize)
{
    size_t remain = in_bytes;
    uint8_t *p = (uint8_t *)in_buffer;
    uint64_t time_diff = 0;
    int pts_found = 0;
    if (p_hwsync == NULL || in_buffer == NULL) {
        ALOGE("p_hwsync or in_buffer is NULL");
        return 0;
    }
    struct aml_audio_device *adev = p_hwsync->aout->dev;
    size_t  v2_hwsync_header = HW_AVSYNC_HEADER_SIZE_V2;
    int debug_enable = aml_audio_get_hwsync_flag();
    mediasync_wrap_set_debug(debug_enable);

    //ALOGI(" --- out_write %d, cache cnt = %d, body = %d, hw_sync_state = %d", out_frames * frame_size, out->body_align_cnt, out->hw_sync_body_cnt, out->hw_sync_state);
    while (remain > 0) {

        if (p_hwsync->hw_sync_state == HW_SYNC_STATE_HEADER) {
            ALOGV("Add to header buffer [%zu], 0x%x", p_hwsync->hw_sync_header_cnt, *p);
            p_hwsync->hw_sync_header[p_hwsync->hw_sync_header_cnt++] = *p++;
            remain--;
            if (p_hwsync->hw_sync_header_cnt == HW_SYNC_VERSION_SIZE ) {
                if (!hwsync_header_valid(&p_hwsync->hw_sync_header[0])) {
                    ALOGV("!!!!!!hwsync header out of sync! Resync.should not happen????");
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                    memmove(p_hwsync->hw_sync_header, p_hwsync->hw_sync_header + 1, HW_SYNC_VERSION_SIZE - 1);
                    p_hwsync->hw_sync_header_cnt--;
                    continue;
                }
                p_hwsync->version_num = p_hwsync->hw_sync_header[3];
                if (p_hwsync->version_num == 1 || p_hwsync->version_num == 2 || p_hwsync->version_num == 3) {
                } else  {
                    ALOGI("invalid hwsync version num %d",p_hwsync->version_num);
                }
            }
            if ((p_hwsync->version_num  == 1 && p_hwsync->hw_sync_header_cnt == HW_AVSYNC_HEADER_SIZE_V1 ) ||
                (p_hwsync->version_num  == 2 && p_hwsync->hw_sync_header_cnt == v2_hwsync_header ) ||
                (p_hwsync->version_num  == 3 && p_hwsync->hw_sync_header_cnt == HW_AVSYNC_HEADER_SIZE_V3)) {
                uint64_t pts = 0;
                uint64_t pts_us = 0;

                if (p_hwsync->version_num  == 2 && p_hwsync->hw_sync_header_cnt == HW_AVSYNC_HEADER_SIZE_V2) {
                    v2_hwsync_header = hwsync_header_get_offset(&p_hwsync->hw_sync_header[0]);
                    if (v2_hwsync_header > HW_AVSYNC_MAX_HEADER_SIZE) {
                        ALOGE("buffer overwrite, check the header size %zu \n",v2_hwsync_header);
                        break;
                    }
                    if (v2_hwsync_header > p_hwsync->hw_sync_header_cnt) {
                        ALOGV("need skip more sync header, %zu\n",v2_hwsync_header);
                        continue;
                    }
                } else if ((p_hwsync->version_num  == 3 && p_hwsync->hw_sync_header_cnt == HW_AVSYNC_HEADER_SIZE_V3)) {
                    //2 bytes flags
                    //Timestamp present
                    //Metadata present
                    //Payload present
                    //Payload contains access unit start
                    //Error was encountered in demux or transmission. (MPEG2 transport stream has this)
                    bool timestamp_present = false,metadata_present = false, payload_present = false;
                    p_hwsync->header_flags = hwsync_header_get_flags(&p_hwsync->hw_sync_header[0]);
                    timestamp_present = (p_hwsync->header_flags & HW_AVSYNC_FLAG_TIMESTAMP_PRESENT);
                    metadata_present = (p_hwsync->header_flags & HW_AVSYNC_FLAG_METADATA_PRESENT);
                    payload_present = (p_hwsync->header_flags & HW_AVSYNC_FLAG_PAYLOAD_PRESENT);
                    ALOGV("p_hwsync->version_num %d timestamp_present %d metadata_present %d payload_present %d",
                        p_hwsync->version_num, timestamp_present, metadata_present, payload_present);
                }

                if ((in_bytes - remain) > p_hwsync->hw_sync_header_cnt) {
                    ALOGI("got the frame sync header cost %zu", in_bytes - remain);
                }

                p_hwsync->hw_sync_body_cnt = hwsync_header_get_size(&p_hwsync->hw_sync_header[0]);
                if (p_hwsync->version_num  == 3) {
                    ALOGV("p_hwsync->hw_sync_body_cnt %d",p_hwsync->hw_sync_body_cnt);
                    p_hwsync->hw_sync_body_cnt = p_hwsync->hw_sync_body_cnt - HW_AVSYNC_HEADER_SIZE_V3;
                }
                if (p_hwsync->hw_sync_body_cnt > HWSYNC_MAX_BODY_SIZE) {
                    ALOGE("body max size =%d hw_sync_body_cnt =%d", HWSYNC_MAX_BODY_SIZE, p_hwsync->hw_sync_body_cnt);
                    p_hwsync->hw_sync_body_cnt = HWSYNC_MAX_BODY_SIZE;
                }

                if (p_hwsync->hw_sync_frame_size && p_hwsync->hw_sync_body_cnt) {
                    if (p_hwsync->hw_sync_frame_size != p_hwsync->hw_sync_body_cnt) {
                        p_hwsync->bvariable_frame_size = 1;
                        ALOGV("old frame size=%d new=%d", p_hwsync->hw_sync_frame_size, p_hwsync->hw_sync_body_cnt);
                    }
                }

                if (p_hwsync->version_num  == 3)  {
                   if (p_hwsync->header_flags & HW_AVSYNC_FLAG_METADATA_PRESENT) {
                       p_hwsync->hw_sync_state = HW_SYNC_STATE_METADATA_HEADER;
                   } else if (p_hwsync->header_flags & HW_AVSYNC_FLAG_PAYLOAD_PRESENT) {
                       p_hwsync->hw_sync_state = HW_SYNC_STATE_PAYLOAD;
                   } else {
                       ALOGV("no metadata  no payload");
                       p_hwsync->hw_sync_state = HW_SYNC_STATE_PAYLOAD;
                   }
                } else {
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_PAYLOAD;
                }
                p_hwsync->hw_sync_frame_size = p_hwsync->hw_sync_body_cnt;
                p_hwsync->body_align_cnt = 0; //  alisan zz
                p_hwsync->hw_sync_header_cnt = 0; //8.1
                if (p_hwsync->version_num == 3) {
                     if (!(p_hwsync->header_flags & HW_AVSYNC_FLAG_TIMESTAMP_PRESENT)) {
                        ALOGV("version_3 no timestamp_present");
                     } else {
                        pts = hwsync_header_get_pts_v3(&p_hwsync->hw_sync_header[0]);
                     }
                } else {
                     pts = hwsync_header_get_pts(&p_hwsync->hw_sync_header[0]);
                }
                /* do this covert to avoid pts overflow */
                /*convert ns to us*/
                pts_us = pts / 1000;
                /*convert us to 90k*/
                pts = pts_us * 90 / 1000;
                time_diff = get_pts_gap(pts, p_hwsync->last_apts_from_header) / 90;
                p_hwsync->pts_gap = time_diff;
                {
                    struct timespec current_timestamp;
                    clock_gettime(CLOCK_MONOTONIC, &current_timestamp);
                    int64_t time_diff = calc_time_interval_us(&p_hwsync->last_hwsync_timestamp, &current_timestamp);
                    if (time_diff >= (TIME_DIFF_THRESHOLD * USEC_PER_SEC) ||
                        p_hwsync->pts_gap > 100 ||
                        pts <= p_hwsync->last_apts_from_header) {
                        if (debug_enable)
                            ALOGI("[hwsync:%p]tunnel time_diff[%"PRIu64"]us frame_body_size[%d]bytes pts_info[%"PRIu64" - %"PRIu64"]ms pts_gap[%"PRIu64"]ms", p_hwsync, time_diff, p_hwsync->hw_sync_frame_size, pts / 90, p_hwsync->last_apts_from_header / 90, p_hwsync->pts_gap);
                        p_hwsync->last_hwsync_timestamp = current_timestamp;
                    }
                }
                if (debug_enable) {
                    ALOGI("pts 0x%"PRIx64",frame len %u\n", pts, p_hwsync->hw_sync_body_cnt);
                    ALOGI("last pts 0x%"PRIx64",diff %" PRId64 " ms\n", p_hwsync->last_apts_from_header, time_diff);
                }
                if (p_hwsync->hw_sync_frame_size > HWSYNC_MAX_BODY_SIZE) {
                    ALOGE("hwsync frame body %d bigger than pre-defined size %d, need check !!!!!\n",
                                p_hwsync->hw_sync_frame_size,HWSYNC_MAX_BODY_SIZE);
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                    return 0;
                }

                if (time_diff > 32) {
                    ALOGV("pts  time gap %"PRIx64" ms,last %"PRIx64",cur %"PRIx64"\n", time_diff,
                          p_hwsync->last_apts_from_header, pts);
                }
                p_hwsync->last_apts_from_header = pts;
                *cur_pts = pts;
                pts_found = 1;
                //ALOGI("get header body_cnt = %d, pts = %lld", out->hw_sync_body_cnt, pts);

            }
            continue;
        } else if (p_hwsync->hw_sync_state == HW_SYNC_STATE_PAYLOAD) {

            if (p_hwsync->version_num  == 3) {
                if (p_hwsync->header_flags & HW_AVSYNC_FLAG_PAYLOAD_PRESENT) {
                    if (p_hwsync->hw_sync_payload_header_cnt == HW_AVSYNC_PAYLOAD_HEADER_SIZE) {
                         p_hwsync->hw_sync_payload_unit_size = hwsync_payload_get_size(p_hwsync->hw_sync_payload_header);
                         ALOGV("hw_sync_payload_unit_size %d",p_hwsync->hw_sync_payload_unit_size);
                         if (p_hwsync->hw_sync_payload_unit_size == 0) {
                             ALOGV("!!!!!!hwsync header out of sync! Resync.should not happen????");
                             p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                             continue;
                         } else {
                             ALOGI("metadata_total size %d payload_unit_size %d  hw_sync_frame_size %d",
                                 p_hwsync->hw_sync_metadata_total_size,p_hwsync->hw_sync_payload_unit_size,p_hwsync->hw_sync_frame_size);
                             if (p_hwsync->hw_sync_frame_size - HW_AVSYNC_HEADER_SIZE_V3 == (p_hwsync->hw_sync_metadata_total_size + p_hwsync->hw_sync_payload_unit_size)) {
                                 ALOGV("matched and now to read payload data ");
                             } else {
                                 ALOGV("possible not matched,pls check more !!!");
                             }
                         }
                         p_hwsync->hw_sync_encapsulation_mode = hwsync_payload_get_encapsulation_mode(p_hwsync->hw_sync_payload_header);
                         ALOGV("hw_sync_encapsulation_mode %d ",p_hwsync->hw_sync_encapsulation_mode);
                         p_hwsync->hw_sync_payload_unit_cnt = p_hwsync->hw_sync_payload_unit_size - HW_AVSYNC_PAYLOAD_HEADER_SIZE;
                         if (p_hwsync->hw_sync_encapsulation_mode >  AUDIO_ENCAPSULATION_MODE_HANDLE) {
                             ALOGV("!!!!!!hwsync header out of sync! Resync.should not happen????");
                             p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                             continue;
                         } else {
                             int cpy_bytes = (p_hwsync->hw_sync_payload_unit_cnt < remain) ? p_hwsync->hw_sync_payload_unit_cnt : remain;
                             // process m bytes body with an empty fragment for alignment
                             if (cpy_bytes > 0) {
                                 memcpy(p_hwsync->hw_sync_body_buf + p_hwsync->hw_sync_payload_unit_size - HW_AVSYNC_PAYLOAD_HEADER_SIZE - p_hwsync->hw_sync_payload_unit_cnt, p, cpy_bytes);
                                 p += cpy_bytes;
                                 remain -= cpy_bytes;
                                 p_hwsync->hw_sync_payload_unit_cnt -= cpy_bytes;
                                 if (p_hwsync->hw_sync_payload_unit_cnt == 0) {
                                     p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                                     p_hwsync->hw_sync_header_cnt = 0;
                                     *outsize = p_hwsync->hw_sync_payload_unit_size - HW_AVSYNC_PAYLOAD_HEADER_SIZE;
                                     /*
                                     sometimes the audioflinger burst size is smaller than hwsync payload
                                     we need use the last found pts when got a complete hwsync payload
                                     */
                                     if (!pts_found) {
                                         *cur_pts = p_hwsync->last_apts_from_header;
                                     }
                                     if (debug_enable) {
                                         ALOGV("we found the frame total body,yeah\n");
                                     }
                                     break;//continue;
                                 }
                             }
                         }
                    } else {
                         p_hwsync->hw_sync_payload_header[p_hwsync->hw_sync_payload_header_cnt++] = *p++;
                         remain--;
                         continue;
                    }
                } else {
                    int m = (p_hwsync->hw_sync_body_cnt < remain) ? p_hwsync->hw_sync_body_cnt : remain;
                    if (m  > 0) {
                        p += m;
                        remain -= m;
                        p_hwsync->hw_sync_body_cnt -= m;
                        if (p_hwsync->hw_sync_body_cnt == 0) {
                            p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                            p_hwsync->hw_sync_header_cnt = 0;

                            if (debug_enable) {
                                ALOGV("we found the frame total body,yeah\n");
                            }
                            break;//continue;
                        }
                    }
                }
            } else {
                int m = (p_hwsync->hw_sync_body_cnt < remain) ? p_hwsync->hw_sync_body_cnt : remain;
                p_hwsync->aout->hwsync_parsed_frames_sum += m/p_hwsync->aout->hal_frame_size;
                // process m bytes body with an empty fragment for alignment
                if (m  > 0) {
                    memcpy(p_hwsync->hw_sync_body_buf + p_hwsync->hw_sync_frame_size - p_hwsync->hw_sync_body_cnt, p, m);
                    p += m;
                    remain -= m;
                    p_hwsync->hw_sync_body_cnt -= m;
                    if (p_hwsync->hw_sync_body_cnt == 0) {
                        p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                        p_hwsync->hw_sync_header_cnt = 0;
                        *outsize = p_hwsync->hw_sync_frame_size;
                        /*
                        sometimes the audioflinger burst size is smaller than hwsync payload
                        we need use the last found pts when got a complete hwsync payload
                        */
                        if (!pts_found) {
                            *cur_pts = p_hwsync->last_apts_from_header;
                        }
                        if (debug_enable) {
                            ALOGV("we found the frame total body,yeah\n");
                        }
                        break;//continue;
                    }
                }
            }
        } else if (p_hwsync->hw_sync_state == HW_SYNC_STATE_METADATA_HEADER) {
            if (p_hwsync->hw_sync_metadata_header_cnt == HW_AVSYNC_METADATA_HEADER_SIZE) {
                p_hwsync->hw_sync_metadata_total_size = hwsync_metadata_get_size(p_hwsync->hw_sync_metadata_header);
                if (p_hwsync->hw_sync_metadata_total_size == 0) {
                    ALOGE("HW_SYNC_STATE_METADATA_HEADER  out of sync! Resync.should not happen????");
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                    continue;
                }
                p_hwsync->hw_sync_metadata_units_cnt = hwsync_metadata_get_units_cnt(p_hwsync->hw_sync_metadata_header);
                ALOGI("hw_sync_metadata_total_size %d hw_sync_metadata_units_cnt %d",p_hwsync->hw_sync_metadata_total_size,p_hwsync->hw_sync_metadata_units_cnt);
                if (p_hwsync->hw_sync_metadata_units_cnt == 0) {
                    ALOGE("HW_SYNC_STATE_METADATA_HEADER Resync.should not happen????");
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                    continue;
                } else {
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_METADATA_UNITS;
                    p_hwsync->hw_sync_metadata_size_cnt = p_hwsync->hw_sync_metadata_total_size - HW_AVSYNC_METADATA_HEADER_SIZE;
                    p_hwsync->hw_sync_metadata_header_cnt = 0;
                    continue;
                }
            } else {
                p_hwsync->hw_sync_metadata_header[p_hwsync->hw_sync_metadata_header_cnt++] = *p++;
                remain--;
                continue;
            }

        }else if (p_hwsync->hw_sync_state == HW_SYNC_STATE_METADATA_UNITS) {

            if (p_hwsync->hw_sync_metadata_unit_header_cnt == HW_AVSYNC_METADATA_UNIT_HEADER_SIZE) {
                p_hwsync->hw_sync_metadata_unit_type = hwsync_metadata_get_unit_type(p_hwsync->hw_sync_metadata_unit_header);
                p_hwsync->hw_sync_metadata_unit_size = hwsync_metadata_get_unit_size(p_hwsync->hw_sync_metadata_unit_header);
                p_hwsync->hw_sync_metadata_unit_size_cnt = p_hwsync->hw_sync_metadata_unit_size - HW_AVSYNC_METADATA_UNIT_HEADER_SIZE;
                ALOGI("hw_sync_metadata_unit_type %0x p_hwsync->hw_sync_metadata_unit_size %d",
                    p_hwsync->hw_sync_metadata_unit_type,p_hwsync->hw_sync_metadata_unit_size);
                if (p_hwsync->hw_sync_metadata_unit_type == AUDIO_ENCAPSULATION_METADATA_TYPE_FRAMEWORK_TUNER) {
                    ALOGI("ENCAPSULATION_METADATA_TYPE_FRAMEWORK_TUNER");
                    if (p_hwsync->hw_sync_metadata_unit_size_cnt != HW_AVSYNC_METADATA_UNIT_TUNER_SIZE)  {
                        ALOGE("p_hwsync->hw_sync_metadata_unit_size %d ？？？",p_hwsync->hw_sync_metadata_unit_size_cnt);
                    }
                    if (remain < p_hwsync->hw_sync_metadata_unit_size_cnt) {
                        break;
                    } else {
                        uint8_t *metadata_unit_buf = p;
                        hw_avsync_metadata_unit_info_t *metadata_unit = &p_hwsync->current_metadata_unit;
                        hwsync_metadata_get_unit_info(metadata_unit_buf,metadata_unit);
                        ALOGI("metadata_unit->stream_id %d metadata_unit->flags %d metadata_unit->broadcast_type %d",
                            metadata_unit->stream_id,metadata_unit->flags,metadata_unit->broadcast_type);
                    }
                } else if (p_hwsync->hw_sync_metadata_unit_type == ENCAPSULATION_METADATA_TYPE_AD_PLACEMENT) {

                        ALOGI("ENCAPSULATION_METADATA_TYPE_AD_PLACEMENT");
                        p_hwsync->hw_sync_metadata_placement = hwsync_metadata_get_placement(p);
                        ALOGI(" p_hwsync->hw_sync_metadata_placement %d", p_hwsync->hw_sync_metadata_placement);

                } else if (p_hwsync->hw_sync_metadata_unit_type == AUDIO_ENCAPSULATION_METADATA_TYPE_DVB_AD_DESCRIPTOR) {
                   if (remain > HW_AVSYNC_METADATA_UNIT_DVB_AD_SIZE) {
                        ALOGI("AUDIO_ENCAPSULATION_METADATA_TYPE_DVB_AD_DESCRIPTOR");
                        if (p_hwsync->hw_sync_metadata_unit_size_cnt != HW_AVSYNC_METADATA_UNIT_DVB_AD_SIZE) {
                             ALOGE("p_hwsync->hw_sync_metadata_unit_size_cnt %d ？？？",p_hwsync->hw_sync_metadata_unit_size_cnt);
                        }
                        uint8_t *metadata_ad_info_buf = p;
                        hw_avsync_metadata_dvb_ad_t *metadata_ad = &p_hwsync->metadata_dvb_ad_info;
                        hwsync_metadata_get_ad_info(metadata_ad_info_buf,metadata_ad);
                    } else {
                        break;
                    }

                } else {
                    ALOGE("HW_SYNC_STATE_METADATA_UNITS out of sync! Resync.should not happen????");
                    p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                    continue;
                }
                remain -= p_hwsync->hw_sync_metadata_unit_size_cnt;
                p += p_hwsync->hw_sync_metadata_unit_size_cnt;
                p_hwsync->hw_sync_metadata_units_cnt--;
                p_hwsync->hw_sync_metadata_size_cnt -= (p_hwsync->hw_sync_metadata_unit_size_cnt + HW_AVSYNC_METADATA_UNIT_HEADER_SIZE);
                p_hwsync->hw_sync_metadata_unit_header_cnt = 0;
                ALOGI("hw_sync_metadata_units_cnt %d hw_sync_metadata_size_cnt %d", p_hwsync->hw_sync_metadata_units_cnt, p_hwsync->hw_sync_metadata_size_cnt);
                if (p_hwsync->hw_sync_metadata_units_cnt == 0) {
                    if (p_hwsync->hw_sync_body_cnt ==  p_hwsync->hw_sync_metadata_total_size) {
                        p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                        p_hwsync->hw_sync_body_cnt = 0;
                        ALOGI("remain %zu matched ,continue to header!!!", remain);
                    } else {
                        if (p_hwsync->header_flags & HW_AVSYNC_FLAG_PAYLOAD_PRESENT) {
                            p_hwsync->hw_sync_state = HW_SYNC_STATE_PAYLOAD;
                            ALOGI("metadata_units_cnt and metadata_size_cnt matched ,continue to payload !!!");
                        } else {
                            p_hwsync->hw_sync_state = HW_SYNC_STATE_HEADER;
                            p_hwsync->hw_sync_body_cnt = 0;
                        }
                    }
                    p_hwsync->hw_sync_metadata_unit_header_cnt = 0;
                    break;
                } else {
                    break;
                }
            }  else {
                p_hwsync->hw_sync_metadata_unit_header[p_hwsync->hw_sync_metadata_unit_header_cnt++] = *p++;
                remain--;
            }
        }
    }
    return in_bytes - remain;
}

int aml_audio_hwsync_set_first_pts(audio_hwsync_t *p_hwsync, uint64_t pts)
{
    ALOGI("%s", __func__);
    char tempbuf[128];
    int vframe_ready_cnt = 0;
    int delay_count = 0;

    if (p_hwsync == NULL) {
        return -1;
    }

    p_hwsync->first_apts_flag = true;
    p_hwsync->first_apts = pts;

    if (aml_hwsync_wrap_set_start_pts64(p_hwsync, pts) < 0)
        return -EINVAL;
    p_hwsync->aout->tsync_status = TSYNC_STATUS_RUNNING;
    return 0;
}

/*
@offset :ms12 real costed offset
@p_adjust_ms: a/v adjust ms.if return a minus,means
 audio slow,need skip,need slow.return a plus value,means audio quick,need insert zero.
*/
int aml_audio_hwsync_audio_process(audio_hwsync_t *p_hwsync, uint64_t apts, int *p_adjust_ms)
{
    int ret = 0;
    *p_adjust_ms = 0;
    uint64_t pcr = 0;
    uint gap = 0;
    int gap_ms = 0;
    int debug_enable = 0;
    char tempbuf[32] = {0};
    struct aml_audio_device *adev = NULL;
    int latency_frames = 0;
    struct audio_stream_out *stream = NULL;
    struct dolby_ms12_dec_desc *ms12_dec = NULL;
    int latency_pts = 0;

    // add protection to avoid NULL pointer.
    if (p_hwsync == NULL) {
        ALOGE("%s,p_hwsync == NULL", __func__);
        return 0;
    }
    struct aml_stream_out  *out = p_hwsync->aout;
    struct timespec ts;
    int pcr_pts_gap = 0;
    int alsa_pcm_delay_frames = 0;
    int alsa_bitstream_delay_frames = 0;
    int ms12_pipeline_delay_frames = 0;
    bool amaster_mode = true;
    aml_stream_speed_info_t *speed_info = NULL;

    ALOGV("%s,================", __func__);

    if (p_hwsync->aout == NULL) {
        ALOGE("%s,p_hwsync->aout == NULL", __func__);
        return 0;
    } else {
        adev = p_hwsync->aout->dev;
        ms12_dec = out->ms12_dec_handle;
        if (adev == NULL) {
            ALOGE("%s,adev == NULL", __func__);
            return 0;
        } else {
            debug_enable = aml_audio_get_hwsync_flag();
            mediasync_wrap_set_debug(debug_enable);
        }
    }

    aml_audio_hwsync_update_threshold(p_hwsync);

    if (ret) {
        ALOGE("%s lookup failed", __func__);
        return 0;
    }

    /*get MS12 pipe line delay + alsa delay*/
    stream = (struct audio_stream_out *)p_hwsync->aout;
    if (!stream) {
        ALOGE("%s,stream == NULL", __func__);
        return 0;
    }
    if (adev && (eDolbyMS12Lib == adev->dolby_lib_type)) {
        latency_frames = aml_audio_get_ms12_tunnel_latency(stream);
        alsa_pcm_delay_frames = out_get_ms12_latency_frames(stream);
        alsa_bitstream_delay_frames = out_get_ms12_bitstream_latency_ms(stream) * 48;
        ms12_pipeline_delay_frames = dolby_ms12_main_pipeline_latency_frames(stream);
    } else {
        latency_frames = (int32_t)out_get_latency_frames(stream);
    }
    latency_pts = latency_frames / 48 * 90;
    speed_info = &out->speed_info;

    if (p_hwsync->use_mediasync) {
        uint64_t apts64 = 0;
        if (p_hwsync->first_apts_flag == false && (apts >= abs(latency_pts) || latency_pts <= 0)) {

            ALOGI("%s apts =%" PRIx64 "", __func__, apts);
            ALOGI("%s alsa pcm delay =%d bitstream delay =%d pipeline =%d", __func__, alsa_pcm_delay_frames, alsa_bitstream_delay_frames, ms12_pipeline_delay_frames);
            ALOGI("%s apts = 0x%" PRIx64 " (%" PRIu64 " ms) latency=0x%x (%d ms)", __func__, apts, apts / 90, latency_pts, latency_pts/90);
            ALOGI("%s aml_audio_hwsync_set_first_pts = 0x%" PRIx64 " (%" PRIu64 " ms)", __func__, apts - latency_pts, (apts - latency_pts)/90);
            apts64 = apts - latency_pts;
            /*if the pts is zero, to avoid video pcr not set issue, we just set it as 1ms*/
            if ((apts64 / 90) == 0) {
                apts64 = 1 * 90;
            }
            aml_audio_hwsync_set_first_pts(out->hwsync, apts64);
            /*the wait function sometime causes too much time which causes audio break*/
            //aml_hwsync_wait_video_drop(out->hwsync, apts32);
            aml_hwsync_wrap_reset_pcrscr_speed(out->hwsync, apts64, speed_info->speed, true);
        } else  if (p_hwsync->first_apts_flag) {
            if (apts >= abs(latency_pts) || latency_pts <= 0) {
                //apts -= latency_pts;
                apts64 = apts - latency_pts;
            } else {
                ALOGE("wrong PTS =0x%" PRIx64 " delay pts=%d",apts, latency_pts);
                return 0;
            }
            /*if the pts is zero, to avoid video pcr not set issue, we just set it as 1ms*/
            if (apts64 == 0) {
                apts64 = 1 * 90;
            }

            clock_gettime(CLOCK_REALTIME, &ts);
            ret = aml_hwsync_wrap_get_pts(out->hwsync, &pcr);
            pcr_pts_gap = ((int)(apts64 - pcr)) / 90;
            gap = pcr_pts_gap * 90;
            /*resume from pause status, we can sync it exactly*/
            if (ms12_dec && ms12_dec->need_resync) {
                ms12_dec->need_resync = false;
                aml_audio_hwsync_set_first_pts(out->hwsync, apts64);
                ALOGI("%s resync pcr_pts_gap %d ms\n", __func__, pcr_pts_gap);
            }

            if (adev && adev->continuous_audio_mode && (out->write_status == false)) {
                // ms12 continuous mode, stream just resume and not ready for write
                //ALOGI("%s : continuous mode, waiting stream[%p] write_status to be true", __func__, out);
            } else {
                float speed_select = speed_info->speed;
                bool force_update = false;
                aml_audio_speed_post_delay_t *p_post_delay = &speed_info->post_delay;

                if (p_post_delay->transitioning || !is_float_equal(p_post_delay->next_speed, speed_info->speed)) {
                    speed_select = p_post_delay->last_speed;
                } else if (speed_info->hwsync_force_update) {
                    if (out->hwsync->last_output_pts && out->hwsync->last_output_pts != ULLONG_MAX) {
                        // Make sure : different speed has different apts value.
                        uint64_t mini_apts64 = out->hwsync->last_output_pts + 90;
                        if (apts64 < mini_apts64) {
                            AM_LOGI("apts64 change %" PRIu64 " to %" PRIu64 "", apts64, mini_apts64);
                            apts64 = mini_apts64;
                        }
                    }
                    force_update = true;
                    speed_info->hwsync_force_update = false;
                    pcr = apts64;
                    pcr_pts_gap = gap = 0;
                }
                aml_hwsync_wrap_reset_pcrscr_speed(out->hwsync, apts64, speed_select, force_update);
            }

            {
                int pts_gap = ((int)apts64 - (int)out->hwsync->last_output_pts)/ 90;
                int time_gap = (int)calc_time_interval_us(&out->hwsync->last_timestamp, &ts) / 1000;

                if (debug_enable || abs(pcr_pts_gap) > 20) {
                    ALOGI("%s apts =%#" PRIx64 " %" PRIu64 " ms", __func__, apts, apts/90);
                    ALOGI("%s alsa pcm delay =%d bitstream delay =%d pipeline =%d total =%d", __func__,
                        alsa_pcm_delay_frames,
                        alsa_bitstream_delay_frames,
                        ms12_pipeline_delay_frames,
                        latency_frames);
                    ALOGI("%s pcr =%" PRIu64 " ms pts =0x%" PRIx64 " %" PRIu64 " ms gap =%d ms", __func__, pcr / 90, apts64, apts64/90, pcr_pts_gap);
                    ALOGI("latency_frames =%d ms=%d", latency_frames, latency_frames / 48);
                    ALOGI("pts last =0x%" PRIx64 " now =0x%" PRIx64 " diff =%d ms time diff =%d ms jitter =%d ms",
                        out->hwsync->last_output_pts, apts64, pts_gap, time_gap, pts_gap - time_gap);
                }
            }
            out->hwsync->last_output_pts = apts64;
            out->hwsync->last_timestamp  = ts;

            aml_hwsync_wrap_is_amaster(out->hwsync, &amaster_mode);
            if (!amaster_mode) {
                ALOGE("%s not amaster mode", __func__);
                out->hwsync->first_apts_flag = false;
                return 0;
            }

            if (!adev->is_netflix &&
                abs(pcr_pts_gap) > (APTS_DISCONTINUE_THRESHOLD_MIN_70MS) &&
                abs(pcr_pts_gap) < APTS_DISCONTINUE_THRESHOLD_MIN_3S &&
                apts64 > pcr &&
                pcr != 0) {
                int insert_size = 0;
                insert_size = pcr_pts_gap * 48 * 4;
                ALOGI("%s(), pcrscr %" PRIu64 " ms adjusted_apts %" PRIu64 " ms", __func__, pcr/90, apts64/90);
                ALOGI("audio gap: pcr < apts %d ms, need insert data %d\n", pcr_pts_gap, insert_size);
                *p_adjust_ms = pcr_pts_gap;
                out->is_insert_zero_data = true;
            } else {
                out->is_insert_zero_data = false;
            }

        } else {
            ALOGI("%s not ready  apts =%" PRIx64 "", __func__, apts);
            ALOGI("%s alsa pcm delay =%d bitstream delay =%d pipeline =%d total =%d", __func__,
                alsa_pcm_delay_frames,
                alsa_bitstream_delay_frames,
                ms12_pipeline_delay_frames,
                latency_frames);
        }
    } else {
        //do nothing
        //it don't have tsync mode after u-amloigc branch.
    }
    return ret;
}


int aml_audio_hwsync_checkin_apts(audio_hwsync_t *p_hwsync, uint64_t offset, uint64_t apts)
{
    int i = 0;
    int ret = -1;
    if (!p_hwsync) {
        ALOGE("%s null point", __func__);
        return -1;
    }
    struct aml_audio_device *adev = p_hwsync->aout->dev;
    int debug_enable = aml_audio_get_hwsync_flag();
    apts_tab_t *pts_tab = NULL;
    if (debug_enable) {
        ALOGI("++ %s checkin ,offset %" PRIx64 ",apts 0x%" PRIx64 ", sizeof(unsigned):%zu", __func__, offset, apts, sizeof(unsigned));
    }
    pthread_mutex_lock(&p_hwsync->lock);
    pts_tab = p_hwsync->pts_tab;
    for (i = 0; i < HWSYNC_APTS_NUM; i++) {
        if (!pts_tab[i].valid) {
            pts_tab[i].pts = apts;
            pts_tab[i].offset = offset;
            pts_tab[i].valid = 1;
            if (debug_enable) {
                ALOGI("%s checkin done,offset %" PRIx64 ",apts 0x%" PRIx64 "", __func__, offset, apts);
            }
            ret = 0;
            break;
        }
    }
    pthread_mutex_unlock(&p_hwsync->lock);
    return ret;
}

/*
for audio tunnel mode.the apts checkin align is based on
the audio payload size.
normally for Netflix case:
1)dd+ 768 byte per frame
2)LPCM from aac decoder, normally 4096 for LC AAC.8192 for SBR AAC
for the LPCM, the MS12 normally drain 6144 bytes per times which is
the each DD+ decoder output,we need align the size to 4096/8192 align
to checkout the apts.
*/
int aml_audio_hwsync_lookup_apts(audio_hwsync_t *p_hwsync, uint64_t offset, uint64_t *p_apts)
{
    int i = 0;
    uint64_t align  = 0;
    int ret = -1;
    struct aml_audio_device *adev = NULL;
    struct aml_stream_out  *out = NULL;
    int    debug_enable = 0;
    //struct aml_audio_device *adev = p_hwsync->aout->dev;
    //struct audio_stream_out *stream = (struct audio_stream_out *)p_hwsync->aout;
    //int    debug_enable = (adev->debug_flag > 8);
    uint32_t latency_frames = 0;
    uint64_t latency_pts = 0;
    apts_tab_t *pts_tab = NULL;
    uint64_t nearest_pts = 0;
    uint64_t nearest_offset = 0;
    uint64_t min_offset = 0x7fffffffffffffff;
    int match_index = -1;
    audio_format_t hal_internal_format = AUDIO_FORMAT_PCM_16_BIT;

    // add protection to avoid NULL pointer.
    if (!p_hwsync) {
        ALOGE("%s null point", __func__);
        return -1;
    }

    if (p_hwsync->aout == NULL) {
        ALOGE("%s,p_hwsync->aout == NULL", __func__);
        return -1;
    }

    out = p_hwsync->aout;
    adev = p_hwsync->aout->dev;
    if (adev == NULL) {
        ALOGE("%s,adev == NULL", __func__);
        return -1;
    } else {
        debug_enable = aml_audio_get_hwsync_flag();
    }

    hal_internal_format = out->hal_internal_format;

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        hal_internal_format = ms12_get_audio_hal_format(out->hal_internal_format);
    }

    if (debug_enable) {
        ALOGI("%s offset %" PRIx64 ",first %d", __func__, offset, p_hwsync->first_apts_flag);
    }
    pthread_mutex_lock(&p_hwsync->lock);

    /*the hw_sync_frame_size will be an issue if it is fixed one*/
    //ALOGI("Adaptive steam =%d", p_hwsync->bvariable_frame_size);
    if (!p_hwsync->bvariable_frame_size) {
        if (p_hwsync->hw_sync_frame_size) {
            align = offset - offset % p_hwsync->hw_sync_frame_size;
        } else {
            align = offset;
        }
    } else {
        align = offset;
    }
    pts_tab = p_hwsync->pts_tab;
    for (i = 0; i < HWSYNC_APTS_NUM; i++) {
        if (pts_tab[i].valid) {
            if (pts_tab[i].offset == align) {
                *p_apts = pts_tab[i].pts;
                nearest_offset = pts_tab[i].offset;
                pts_tab[i].valid = 0;
                ret = 0;
                if (debug_enable) {
                    ALOGI("%s first flag %d,pts checkout done,offset %" PRIx64 ",align %" PRIx64 ",pts 0x%" PRIx64 "",
                          __func__, p_hwsync->first_apts_flag, offset, align, *p_apts);
                }
                break;
            } else if (pts_tab[i].offset < align) {
                /*find the nearest one*/
                if ((align - pts_tab[i].offset) < min_offset) {
                    min_offset = align - pts_tab[i].offset;
                    match_index = i;
                    nearest_pts = pts_tab[i].pts;
                    nearest_offset = pts_tab[i].offset;
                }
                pts_tab[i].valid = 0;
            }
        }
    }
    if (i == HWSYNC_APTS_NUM) {
        if (nearest_pts) {
            ret = 0;
            *p_apts = nearest_pts;
            if (debug_enable)
                ALOGI("find nearest pts 0x%" PRIx64 " offset %" PRIx64 " align %" PRIx64 "", *p_apts, nearest_offset, align);
        }
    }
    if ((ret == 0) && audio_is_linear_pcm(hal_internal_format)) {
        int diff = 0;
        int pts_diff = 0;
        uint32_t frame_size = out->hal_frame_size;
        uint32_t sample_rate  = out->hal_rate;
        diff = (offset >= nearest_offset) ? (offset - nearest_offset) : 0;
        if ((frame_size != 0) && (sample_rate != 0)) {
            pts_diff = (diff * 1000/frame_size)/sample_rate;
        }
        *p_apts +=  pts_diff * 90;
        if (debug_enable) {
            ALOGI("data offset =%" PRIx64 " pts offset =%" PRIx64 " diff =%" PRIx64 " pts=0x%" PRIx64 " pts diff =%d", offset, nearest_offset, offset - nearest_offset, *p_apts, pts_diff);
        }
    }
    pthread_mutex_unlock(&p_hwsync->lock);
    return ret;
}

void* aml_audio_hwsync_create(void)
{
    return aml_hwsync_mediasync_create();
}

void aml_audio_hwsync_release(audio_hwsync_t *p_hwsync)
{
    ALOGI("%s", __func__);
    if (!p_hwsync) {
        ALOGE("%s p_hwsync is null, release failed", __func__);
        return;
    }
    aml_hwsync_wrap_release(p_hwsync);

    ALOGI("%s done", __func__);
}

int aml_audio_hwsync_open(void)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)aml_adev_get_handle();

    adev->tsync_fd = aml_hwsync_wrap_set_tsync_open();
    if (adev->tsync_fd < 0) {
        ALOGE("%s() open tsync failed", __func__);
    }
    return 0;
}

int aml_audio_hwsync_close(void)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)aml_adev_get_handle();

    aml_hwsync_wrap_set_tsync_close(adev->tsync_fd);
    return 0;
}

/*
** parsed_size is counsed data size of inBuffer
** outBuffer is payload data
** outBytes is the payload size, it should be a frame size or 0.
** outPts is the pts of this frame.
** return cost bytes for this parser
*/
int aml_audio_hwsync_data_parser(audio_hwsync_t *pHwsync,
        const void *inBuffer, const size_t inbytes, size_t parsedSize, void **outBuffer, size_t *outBytes, uint64_t *outPts)
{
    //struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_stream_out *aml_out = pHwsync->aout;
    struct aml_audio_device *adev = aml_out->dev;
    size_t total_bytes = inbytes;
    const void *buffer = inBuffer;
    size_t bytes_cost = 0;
    uint64_t apts64 = 0;
    bool amaster_mode = true;
    size_t hwsync_cost_bytes = 0;
    audio_hwsync_t *hw_sync = aml_out->hwsync;
    int ret = -1;


    bytes_cost = parsedSize;
    /* handle HWSYNC audio data*/
    uint64_t  cur_pts = ULLONG_MAX;//defined in limits.h
    int outsize = 0;

    AM_LOGV("+++ before aml_audio_hwsync_find_frame bytes %zu\n", total_bytes - bytes_cost);
    hwsync_cost_bytes = aml_audio_hwsync_find_frame(aml_out->hwsync, (char *)buffer + bytes_cost, total_bytes - bytes_cost, &cur_pts, &outsize);
    if (cur_pts > ULLONG_MAX) {
        ALOGE("APTS exceed the max 64bit value");
    }
    /*in xts test, the last frame size and pts are both 0, so we assume it is end of stream*/
    if (aml_out->hwsync->last_apts_from_header == 0 && aml_out->hwsync->hw_sync_body_cnt == 0) {
        aml_out->hwsync->end_of_hwsync_frame = true;
    } else {
        aml_out->hwsync->end_of_hwsync_frame = false;
    }
    ALOGV("after aml_audio_hwsync_find_frame bytes remain %zu,cost %zu,outsize %d,pts %"PRIx64"\n",
           total_bytes - bytes_cost - hwsync_cost_bytes, hwsync_cost_bytes, outsize, cur_pts);
    //TODO,skip 3 frames after flush, to tmp fix seek pts discontinue issue.need dig more
    // to find out why seek print pts frame is remained after flush.WTF.
    if (aml_out->skip_frame > 0) {
        aml_out->skip_frame--;
        ALOGI ("skip pts@%"PRIx64",cur frame size %d,cost size %zu\n", cur_pts, outsize, hwsync_cost_bytes);
        return hwsync_cost_bytes;
    }

    if (outsize > 0) {
        *outPts = cur_pts;
        *outBuffer = hw_sync->hw_sync_body_buf;
        *outBytes = outsize;
    } else {
        *outPts = 0;
        *outBuffer = NULL;
        *outBytes = outsize;
    }
    AM_LOGV("---  outsize:%d  outBuffer:%p outBytes:%zu outPts:%"PRIu64" ", outsize, *outBuffer, *outBytes, *outPts);
    return hwsync_cost_bytes;
}

static int aml_hwsync_data_process(void *pHwsync, const void *inABuffer, void *outABuffer, void *parser_callback)
{
    aml_audio_buffer_t *audioBuffer = (aml_audio_buffer_t *)inABuffer;
    const void *inBuffer = audioBuffer->pData;
    size_t inBytes = audioBuffer->size;
    aml_audio_buffer_t *outAudioBuffer = (aml_audio_buffer_t *)outABuffer;
    struct aml_stream_out *aml_out = ((audio_hwsync_t *)pHwsync)->aout;
    int ret = 0;
    void *outBuffer = NULL;
    size_t outBytes = 0;
    uint64_t outPts = 0;
    int retValue = 0;
    int consumed_bytes = 0, parsed_size = 0;

hwsync_rewrite:
    consumed_bytes = aml_audio_hwsync_data_parser(pHwsync, inBuffer, inBytes, parsed_size, &outBuffer, &outBytes, &outPts);
    parsed_size += consumed_bytes;

    //AM_LOGI("  inBytes:%zu  parsed_size:%d  consumed_bytes:%d  outBuffer:%p outBytes:%zu outPts:%"PRIu64" ",
    //    inBytes, parsed_size, consumed_bytes, outBuffer, outBytes, outPts);
    if (parser_callback && outBytes && outBuffer) {
        aml_parser_data_callback_t *pCallback = (aml_parser_data_callback_t *)parser_callback;
        Func_Write_CallBack __callback = pCallback->callback;
        outAudioBuffer->pData = outBuffer;
        outAudioBuffer->size = outBytes;
        outAudioBuffer->apts = outPts;
        outAudioBuffer->isAptsValid = true;
        memcpy(&outAudioBuffer->bufFormat, &audioBuffer->bufFormat, sizeof(buffer_data_format_t));

        retValue = (*__callback)(pCallback->common.pAmlParser, outAudioBuffer, pHwsync);
    }


    if (consumed_bytes > 0 &&  inBytes > parsed_size) {//need to keep parser

        /* We need to wait for Google to fix the issue:
         * Issue: After pause, there will be residual sound in AF, which will cause NTS fail.
         * Now we need to judge whether the current format is DTS */
        // this dts decoding more frames logic should be put to decoder module.
        if (false && is_dts_format(aml_out->hal_internal_format)) {
            // For some low bitrate streams, we need to decode more frames to avoid underrun.
            // (DTSHD_PERIOD_SIZE) is the value after tuning.
            if (parsed_size < DTSHD_PERIOD_SIZE) {
                goto hwsync_rewrite;
            } else {
                return parsed_size;
            }
        } else {
            goto hwsync_rewrite;
        }
    } else {//input data was parsed completely.
        ret = parsed_size;
    }

    return ret;
}

static int aml_get_hwsync_parser_instance(void **pphandle)
{
    *pphandle;
    struct aml_stream_out *pAmlStream = (struct aml_stream_out *)*pphandle;
    AM_LOGI("hw_sync:%p", pAmlStream->hwsync);

    *pphandle = (void *)pAmlStream->hwsync;
    return 0;
}

static void *aml_hwsync_parser_close(void *phandle)
{
    phandle;
    //aml_audio_hwsync_close();
    //aml_audio_hwsync_create();
    return NULL;
}

static void *aml_hwsync_parser_flush(void *phandle)
{
    phandle;
    AM_LOGI(" phandle:%p ", phandle);
    return NULL;
}

aml_parser_func_t *get_hwsync_parser_func_handle(void)
{
    aml_parser_func_t *amlParserFunc = NULL;

    amlParserFunc = (struct aml_parser_func *)aml_audio_calloc(1, sizeof(struct aml_parser_func));
    if (amlParserFunc) {
        amlParserFunc->f_init       = aml_get_hwsync_parser_instance;
        amlParserFunc->f_deinit     = aml_hwsync_parser_close;
        amlParserFunc->f_process    = aml_hwsync_data_process;
        amlParserFunc->f_flush      = aml_hwsync_parser_flush;
    } else {
        AM_LOGE(" calloc amlParserFunc:%p failed", amlParserFunc);
        amlParserFunc = NULL;
    }

    return amlParserFunc;
}


