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

#define LOG_TAG "audio_hw_input_dtv"

#include <stdio.h>
#include <errno.h>
#include <cutils/log.h>

#include "dtv_private_object.h"
#include "device_patch_mgr.h"

#ifdef ENABLE_DVB_PATCH
#include "dtv_patch.h"
#endif

uint32_t get_dtv_i2s_clock(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->dtv_i2s_clock;
}

void set_dtv_i2s_clock(struct aml_audio_device *adev, uint32_t i2s_clock)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->dtv_i2s_clock = i2s_clock;
}

uint32_t get_dtv_spdif_clock(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->dtv_spdif_clock;
}

void set_dtv_spdif_clock(struct aml_audio_device *adev, uint32_t spdif_clock)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->dtv_spdif_clock = spdif_clock;
}

#ifdef ENABLE_DVB_PATCH
aml_dtv_audio_context_t *get_dtv_audio_context(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->aml_dtv_audio_context;
}
#endif

int get_dtv_sound_mode(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->dtv_sound_mode;
}

void set_dtv_sound_mode(struct aml_audio_device *adev, int sound_mode)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->dtv_sound_mode = sound_mode;
}

float get_dtv_volume(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->dtv_volume;
}

void set_dtv_volume(struct aml_audio_device *adev, float volume)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->dtv_volume = volume;
}

void enable_dtv_multi_demux(struct aml_audio_device *adev, int enable)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    dtv_obj->is_multi_demux = enable;
}

bool is_dtv_multi_demux(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    return dtv_obj->is_multi_demux;
}
void acquire_dtv_mutex_lock(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    pthread_mutex_lock(&dtv_obj->dtv_lock);
}

void release_dtv_mutex_lock(struct aml_audio_device *adev)
{
   struct dtv_private_object *dtv_obj = get_dtv_object(adev);
   pthread_mutex_unlock(&dtv_obj->dtv_lock);
}


static bool is_multi_demux()
{
   /*
    * the api indicate use which way to read data
    * true : use new multi demux api
    * false : use old uio abuf api
    */
    if (access("/sys/class/stb/demux0_source",F_OK) == 0) {
         ALOGI("use AmHwDemux mode\n");
         return false;
    }

    if (access("/sys/module/dvb_demux/",F_OK) == 0 ||
        access("/sys/module/amlogic_dvb_demux/",F_OK) == 0) {
        ALOGI("use AmHwMultiDemux mode\n");
        return true;
    }

    ALOGI("use AmHwMultiDemux mode\n");
    return true;
}

int init_dtv_object(struct aml_audio_device *adev)
{
    int ret = 0;
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    if (!dtv_obj) {
        ALOGE("%s() Error, dtv_obj = NULL, return!", __func__);
        return -EINVAL;
    }

    enable_dtv_multi_demux(adev, is_multi_demux());

#if ENABLE_DVB_PATCH
    dtv_obj->aml_dtv_audio_context = aml_audio_calloc(1, sizeof(aml_dtv_audio_context_t));
    if (dtv_obj->aml_dtv_audio_context == NULL) {
        ALOGE("malloc aml_dtv_audio_instances failed");
        ret = -ENOMEM;
        return ret;
    } else {
        aml_dtv_audio_context_t *dtv_audio_context = (aml_dtv_audio_context_t *)dtv_obj->aml_dtv_audio_context;
        create_dtv_cmd_process_thread(dtv_audio_context);
        for (int index = 0; index < DVB_DEMUX_SUPPORT_MAX_NUM; index ++) {
            aml_dtvsync_t *dtvsync = &dtv_audio_context->instances[index].dtvsync;
            pthread_mutex_init(&dtvsync->ms_lock, NULL);
            dtv_audio_context->instances[index].dtv_audio_state = AUDIO_DTV_PATCH_DECODER_STATE_IDLE;
            dtv_audio_context->instances[index].dtv_scene = DTV_AUDIO_PATCH;
            dtv_audio_context->instances[index].dtv_audio_info.playback_mode = NORMAL_MODE;
            dtv_audio_context->instances[index].dtv_audio_info.volume = 1.0f;
            dtv_audio_context->instances[index].dtv_audio_info.tv_mute = 0;
        }
    }

    pthread_mutex_init(&dtv_obj->dtv_lock, NULL);
#endif

    dtv_obj->dtv_sound_mode = 0;
    /* dtv_volume init , range [0, 1]*/
    dtv_obj->dtv_volume = 1.0;
    return ret;
}

int destroy_dtv_object(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = get_dtv_object(adev);
    if (!dtv_obj) {
        ALOGW("%s() Warning, dtv_obj = NULL!", __func__);
        return -EINVAL;
    }

    //free aml_dtv_audio_instances
    if (dtv_obj->aml_dtv_audio_context) {
#if ENABLE_DVB_PATCH
        aml_dtv_audio_context_t *dtv_audio_context = (aml_dtv_audio_context_t *)dtv_obj->aml_dtv_audio_context;
        for (int index = 0; index < DVB_DEMUX_SUPPORT_MAX_NUM; index ++) {
            aml_dtvsync_t *dtvsync = &dtv_audio_context->instances[index].dtvsync;
            pthread_mutex_destroy(&dtvsync->ms_lock);
        }
        release_dtv_cmd_process_thread(dtv_audio_context);
        aml_audio_free(dtv_obj->aml_dtv_audio_context);
        pthread_mutex_destroy(&dtv_obj->dtv_lock);
        dtv_obj->aml_dtv_audio_context = NULL;
#endif
    }

    //free dtv_obj
    free(dtv_obj);
    return 0;
}
