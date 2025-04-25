/*
 * Copyright (C) 2024 Amlogic Corporation.
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

#define LOG_TAG "audio_hw_hal_streamMgr"
//#define LOG_NDEBUG 0
#define __USE_GNU

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <cutils/properties.h>

#include <aml_dump_debug.h>
#include "dolby_lib_api.h"
#include "aml_stream_manager.h"
#include "audio_hw_utils.h"

static bool _is_stream_raw_format(struct aml_stream_out *amlStream)
{
    bool retValue = false;

    switch (amlStream->hal_internal_format) {
        case AUDIO_FORMAT_AC3:
        case AUDIO_FORMAT_E_AC3:
        case AUDIO_FORMAT_AC4:
        case AUDIO_FORMAT_MAT:
        case AUDIO_FORMAT_DOLBY_TRUEHD:
        case AUDIO_FORMAT_DTS:
        case AUDIO_FORMAT_DTS_HD:
        case AUDIO_FORMAT_AAC:
        case AUDIO_FORMAT_HE_AAC_V1:
        case AUDIO_FORMAT_HE_AAC_V2:
        case AUDIO_FORMAT_MP3:
        case AUDIO_FORMAT_DRA:
            retValue = true;
            break;
        default:
            retValue = false;
            break;
    };

    return retValue;
}

static bool _is_dts_stream(struct aml_stream_out *amlStream)
{
    bool retValue = false;

    switch (amlStream->hal_internal_format) {
        case AUDIO_FORMAT_DTS:
        case AUDIO_FORMAT_DTS_HD:
            retValue = true;
            break;
        default:
            retValue = false;
            break;
    };

    return retValue;
}

static bool _is_hwsync_stream(struct aml_stream_out *amlStream)
{
    bool retValue = false;

    //amlStream->hw_sync_mode
    switch (amlStream->streamType) {
        case STREAM_PCM_HWSYNC:
        case STREAM_RAW_HWSYNC:
            retValue = true;
            break;
        default:
            retValue = false;
            break;
    };

    return retValue;
}

int aml_stream_check_hwsync_release_policy(struct aml_stream_out *amlStream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)amlStream->dev;
    aml_release_policy_type_t policyValue = AML_RELEASE_POLICY_INVALID;
    uint32_t hwsyncStreamCount = 0;

    //we need check if there is multi hwsync stream.
    //if yes, current can't release hwsync resource in close stream.
    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;

            struct aml_stream_out *pOutStream = (struct aml_stream_out *)ptmp->pStream;
            //check all streams,and store hwsync stream Num.
            if (_is_hwsync_stream(pOutStream)) {
                hwsyncStreamCount++;
            }
        }

        //Just hwsync stream is equal and less than 1,we can be approved to release mediasync.
        if (hwsyncStreamCount <= 1) {
            policyValue = AML_RELEASE_POLICY_APPROVAL;
        } else {
            policyValue = AML_RELEASE_POLICY_REJECTION;
        }
    }

    return policyValue;
}

//this function just for debug, it should be removed after dts support dual instance.
int aml_stream_check_dts_write_policy(struct aml_stream_out *amlStream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)amlStream->dev;
    aml_write_policy_type_t policyValue = AML_WRITE_POLICY_INVALID;
    uint32_t nodeIndex = 0;

    //we need check if there is multi dts stream.
    //if yes, just the first stream can send data to decoder.
    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;

            nodeIndex++;
            struct aml_stream_out *pOutStream = (struct aml_stream_out *)ptmp->pStream;
            //search the first dts stream
            if (_is_dts_stream(pOutStream))
                break;
        }

        //Just the first dts stream can be approved
        //the policy is the first coming stream has the high priority to write.
        if (amlStream == ptmp->pStream) {
            policyValue = AML_WRITE_POLICY_APPROVAL;
        } else {
            policyValue = AML_WRITE_POLICY_REJECTION;
        }
    }

    return policyValue;
}


int aml_check_spdif_write_policy(struct aml_stream_out *amlStream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)amlStream->dev;
    aml_write_policy_type_t policyValue = AML_WRITE_POLICY_INVALID;
    uint32_t nodeIndex = 0;

    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;
            //AM_LOGI(" item:%p ptmp:%p  lNode:%p pStream:%p", item, ptmp, &ptmp->lNode, ptmp->pStream);

            nodeIndex++;
            struct aml_stream_out *pOutStream = (struct aml_stream_out *)ptmp->pStream;
            if (_is_stream_raw_format(pOutStream))
                break;
        }

        //AM_LOGI(" ptmp:%p  pStream:%p, amlStream:%p, streamCount:%u nodeIndex:%u",
        //    ptmp, ptmp->pStream, amlStream, adev->streamCount, nodeIndex);
        if (amlStream == ptmp->pStream) {
            //the first raw node is this stream,
            //the policy is the first coming stream has the high priority to write.
            policyValue = AML_WRITE_POLICY_APPROVAL;
        } else {
            policyValue = AML_WRITE_POLICY_REJECTION;
        }
    }

    return policyValue;
}

static bool _is_main_stream(struct aml_stream_out *amlStream)
{
    bool retValue = false;

    switch (amlStream->streamType) {
        case STREAM_PCM_DIRECT:
        case STREAM_PCM_HWSYNC:
        case STREAM_RAW_DIRECT:
        case STREAM_RAW_HWSYNC:
        case STREAM_PCM_MMAP:
            retValue = true;
            break;
        default:
            retValue = false;
            break;
    };

    return retValue;
}

//replace this need_hw_mix(adev->usecase_masks) function.
bool aml_get_is_need_hw_mix(struct aml_stream_out *amlStream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)amlStream->dev;
    return adev->is_main_stream_exist;
}

//check there is active stream or not.
bool aml_get_is_exist_active_stream(void)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    struct aml_stream_out *amlStream = NULL;
    uint32_t nodeIndex = 0;
    bool retValue = false;

    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;
            amlStream = (struct aml_stream_out *)ptmp->pStream;
            nodeIndex++;
            if (!amlStream->standby) {
                retValue = true;
                break;
            }
        }

        AM_LOGI(" ptmp:%p  pStream:%p, amlStream:%p, streamCount:%u nodeIndex:%u, retValue:%d",
            ptmp, ptmp->pStream, amlStream, adev->streamCount, nodeIndex, retValue);
    }

    return retValue;
}

struct aml_stream_out * aml_get_main_active_stream(audio_format_t audio_format)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    struct aml_stream_out *amlStream = NULL;
    bool retValue = false;

    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;
            amlStream = (struct aml_stream_out *)ptmp->pStream;
            if (amlStream->hal_internal_format == audio_format) {
                return amlStream;
            }
        }
    }

    return NULL;
}


bool aml_is_preempt_deep_buffer_stream(struct aml_stream_out *amlStream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)amlStream->dev;
    uint32_t nodeIndex = 0;
    bool retValue = false;

    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;
            nodeIndex++;
            struct aml_stream_out *pOutStream = (struct aml_stream_out *)ptmp->pStream;
            if (pOutStream && !pOutStream->standby && ((pOutStream->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) || pOutStream->is_system_audio_usage_media)) {
                retValue = true;
                break;
            }
        }

        amlStream->is_preempt_system_audio_usage_media_stream = retValue;
    }
    ALOGI("%s main stream can preempt the system audio usage media (deep buffer) stream:%d and retValue:%d", __func__, amlStream->is_preempt_system_audio_usage_media_stream, retValue);

    return retValue;
}

void aml_check_close_ms12_output_main_stream(struct aml_stream_out *amlStream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)amlStream->dev;
    uint32_t nodeIndex = 0;

    pthread_mutex_lock(&adev->streamList_MutexLock);
    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;
        // 1. If "setprop persist.vendor.audio.ms12.default.values true"
        // these following ASDK Test cases can passed
        // atmos_stickiness_usage_media_ddp_out-no_cfg-v241-HDMI (6581)
        // atmos_stickiness_usage_media_mat_out-no_cfg-v241-HDMI (6612)
        // 2. If "setprop persist.vendor.audio.ms12.default.values false"
        // when BT input sounds(deepbuffer audiotrack) during EXO playing AAC/MPEG file(main stream as Tunnel mode).
        // we can keep "deep buffer stream" and "main stream" both working.
        // to avoid Soundbar speaker output noise.
        bool is_asdk_test = property_get_bool("persist.vendor.audio.ms12.default.values", false);

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;
            nodeIndex++;
            struct aml_stream_out *pOutStream = (struct aml_stream_out *)ptmp->pStream;
            if (pOutStream && pOutStream->is_ms12_main_decoder && !pOutStream->is_preempt_system_audio_usage_media_stream && is_asdk_test) {
                if (pOutStream->is_ms12_main_decoder) {
                    ALOGI("%s() line %d close ms12 main stream", __func__, __LINE__);
                    aml_close_ms12_output_main_stream(pOutStream);
                }
            }
        }
    }
    pthread_mutex_unlock(&adev->streamList_MutexLock);
    return ;
}

void aml_stream_check_preempt(struct aml_stream_out *amlStream) {
    struct aml_audio_device *adev = (struct aml_audio_device *)amlStream->dev;

    bool is_new_dolby_format = is_dolby_ms12_support_compression_format(amlStream->hal_internal_format);
    bool is_new_dts_format = is_dts_format(amlStream->hal_internal_format);

    pthread_mutex_lock(&adev->streamList_MutexLock);

    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;
        struct aml_stream_out *tmpStream = NULL;
        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;
            tmpStream = (struct aml_stream_out *)ptmp->pStream;
            if (tmpStream) {
                if (is_new_dolby_format) {
                    /*if the incoming stream is dolby, try to preempt the dts one*/
                    if (is_dts_format(tmpStream->hal_internal_format)) {
                        tmpStream->is_preempted = true;
                        ALOGI("%s new stream %p format =0x%x preempt old stream %p format=0x%x",
                              __func__, amlStream, amlStream->hal_internal_format, tmpStream, tmpStream->hal_internal_format);
                    }
                } else if (is_new_dts_format) {
                    /*if the incoming stream is dts, try to preempt the all the ms12 stream*/
                    if (tmpStream->is_ms12_main_decoder) {
                        tmpStream->is_preempted = true;
                        ALOGI("%s new stream %p format =0x%x preempt old stream %p format=0x%x",
                             __func__,amlStream, amlStream->hal_internal_format, tmpStream, tmpStream->hal_internal_format);
                    }
                }
            }
        }
    }

    pthread_mutex_unlock(&adev->streamList_MutexLock);
    return;
}

int aml_stream_register(struct aml_stream_out *amlStream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)amlStream->dev;
    struct stream_infos *pStreamInfos = NULL;
    int retValue = 0;
    int streamTypeIndex = 0;
    bool is_main_stream_exist = false;
    uint32_t nodeIndex = 0;

    pthread_mutex_lock(&adev->streamList_MutexLock);
    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;
        struct aml_stream_out *tmpStream = NULL;
        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;
            tmpStream = (struct aml_stream_out *)ptmp->pStream;
            if (tmpStream && tmpStream->streamType == amlStream->streamType) {
                streamTypeIndex++;
            }
        }
    }
    pStreamInfos = (struct stream_infos *)aml_audio_calloc(1, sizeof(struct stream_infos));
    if (pStreamInfos) {
        pStreamInfos->pStream = amlStream;
        amlStream->streamTypeIndex = streamTypeIndex;
        list_add_tail(&adev->stream_ListHead, &pStreamInfos->lNode);
        adev->streamCount++;
        AM_LOGI(" mutexLock list_empty:%d, pStreamInfos:%p lNode:%p pStream:%p aml_out:%p,  streamCount:%u",
            list_empty(&adev->stream_ListHead), pStreamInfos, &pStreamInfos->lNode, pStreamInfos->pStream, amlStream, adev->streamCount);

        if (adev->debug_flag) {
            if (!list_empty(&adev->stream_ListHead)) {
                struct listnode *item = NULL, *temp = NULL;
                struct stream_infos *ptmp = NULL;

                list_for_each_safe(item, temp, &adev->stream_ListHead) {
                    ptmp = (struct stream_infos *)item;
                    AM_LOGI(" item:%p ptmp:%p  lNode:%p pStream:%p", item, ptmp, &ptmp->lNode, ptmp->pStream);
                }
            }
        }
    } else {
        retValue = -1;
        AM_LOGE(" pStreamInfos:%p  alloc failed, then retValue:%d", pStreamInfos, retValue);
    }

    /*update the main stream exist info*/
    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;

            nodeIndex++;
            struct aml_stream_out *pOutStream = (struct aml_stream_out *)ptmp->pStream;
            if (_is_main_stream(pOutStream))
                break;
        }

        if (nodeIndex >= 2 && _is_main_stream((struct aml_stream_out *)ptmp->pStream)) {
            is_main_stream_exist = true;
        }
        AM_LOGV(" ptmp:%p  pStream:%p, amlStream:%p, streamCount:%u nodeIndex:%u, is_main_stream_exist:%d",
            ptmp, ptmp->pStream, amlStream, adev->streamCount, nodeIndex, is_main_stream_exist);
        adev->is_main_stream_exist = is_main_stream_exist;
    }

    pthread_mutex_unlock(&adev->streamList_MutexLock);
    return retValue;
}

void aml_stream_unregister(struct aml_stream_out *amlStream)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)amlStream->dev;
    bool is_main_stream_exist = false;
    uint32_t nodeIndex = 0;

    pthread_mutex_lock(&adev->streamList_MutexLock);
    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            //AM_LOGI(" item:%p ptmp:%p", item, ptmp);
            ptmp = (struct stream_infos *)item;
            if (ptmp->pStream == amlStream) {
                AM_LOGI(" item:%p ptmp:%p  lNode:%p pStream:%p", item, ptmp, &ptmp->lNode, ptmp->pStream);
                list_remove(&ptmp->lNode);
                ptmp->lNode.prev = NULL;
                ptmp->lNode.next = NULL;
                aml_audio_free(ptmp);
                adev->streamCount--;
            }
        }
    }

    /*update the main stream exist info*/
    if (!list_empty(&adev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &adev->stream_ListHead) {
            ptmp = (struct stream_infos *)item;

            nodeIndex++;
            struct aml_stream_out *pOutStream = (struct aml_stream_out *)ptmp->pStream;
            if (_is_main_stream(pOutStream))
                break;
        }

        if (nodeIndex >= 2 && _is_main_stream((struct aml_stream_out *)ptmp->pStream)) {
            is_main_stream_exist = true;
        }
        AM_LOGV(" ptmp:%p  pStream:%p, amlStream:%p, streamCount:%u nodeIndex:%u, is_main_stream_exist:%d",
            ptmp, ptmp->pStream, amlStream, adev->streamCount, nodeIndex, is_main_stream_exist);
        adev->is_main_stream_exist = is_main_stream_exist;
    }

    pthread_mutex_unlock(&adev->streamList_MutexLock);

    return ;
}


void aml_init_stream_manager(struct aml_audio_device *amlDev)
{
    struct aml_audio_device *adev = amlDev;

    pthread_mutex_lock(&adev->streamList_MutexLock);
    list_init(&adev->stream_ListHead);
    amlDev->streamCount = 0;
    pthread_mutex_unlock(&adev->streamList_MutexLock);
    AM_LOGI(" adev:%p, stream_ListHead:%p, prev:%p next:%p, lis_empty:%d, streamCount:%u",
        adev, &adev->stream_ListHead, adev->stream_ListHead.prev, adev->stream_ListHead.next, list_empty(&adev->stream_ListHead), amlDev->streamCount);

    return ;
}

void aml_destroy_stream_manager(struct aml_audio_device *amlDev)
{
    pthread_mutex_lock(&amlDev->streamList_MutexLock);
    if (!list_empty(&amlDev->stream_ListHead)) {
        struct listnode *item = NULL, *temp = NULL;
        struct stream_infos *ptmp = NULL;

        list_for_each_safe(item, temp, &amlDev->stream_ListHead) {
            //AM_LOGI(" item:%p ptmp:%p", item, ptmp);
            ptmp = (struct stream_infos *)item;
            AM_LOGI(" item:%p ptmp:%p  lNode:%p pStream:%p", item, ptmp, &ptmp->lNode, ptmp->pStream);
            list_remove(&ptmp->lNode);
            ptmp->lNode.prev = NULL;
            ptmp->lNode.next = NULL;
            aml_audio_free(ptmp);
        }
    } else {
        AM_LOGI(" stream list is_empty:%d", list_empty(&amlDev->stream_ListHead));
    }

    amlDev->streamCount = 0;
    amlDev->stream_ListHead.prev = NULL;
    amlDev->stream_ListHead.next = NULL;
    pthread_mutex_unlock(&amlDev->streamList_MutexLock);

    AM_LOGI(" reset stream List, then exit");
    return ;
}


