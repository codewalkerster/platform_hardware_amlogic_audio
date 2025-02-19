/*
 * Copyright (C) 2023 Amlogic Corporation.
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

#define LOG_TAG "aml_volume_shaper"

#include <cutils/log.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include "audio_hw.h"
#include "audio_hw_utils.h"
#include "aml_volume_shaper.h"


#define AML_VS_LOG(...) ALOGD_IF(p_vol_shaper->bDebug, __VA_ARGS__)


// Theoretically the delay time of volume point is better as shorter as,
// this 330ms(experimental data) is worst case we have met.
#define VOLUME_MAX_DELAY_US    (330 * 1000)

// When a volume-shaper is active, it will updated on each buffer write.
// current DDP or HEAAC case, its buffer time is 32/42 ms;
// so a volume point max used timed will not exceed two buffer's time(84 ms)
#define VOLUME_MAX_USED_US     (96 * 1000)

typedef enum aml_vol_list_state {
    AML_VOL_LIST_INVALID = 0,
    AML_VOL_LIST_EMPTY = 1,
    AML_VOL_LIST_NON_EMPTY,

    AML_VOL_LIST_MAX,
} aml_vol_list_t;


typedef struct aml_volume_desc {
    struct listnode list_node;
    float           fVolume;
    int             s32WriteTimeMs;
    uint64_t        u64AddTimeUs;
    uint64_t        u64OffsetUs;
} aml_volume_t;


int aml_volume_shaper_empty(aml_volume_shaper_t *p_vol_shaper)
{
    int is_emtpy = 1;

    if (p_vol_shaper) {
        if (!list_empty(&p_vol_shaper->list_head)) {
            is_emtpy = 0;
        }
    }
    return is_emtpy;

}

int aml_volume_shaper_list_each(aml_volume_shaper_t *p_vol_shaper)
{
    AM_LOGD("entry...");
    if (p_vol_shaper == NULL) {
        return -1;
    }

    pthread_mutex_lock(&p_vol_shaper->lock);
    if (!p_vol_shaper->bInitialized || list_empty(&p_vol_shaper->list_head)) {
        //*vol = 0.0;
        AM_LOGW("volume list %s empty, or list inited:%d\n",
            list_empty(&p_vol_shaper->list_head)?"is":"is not", p_vol_shaper->bInitialized);
        pthread_mutex_unlock(&p_vol_shaper->lock);
        return 0;
    }

    struct listnode *node = NULL, *tmp_node = NULL;
    list_for_each_safe(node, tmp_node, &p_vol_shaper->list_head) {
        struct aml_volume_desc *vol_item = (struct aml_volume_desc *)node;
        AM_LOGD("time:%"PRId64" vol:%f\n", vol_item->u64AddTimeUs, vol_item->fVolume);
    }
    pthread_mutex_unlock(&p_vol_shaper->lock);

    return 0;
}

static aml_vol_list_t aml_volume_shaper_peek(aml_volume_shaper_t *p_vol_shaper, float *vol)
{
    if (p_vol_shaper == NULL) {
        return AML_VOL_LIST_EMPTY;
    }

    pthread_mutex_lock(&p_vol_shaper->lock);
    if (list_empty(&p_vol_shaper->list_head)) {
        //ALOGW("%s  volume list is empty.\n", __FUNCTION__);
        pthread_mutex_unlock(&p_vol_shaper->lock);
        return AML_VOL_LIST_EMPTY;
    }

    struct listnode *node = list_head(&p_vol_shaper->list_head);
    struct aml_volume_desc *vol_item = (struct aml_volume_desc *)node;

    *vol = vol_item->fVolume;
    pthread_mutex_unlock(&p_vol_shaper->lock);

    return AML_VOL_LIST_NON_EMPTY;
}


static aml_vol_list_t aml_volume_shaper_move_on(aml_volume_shaper_t *p_vol_shaper, float *next_vol, int step_time_us, int *p_write_ms)
{
    struct listnode *node = NULL;
    struct aml_volume_desc *vol_item = NULL;
    struct aml_volume_desc *left_item = NULL;
    struct aml_volume_desc *right_item = NULL;
    uint64_t target_time_us = 0;

    if (p_vol_shaper == NULL) {
        return AML_VOL_LIST_EMPTY;
    }

    pthread_mutex_lock(&p_vol_shaper->lock);
    if (list_empty(&p_vol_shaper->list_head)) {
        //AM_LOGW("volume list is empty");
        pthread_mutex_unlock(&p_vol_shaper->lock);
        return AML_VOL_LIST_EMPTY;
    }
    if (step_time_us <= 0) {
        *next_vol = p_vol_shaper->fLastUsedVolume;
        pthread_mutex_unlock(&p_vol_shaper->lock);
        return AML_VOL_LIST_NON_EMPTY;
    }

    if (p_vol_shaper->last_used_node == NULL) {
        p_vol_shaper->last_used_node = list_head(&p_vol_shaper->list_head);
        vol_item = (struct aml_volume_desc *)p_vol_shaper->last_used_node;
        *next_vol = vol_item->fVolume;
        *p_write_ms = vol_item->s32WriteTimeMs;
        pthread_mutex_unlock(&p_vol_shaper->lock);
        AML_VS_LOG("last_used_node is NULL, return first volume %f, writeTimeMs %d", vol_item->fVolume, *p_write_ms);
        return AML_VOL_LIST_NON_EMPTY;
    }

    vol_item = (struct aml_volume_desc *)p_vol_shaper->last_used_node;

    if (!p_vol_shaper->bUseStartFrames && p_vol_shaper->u64StartFrames > 0) {
        uint64_t frames_us = p_vol_shaper->u64StartFrames * 1000 * 1000 / p_vol_shaper->s32SampleRate;
        ALOGI("%s frames_us (%"PRId64"), adjust_time_us (%"PRId64")", __func__, frames_us, p_vol_shaper->u64StartTimeUs - frames_us);

        if (p_vol_shaper->u64StartTimeUs > frames_us) {
            uint64_t adjust_time_us = p_vol_shaper->u64StartTimeUs - frames_us;
            if (adjust_time_us > vol_item->u64AddTimeUs) {
                AM_LOGI("adjust vol_item time from %"PRId64" to %"PRId64"", vol_item->u64AddTimeUs, adjust_time_us);
                vol_item->u64AddTimeUs = adjust_time_us;
                vol_item->u64OffsetUs = 0;
            }
        }
        p_vol_shaper->bUseStartFrames = true;
    }
    left_item = vol_item;
    target_time_us = vol_item->u64AddTimeUs + vol_item->u64OffsetUs + step_time_us;

    AML_VS_LOG("target_time_us(%"PRId64") add_time_us(%"PRId64"), offset_us(%"PRId64") step_time_us(%d)",
        target_time_us, vol_item->u64AddTimeUs, vol_item->u64OffsetUs, step_time_us);

    for (node = p_vol_shaper->last_used_node; node != &p_vol_shaper->list_head; node = node->next) {
        vol_item  = (struct aml_volume_desc *)node;
        AML_VS_LOG("volume %f, offset_us %"PRId64", add_time_us %"PRId64"",
            vol_item->fVolume, vol_item->u64OffsetUs, vol_item->u64AddTimeUs);

        if ((vol_item->u64AddTimeUs + vol_item->u64OffsetUs) <= target_time_us) {
            left_item = vol_item;
        } else {
            right_item = vol_item;
            break;
        }
    }

    float new_volume = 0.0;
    uint64_t left_item_time_us = left_item->u64AddTimeUs + left_item->u64OffsetUs;
    AML_VS_LOG("left_item : add(%"PRId64") offset(%"PRId64"), target(%"PRId64")",
        left_item->u64AddTimeUs, left_item->u64OffsetUs, target_time_us);

    left_item->u64OffsetUs += (target_time_us - left_item_time_us);
    if ((left_item_time_us == target_time_us) || (right_item == NULL)) {
        new_volume = left_item->fVolume;
        AML_VS_LOG("return volume(%f), %d %d", new_volume, (left_item_time_us == target_time_us), (right_item == NULL));
    } else {
        float left_vol = left_item->fVolume;
        uint64_t left_time_us = left_item->u64AddTimeUs;
        float right_vol  = right_item->fVolume;
        uint64_t right_time_us  = right_item->u64AddTimeUs;

        new_volume = left_vol + (right_vol - left_vol)*(left_item->u64OffsetUs)/(right_time_us - left_time_us);
        AML_VS_LOG("return volume %f = %f + (%f - %f) * %"PRId64" / (%"PRId64" - %"PRId64")", new_volume, \
            left_vol, right_vol, left_vol, left_item->u64OffsetUs, right_time_us, left_time_us);
    }

    *next_vol = new_volume;
    p_vol_shaper->last_used_node = (struct listnode *)left_item;
    p_vol_shaper->fLastUsedVolume = new_volume;
    pthread_mutex_unlock(&p_vol_shaper->lock);
    return AML_VOL_LIST_NON_EMPTY;
}


static int aml_volume_shaper_clear(aml_volume_shaper_t *p_vol_shaper)
{
    if (p_vol_shaper == NULL) {
        return 0;
    }

    pthread_mutex_lock(&p_vol_shaper->lock);
    if (list_empty(&p_vol_shaper->list_head)) {
        //ALOGW("%s  volume list is empty.\n", __FUNCTION__);
        pthread_mutex_unlock(&p_vol_shaper->lock);
        return 0;
    }
    struct listnode *node = NULL, *tmp_node = NULL;//list_head(&list_head);

    list_for_each_safe(node, tmp_node, &p_vol_shaper->list_head) {
        struct aml_volume_desc *vol_item = (struct aml_volume_desc *)node;
        list_remove(node);
        free(vol_item);
    }
    pthread_mutex_unlock(&p_vol_shaper->lock);

    return 0;
}


static float get_volume_diff(float a, float b)
{
    if (a >= b) {
        return (a - b);
    } else {
        return (b - a);
    }
}


int aml_volume_shaper_check_equal(float a, float b)
{
    double da = a;
    double db = b;
    const double PRECISION = 1e-08;
    return (fabs(da - db) < PRECISION);
}


// return 1 if volume is ok, else return 0.
int aml_volume_shaper_check_sanity(float volume)
{
    if (volume < GAIN_FLOAT_ZERO || volume > GAIN_FLOAT_UNITY) {
        return 0;
    }
    return 1;
}


int aml_volume_shaper_add(aml_volume_shaper_t *p_vol_shaper, float vol, uint64_t time_us)
{
    uint64_t curr_time_us = 0;

    if (p_vol_shaper == NULL) {
        return -1;
    }
    if (time_us > 0) {
        curr_time_us = time_us;
    } else {
        curr_time_us = aml_audio_get_systime();
    }

    if (!list_empty(&p_vol_shaper->list_head)) {
        pthread_mutex_lock(&p_vol_shaper->lock);
        struct listnode *node = list_tail(&p_vol_shaper->list_head);
        struct aml_volume_desc *last_vol_item = (struct aml_volume_desc *)node;

        if (last_vol_item->u64AddTimeUs == curr_time_us) {
            AML_VS_LOG("replace volume from %f to %f\n", last_vol_item->fVolume, vol);
            last_vol_item->fVolume = vol;
            last_vol_item->u64OffsetUs = 0;
            last_vol_item->s32WriteTimeMs = p_vol_shaper->s32WriteTimeMs;
            pthread_mutex_unlock(&p_vol_shaper->lock);
            return 0;
        }
        pthread_mutex_unlock(&p_vol_shaper->lock);
    }

    struct aml_volume_desc *vol_item = calloc(1, sizeof(struct aml_volume_desc));

    if (NULL == vol_item) {
        AM_LOGE("calloc vol item fail, errno:%s", strerror(errno));
        return -ENOMEM;
    }

    vol_item->fVolume = vol;
    vol_item->u64AddTimeUs = curr_time_us;
    vol_item->s32WriteTimeMs = p_vol_shaper->s32WriteTimeMs;
    list_init(&vol_item->list_node);

    pthread_mutex_lock(&p_vol_shaper->lock);
    list_add_tail(&p_vol_shaper->list_head, &vol_item->list_node);
    pthread_mutex_unlock(&p_vol_shaper->lock);
    if (aml_volume_shaper_check_sanity(p_vol_shaper->fLastUsedVolume)) {
        p_vol_shaper->fLastUsedVolume = vol;
    }

    AML_VS_LOG("add volume:%f entry_time_us %"PRId64", writeTimeMs %d, success", vol,
        vol_item->u64AddTimeUs, vol_item->s32WriteTimeMs);
    //ms_audio_volume_list_each();
    return 0;
}


static float aml_volume_shaper_cleanup(aml_volume_shaper_t *p_vol_shaper)
{
    float vol = AML_AUDIO_GAIN_FLOAT_INVALID;
    if (p_vol_shaper == NULL) {
        return vol;
    }

    pthread_mutex_lock(&p_vol_shaper->lock);
    if (!p_vol_shaper->bInitialized || list_empty(&p_vol_shaper->list_head)) {
        AM_LOGW("volume list %s empty, or list inited:%d\n",
            list_empty(&p_vol_shaper->list_head)?"is":"is not", p_vol_shaper->bInitialized);
        pthread_mutex_unlock(&p_vol_shaper->lock);
        return vol;
    }

    struct listnode *node = NULL, *tmp_node = NULL;
    uint64_t current_time_us = aml_audio_get_systime();

    list_for_each_safe(node, tmp_node, &p_vol_shaper->list_head) {
        struct aml_volume_desc *vol_item = (struct aml_volume_desc *)node;
        bool expired_item = false;
        uint64_t diff_time_us = current_time_us - vol_item->u64AddTimeUs;

        if (diff_time_us > 0) {
            if (diff_time_us > VOLUME_MAX_DELAY_US) {
                expired_item = true;
            } else if ((list_head(node) == (list_tail(node))) && (vol_item->u64OffsetUs >= VOLUME_MAX_USED_US)) {
                // Add for AUDIO-MEDIAVOL-XX-TC4-Tunnel :
                // if current volume list only has one item and it has been used for a while,
                // that means current volume-shaper has finished. We should remove it to avoid affecting next volume-shaper.
                //
                // vol_item->offset : how much time this volume item has used
                expired_item = true;
            }
        }

        if (expired_item) {
            if (p_vol_shaper->last_used_node == node) {
                p_vol_shaper->last_used_node = NULL;
            }
            vol = vol_item->fVolume;
            AML_VS_LOG("remove vol:%f", vol_item->fVolume);
            list_remove(node);
            free(vol_item);
        } else {
            break;
        }
    }
    pthread_mutex_unlock(&p_vol_shaper->lock);

    return vol;
}


int aml_volume_shaper_init(aml_volume_shaper_t *p_vol_shaper, int delay_samples)
{
    if (p_vol_shaper == NULL) {
        AM_LOGV("p_vol_shaper is NULL");
        return -1;
    }
    memset(p_vol_shaper, 0, sizeof(*p_vol_shaper));

    if (pthread_mutex_init(&p_vol_shaper->lock, NULL) != 0) {
        AM_LOGE("pthread_mutex_init fail, errono:%s", strerror(errno));
        return -1;
    }
    list_init(&p_vol_shaper->list_head);
    p_vol_shaper->last_used_node = NULL;
    p_vol_shaper->bInitialized = true;
    p_vol_shaper->s32DelayFrames = delay_samples;
    p_vol_shaper->u64CurrentEaseFrames = 0;
    p_vol_shaper->fLastUsedVolume = AML_AUDIO_GAIN_FLOAT_INVALID;
    p_vol_shaper->fFinalVolume = AML_AUDIO_GAIN_FLOAT_INVALID;

    p_vol_shaper->bDebug = false;
    p_vol_shaper->u64StartFrames = 0;
    p_vol_shaper->u64StartTimeUs = 0;
    p_vol_shaper->bUseStartFrames = 0;
    p_vol_shaper->s32SampleRate = 48000;

    AM_LOGI("volume_shaper %p done !", p_vol_shaper);
    return 0;
}

int aml_volume_shaper_release(aml_volume_shaper_t *p_vol_shaper)
{
    if (p_vol_shaper == NULL) {
        AM_LOGV("p_vol_shaper is NULL");
        return -1;
    }

    if (p_vol_shaper->bInitialized) {
        aml_volume_shaper_clear(p_vol_shaper);
        pthread_mutex_destroy(&p_vol_shaper->lock);
        p_vol_shaper->last_used_node = NULL;
        p_vol_shaper->bInitialized = false;
        p_vol_shaper->u64StartFrames = 0;
        p_vol_shaper->u64StartTimeUs = 0;
        p_vol_shaper->bUseStartFrames = 0;
    }
    AM_LOGI("volume_shaper %p done !", p_vol_shaper);
    return 0;
}

int aml_volume_shaper_get(aml_volume_shaper_t *p_vol_shaper, int move_frames, float *p_volume, int *p_write_ms)
{
    if (p_vol_shaper == NULL) {
        AM_LOGV("p_vol_shaper is NULL");
        return -1;
    }
    if (p_volume == NULL) {
        AM_LOGV("next_volume is NULL");
        return -1;
    }
    *p_write_ms = 0;

    if (!aml_volume_shaper_empty(p_vol_shaper)) {
        float next_volume = 0.0;
        float final_volume = AML_AUDIO_GAIN_FLOAT_INVALID;
        int volume_valid = AML_VOL_LIST_NON_EMPTY;
        int step_time_us = move_frames*1000/48;

        // Remove the expired volume item
        final_volume = aml_volume_shaper_cleanup(p_vol_shaper);
        if (aml_volume_shaper_check_sanity(final_volume)) {
            p_vol_shaper->fFinalVolume = final_volume;
        }

        if (p_vol_shaper->u64CurrentEaseFrames >= p_vol_shaper->s32DelayFrames) {
            volume_valid = aml_volume_shaper_move_on(p_vol_shaper, &next_volume, step_time_us, p_write_ms);
        } else {
            if (aml_volume_shaper_check_sanity(p_vol_shaper->fFinalVolume)) {
                next_volume = p_vol_shaper->fFinalVolume;
            } else {
                volume_valid = aml_volume_shaper_peek(p_vol_shaper, &next_volume);
            }
        }
        AML_VS_LOG("curr_ease %"PRId64", next_volume %f, final_volume %f", \
            p_vol_shaper->u64CurrentEaseFrames, next_volume, p_vol_shaper->fFinalVolume);

        if (volume_valid != AML_VOL_LIST_EMPTY) {
            *p_volume = next_volume;
        }else {
            *p_volume = p_vol_shaper->fFinalVolume;
            AML_VS_LOG("use final_volume %f", p_vol_shaper->fFinalVolume);
        }
        p_vol_shaper->u64LastEaseFrames = p_vol_shaper->u64CurrentEaseFrames;
        p_vol_shaper->u64CurrentEaseFrames += move_frames;

        // Prepare for the next volume-shaper.
        if (aml_volume_shaper_empty(p_vol_shaper)) {
            p_vol_shaper->u64CurrentEaseFrames = 0;
            p_vol_shaper->u64LastEaseFrames = 0;
        }
    } else {
        p_vol_shaper->u64CurrentEaseFrames = 0;
        p_vol_shaper->u64LastEaseFrames = 0;
        if (aml_volume_shaper_check_sanity(p_vol_shaper->fFinalVolume)) {
            *p_volume = p_vol_shaper->fFinalVolume;
                AML_VS_LOG("use final_volume %f", p_vol_shaper->fFinalVolume);
        } else {
            return 1;
        }
    }
    return 0;
}

uint64_t aml_volume_shaper_get_current_frame(aml_volume_shaper_t *p_vol_shaper)
{
    if (p_vol_shaper == NULL) {
        AM_LOGV("p_vol_shaper is NULL");
        return -1;
    }
    return p_vol_shaper->u64CurrentEaseFrames;
}


void aml_volume_shaper_enable_debug(aml_volume_shaper_t *p_vol_shaper, bool debug_enable)
{
    if (p_vol_shaper == NULL) {
        AM_LOGV("p_vol_shaper is NULL");
        return;
    }
    p_vol_shaper->bDebug = debug_enable;
}

void aml_volume_shaper_set_delay_frames(aml_volume_shaper_t *p_vol_shaper, int delay_frames)
{
    if (p_vol_shaper == NULL) {
        AM_LOGV("p_vol_shaper is NULL");
        return;
    }
    p_vol_shaper->s32DelayFrames = delay_frames;
}

void aml_volume_shaper_update_start_frames(aml_volume_shaper_t *p_vol_shaper, uint64_t frames)
{
    if (p_vol_shaper == NULL) {
        AM_LOGV("p_vol_shaper is NULL");
        return;
    }
    if (p_vol_shaper->u64StartFrames <= 0) {
        p_vol_shaper->u64StartFrames = frames;
        p_vol_shaper->u64StartTimeUs = aml_audio_get_systime();
        p_vol_shaper->u64CurrentEaseFrames = 0;
        ALOGI("%s frames %"PRId64", timeUs %"PRId64"", __func__, p_vol_shaper->u64StartFrames, p_vol_shaper->u64StartTimeUs);
    }
}

void aml_volume_shaper_update_write_time(aml_volume_shaper_t *p_vol_shaper, int time_ms)
{
    if (p_vol_shaper == NULL) {
        AM_LOGV("p_vol_shaper is NULL");
        return;
    }
    p_vol_shaper->s32WriteTimeMs = time_ms;
}

// return true  : volume shaper is moving
// return false : volume shaper moved done
bool aml_volume_shaper_update_moving_frame(aml_volume_shaper_t *p_vol_shaper, int frames)
{
    if (p_vol_shaper == NULL) {
        AM_LOGV("p_vol_shaper is NULL");
        return false;
    }
    p_vol_shaper->u64LastEaseFrames += frames;
    if (p_vol_shaper->u64LastEaseFrames < p_vol_shaper->u64CurrentEaseFrames) {
        return true;
    }
    return false;
}
