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

#define LOG_TAG "audio_hw_device_patchmgr"

#include <sys/types.h>
#include <sys/errno.h>
#include <pthread.h>
#include <cutils/log.h>
#include <cutils/atomic.h>


#include "audio_hw.h"
#include "aml_audio_stream.h"
#include "audio_hw_resource_def.h"
#include "device_patch_mgr.h"
#include "device_patch.h"
#include "dtv_patch.h"
#include "tv_patch.h"
#include "aml_ng.h"
#include "audio_hw_utils.h"
#include "audio_hw_resource_mgr.h"
#include "alsa_device_parser.h"
#include "component_picture_mode.h"
#include "component_noise_gate.h"
#include "dtv_private_object.h"
#include "tv_private_object.h"

struct patch_manager;
typedef int (*create_patch_t)(struct patch_manager *, int, audio_devices_t, audio_devices_t, int, audio_patch_handle_t *);
typedef int (*release_patch_t)(struct patch_manager *, int, aml_audio_patch_handle_t);

#define PATCH_TYPE_TO_OFFSET(type) (type)

typedef struct patch_manager
{
    struct aml_audio_device *adev;
    volatile int32_t next_unique_ID;
    // device to device patch context
    struct listnode patch_list;
    // number of patch instance
    int count;
    // patch running flag
    bool audio_patching;
    // Only used for DTV
    bool patch_start;
    // used only for real TV source
    enum patch_src_assort patch_src;
    // Which HW inport is selected for data source
    enum IN_PORT inport;
    bool valid;
    pthread_mutex_t lock;

    // operations of patch manger
    create_patch_t create_patch;
    release_patch_t release_patch;

    //object from adev only used by dtv case
    struct dtv_private_object *dtv_obj;
    //object from adev only used by tv case
    struct tv_private_object *tv_obj;
    // Others not directly related to patch from adev
    struct component_noise_gate noise_gate;
    struct component_picture_mode pic_mode;
    bool tv_have_exit;
    bool atv_dtv_switch;
    //patch source of audio patch
    enum patch_src_assort active_patch_source;
    //patch source status of audio patch
    bool patch_is_active_status;
} patch_manager;


//Patch manger APIs
static inline void acquire_patch_mgr_lock(struct patch_manager *patch_mgr)
{
    pthread_mutex_lock(&patch_mgr->lock);
}

static inline void release_patch_mgr_lock(struct patch_manager *patch_mgr)
{
    pthread_mutex_unlock(&patch_mgr->lock);
}
struct listnode *get_patch_list_from_mgr( struct aml_audio_device *aml_dev) {
   struct patch_manager *pm_tmp = aml_dev->patch_manager;
   return &pm_tmp->patch_list;

}


/*return first audiopatch*/
struct aml_audio_patch *get_patch_from_mgr(struct patch_manager *patch_mgr)
{
    struct audio_patch_set *patch_set = NULL;
    struct listnode *node = NULL;
    struct audio_patch *patch_tmp = NULL;
    //ALOGI("get_patch_from_mgr %d",list_empty(&patch_mgr->patch_list));
    /* find audio_patch in patch_set list */
    list_for_each(node, &patch_mgr->patch_list) {
       patch_set = node_to_item(node, struct audio_patch_set, list_node);
       if (patch_set) {
          patch_tmp = &patch_set->audio_patch;
          if ((patch_tmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
               patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) ||
               (patch_tmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
               patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_MIX)) {
               break;
            }
       }
    }
    //ALOGI("patch %p", patch);
    if (patch_set) {
        return patch_set->aml_audio_patch;
    } else {
        return NULL;
    }
}

int get_audio_patch_by_src_dev(struct audio_hw_device *dev, audio_devices_t dev_type, struct audio_patch **p_audio_patch)
{
    struct aml_audio_device *aml_dev = (struct aml_audio_device *) dev;
    struct patch_manager *pm_tmp = aml_dev->patch_manager;
    struct listnode *node = NULL;
    struct audio_patch_set *patch_set_tmp = NULL;
    struct audio_patch *patch_tmp = NULL;

    list_for_each(node, &pm_tmp->patch_list) {
        patch_set_tmp = node_to_item(node, struct audio_patch_set, list_node);
        patch_tmp = &patch_set_tmp->audio_patch;
        if (patch_tmp->sources[0].ext.device.type == dev_type) {
            ALOGI("%s, patch_tmp->id = %d, dev_type = %ud", __func__, patch_tmp->id, dev_type);
            *p_audio_patch = patch_tmp;
            break;
        }
    }
    return 0;
}

struct aml_audio_patch *get_patch_by_handle_from_mgr(struct patch_manager *patch_mgr, aml_audio_patch_handle_t handle)
{

      if (list_empty(&patch_mgr->patch_list)) {
          ALOGE("No patch in list to release");
          return NULL;
      }
      struct listnode *node = NULL;
      struct audio_patch_set *patch_set = NULL;
      struct audio_patch *patch = NULL;
      void *aml_audio_patch = NULL;
      /* find audio_patch in patch_set list */
      list_for_each(node, &patch_mgr->patch_list) {
          patch_set = node_to_item(node, struct audio_patch_set, list_node);
          patch = &patch_set->audio_patch;
          aml_audio_patch = patch_set->aml_audio_patch;
          if (patch->id == handle) {
              AM_LOGI("patch set found id %d, patchset %p", patch->id, patch_set);
              break;
          } else {
              patch_set = NULL;
              patch = NULL;
          }
      }

      if (!patch_set || !patch || !aml_audio_patch) {
          AM_LOGE("Can't get patch in list");
      }
      return aml_audio_patch;

}


struct audio_patch_set *get_patch_set_by_handle(struct patch_manager *patch_mgr, aml_audio_patch_handle_t handle)
{
      if (list_empty(&patch_mgr->patch_list)) {
          ALOGE("No patch in list to release");
          release_patch_mgr_lock(patch_mgr);
          return NULL;
      }
      struct audio_patch_set *patch_set = NULL;
      struct audio_patch *patch = NULL;
      struct listnode *node = NULL;
      /* find audio_patch in patch_set list */
      list_for_each(node, &patch_mgr->patch_list) {
          patch_set = node_to_item(node, struct audio_patch_set, list_node);
          patch = &patch_set->audio_patch;
          if (patch->id == handle) {
              AM_LOGI("patch set found id %d, patchset %p", patch->id, patch_set);
              break;
          } else {
              patch_set = NULL;
              patch = NULL;
          }
      }

      return patch_set;

}

static const char* patch_type_to_str(int patch_type)
{
    switch (patch_type)
    {
    case PATCH_TYPE_TV:
        return "TV";
    case PATCH_TYPE_DTV:
        return "DTV";
    default:
        return "unknown";
        break;
    }
}

static inline bool is_patch_exist_mgr(struct patch_manager *patch_mgr)
{
    struct listnode *node = NULL;
    struct audio_patch_set *patch_set_tmp = NULL;
    struct audio_patch *patch_tmp = NULL;
    list_for_each(node, &patch_mgr->patch_list) {
        patch_set_tmp = node_to_item(node, struct audio_patch_set, list_node);
        patch_tmp = &patch_set_tmp->audio_patch;
        if ((patch_tmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
            patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) ||
            ((patch_tmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
            patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_MIX))) {
            return true;
        }
    }
    return false;
}

static inline bool is_patch_valid_mgr(struct patch_manager *patch_mgr)
{
    return (patch_mgr->valid ? true : false);
}

static inline bool is_patch_running_mgr(struct patch_manager *patch_mgr)
{
    return (patch_mgr->audio_patching ? true : false);
}

static inline void set_patch_running_mgr(struct patch_manager *patch_mgr, bool enable)
{
    patch_mgr->audio_patching = enable;
}

static inline bool is_same_patch_source_mgr(struct patch_manager *patch_mgr, enum patch_src_assort patch_src)
{
    return (patch_mgr->patch_src == patch_src ? true : false);
}

static inline void set_patch_source_mgr(struct patch_manager *patch_mgr, enum patch_src_assort patch_src)
{
    patch_mgr->patch_src = patch_src;
}

static inline enum patch_src_assort get_patch_source_mgr(struct patch_manager *patch_mgr)
{
    return patch_mgr->patch_src;
}

static inline void invalidate_patch_mgr(struct patch_manager *patch_mgr)
{
    patch_mgr->valid = false;
}

static inline void validate_patch_mgr(struct patch_manager *patch_mgr)
{
    patch_mgr->valid = true;
}

static inline void start_dtv_patch_mgr(struct patch_manager *patch_mgr)
{
    patch_mgr->patch_start = true;
}

static inline void stop_dtv_patch_mgr(struct patch_manager *patch_mgr)
{
    patch_mgr->patch_start = false;
}

static inline bool is_dtv_patch_exist_mgr(struct patch_manager *patch_mgr)
{
    struct aml_audio_patch *patch = NULL;
    struct listnode *node = NULL;
    struct audio_patch_set *patch_set = NULL;
    void *aml_audio_patch = NULL;
    /* find audio_patch in patch_set list */
    list_for_each(node, &patch_mgr->patch_list) {
       patch_set = node_to_item(node, struct audio_patch_set, list_node);
       if (patch_set) {
           patch = patch_set->aml_audio_patch;
           if (patch && patch->patch_src == SRC_DTV) {
                break;
            }
       }
    }
    return patch!=NULL;
}


void do_patch_source_routing(struct patch_manager *patch_mgr, audio_devices_t input_device)
{
    enum input_source input_src = android_input_dev_convert_to_hal_input_src(input_device);
    enum IN_PORT inport = INPORT_HDMIIN;
    int ret = 0;

    ret = do_input_device_routing(patch_mgr->adev, input_device, true);
    //select source signal for selected in port
    if (input_src != SRC_NA) {
        set_audio_source_routing(patch_mgr->adev, input_src);
    }
}

static int create_patch_internal(struct patch_manager *patch_mgr,
                                 int patch_src,
                                 audio_devices_t src_device,
                                 audio_devices_t sink_device,
                                 int type,
                                 aml_audio_patch_handle_t *handle)
{
    int ret = 0;
    int inport;
    struct audio_patch_set *patch_set_new = NULL;
    struct audio_patch *patch_new = NULL;
    void *aml_audio_patch = NULL;

    ALOGI("%s() type:%s patch_src:%s in_device:0x%x out_device:0x%x", __func__,
        patch_type_to_str(type), patchSrc2Str(patch_src), src_device, sink_device);

    acquire_patch_mgr_lock(patch_mgr);
    ret = android_dev_convert_to_hal_dev(src_device, (int *)&inport);
    if ((patch_src == SRC_INVAL) || ret < 0) {
        ALOGD("%s() Invalid patch_src:%s in_device:0x%x return!", __func__, patchSrc2Str(patch_src), src_device);
        ret = -EINVAL;
        goto exit;
    }

    //2.set patch source
    patch_mgr->patch_src = patch_src;

    //3.do source device routing and gain setting
    set_input_device_avail(patch_mgr->adev, src_device, true);
    set_inport_gain(patch_mgr->adev, inport, 1.0);
    do_patch_source_routing(patch_mgr, src_device);

    patch_set_new  = get_patch_set_by_handle(patch_mgr, *handle);
    if (!patch_set_new) {
        ALOGE("%s() Error! patch_set_new NULL ", __func__);
        return -1;
    }

    // 4.Create new request patch
    switch (type)
    {
    case PATCH_TYPE_TV:
        ret = create_tv_patch((struct aml_audio_patch **)&patch_set_new->aml_audio_patch, src_device, sink_device);
        if (ret == 0) {
            set_patch_running_mgr(patch_mgr, true);
        }
        break;
#ifdef ENABLE_DVB_PATCH
    case PATCH_TYPE_DTV:
        ret = create_dtv_patch((struct aml_audio_patch **)&patch_set_new->aml_audio_patch,
                 AUDIO_DEVICE_IN_TV_TUNER, AUDIO_DEVICE_OUT_SPEAKER);
        if (ret == 0) {
            set_patch_running_mgr(patch_mgr, true);
            set_patch_source_mgr(patch_mgr, SRC_DTV);
        }
        break;
#endif
    default:
        ALOGE("%s() Error! Unknown patch_type:%d", __func__, type);
        break;
    }

    // 5.init noise gate for TV if needed
    if (src_device == AUDIO_DEVICE_IN_LINE) {
        struct component_noise_gate *noise_gate = &patch_mgr->noise_gate;
        if (noise_gate->aml_ng_enable) {
            noise_gate->aml_ng_handle = init_noise_gate(noise_gate->aml_ng_level,
                                                        noise_gate->aml_ng_attack_time,
                                                        noise_gate->aml_ng_release_time);
            ALOGE("%s: init amlogic noise gate: level: %fdB, attack_time = %dms, release_time = %dms",
                  __func__, noise_gate->aml_ng_level, noise_gate->aml_ng_attack_time, noise_gate->aml_ng_release_time);
        }
    }

exit:
    release_patch_mgr_lock(patch_mgr);
    return ret;
}

int release_patch_internal(struct patch_manager *patch_mgr, int type, aml_audio_patch_handle_t handle)
{
    int ret = 0;

    struct aml_audio_patch *patch = NULL;
    ALOGI("%s() type:%s patch_src:%s ",__func__,
        patch_type_to_str(type), patchSrc2Str(patch_mgr->patch_src));
    acquire_patch_mgr_lock(patch_mgr);
    patch = get_patch_by_handle_from_mgr(patch_mgr, handle);
    if (!patch) {
       ALOGE("%s() no patch instance for handle:%d", __func__, handle);
       release_patch_mgr_lock(patch_mgr);
       return 0;
    }

    switch (type)
    {
    case PATCH_TYPE_TV:

       do_input_device_routing(patch_mgr->adev, patch->input_src, false);
       ret = release_tv_patch(patch);

        break;
    case PATCH_TYPE_DTV:
#ifdef ENABLE_DVB_PATCH
       do_input_device_routing(patch_mgr->adev, patch->input_src, false);
       ret = release_dtv_patch(patch);
#endif
        break;
    default:
        ALOGE("%s() Error! Unknown patch_type:%d", __func__, type);
        break;
    }
    set_patch_running_mgr(patch_mgr, false);
    set_patch_source_mgr(patch_mgr, SRC_INVAL);
    release_patch_mgr_lock(patch_mgr);

    return ret;
}

/******************************************
 * patch APIs wrap for adev
 ********************************************/

struct component_picture_mode *get_pic_mode_instance(struct aml_audio_device *adev)
{
    return &adev->patch_manager->pic_mode;
}

struct component_noise_gate *get_noise_gate_instance(struct aml_audio_device *adev)
{
    return &adev->patch_manager->noise_gate;
}

struct dtv_private_object *get_dtv_object(struct aml_audio_device *adev)
{
    struct dtv_private_object *dtv_obj = adev->patch_manager->dtv_obj;
    if (!dtv_obj) {
        dtv_obj = aml_audio_calloc(1, sizeof(struct dtv_private_object));
        adev->patch_manager->dtv_obj = dtv_obj;
        if (!dtv_obj) {
            ALOGE("%s() error, No memory!", __func__);
        }
    }
    return dtv_obj;
}

struct tv_private_object *get_tv_object(struct aml_audio_device *adev)
{
    struct tv_private_object *tv_obj = adev->patch_manager->tv_obj;
    if (!tv_obj) {
        tv_obj = aml_audio_calloc(1, sizeof(struct tv_private_object));
        adev->patch_manager->tv_obj = tv_obj;
        if (!tv_obj) {
            ALOGE("%s() Error, No memory!", __func__);
        }
    }
    return tv_obj;
}

bool is_dev_patch_exist(struct aml_audio_device *adev)
{
    return is_patch_exist_mgr(adev->patch_manager);
}


bool is_dtv_patch_exist(struct aml_audio_device *adev)
{
    return is_dtv_patch_exist_mgr(adev->patch_manager);
}

struct aml_audio_patch *get_dev_patch(struct aml_audio_device *adev)
{
    return get_patch_from_mgr(adev->patch_manager);
}
#if 0
void set_dev_patch(struct aml_audio_device *adev, struct aml_audio_patch *audio_patch)
{
    add_patch_to_mgr(adev->patch_manager, audio_patch);
}
#endif

bool is_dev_patch_running(struct aml_audio_device *adev)
{
    return is_patch_running_mgr(adev->patch_manager);
}

void set_dev_patch_running(struct aml_audio_device *adev, bool enable)
{
    set_patch_running_mgr(adev->patch_manager, enable);
}

void set_dev_patch_src(struct aml_audio_device *adev, enum patch_src_assort patch_src)
{
    set_patch_source_mgr(adev->patch_manager, patch_src);
}

int get_dev_patch_src(struct aml_audio_device *adev)
{
    return get_patch_source_mgr(adev->patch_manager);
};

bool is_same_patch_src(struct aml_audio_device *adev, enum patch_src_assort patch_src)
{
    return is_same_patch_source_mgr(adev->patch_manager, patch_src);
}

bool is_dev_patch_valid(struct aml_audio_device *adev)
{
    return is_patch_valid_mgr(adev->patch_manager);
}

void invalidate_dev_patch(struct aml_audio_device *adev)
{
    invalidate_patch_mgr(adev->patch_manager);
}

void validate_dev_patch(struct aml_audio_device *adev)
{
    validate_patch_mgr(adev->patch_manager);
}

void start_dtv_patch(struct aml_audio_device *adev)
{
    start_dtv_patch_mgr(adev->patch_manager);
}

void stop_dtv_patch(struct aml_audio_device *adev)
{
    stop_dtv_patch_mgr(adev->patch_manager);
}

void acquire_dev_patch_lock(struct aml_audio_device *adev)
{
    acquire_patch_mgr_lock(adev->patch_manager);
}

void release_dev_patch_lock(struct aml_audio_device *adev)
{
    release_patch_mgr_lock(adev->patch_manager);
}

enum patch_src_assort get_patch_source(struct aml_audio_device *adev __unused, audio_devices_t src_device, int route_type __unused)
{
    enum patch_src_assort patch_source;
    int inport;
    int ret;

    ret = android_dev_convert_to_hal_dev(src_device, (int *)&inport);
    if (ret < 0) {
        ALOGD("%s() Not support src_device:0x%x return!", __func__, src_device);
        return SRC_INVAL;
    }

    if (inport != INPORT_ECHO_REFERENCE && inport != INPORT_BUILTIN_MIC) {
        patch_source = android_input_dev_convert_to_hal_patch_src(src_device);
    } else {
        patch_source = SRC_INVAL;
        ALOGW("%s() Warning! Not support inport:%s", __func__, inputPort2Str(inport));
    }

    return patch_source;
}

//impl but not used
int get_patch_type(struct aml_audio_device *adev __unused, int inport, enum patch_src_assort patch_src, enum patch_route_e route_type)
{
    int patch_type;

    switch (route_type) {
    case PATCH_ROUTE_DEV_DEV:
        if ((inport != INPORT_TUNER) || ((inport == INPORT_TUNER) && (patch_src == SRC_ATV))) {
            patch_type = PATCH_TYPE_TV;
        } else if ((inport == INPORT_TUNER) && (patch_src == SRC_DTV)) {
            patch_type = PATCH_TYPE_DTV;
        } else {
            patch_type = PATCH_TYPE_INVAL;
        }
        break;
    case PATCH_ROUTE_DEV_MIX:
        if (inport == INPORT_HDMIIN ||
            inport == INPORT_ARCIN  ||
            inport == INPORT_SPDIF  ||
            inport == INPORT_LINEIN ||
            ((inport == INPORT_TUNER) && (patch_src == SRC_ATV))) {
            patch_type = PATCH_TYPE_TV;
        } else if ((inport == INPORT_TUNER) && (patch_src == SRC_DTV)){
            patch_type = PATCH_TYPE_DTV;
        } else {
            patch_type = PATCH_TYPE_INVAL;
        }
        break;
    default:
        ALOGI("%s() Warning, unsupport patch_src:%s ");
        patch_type = PATCH_TYPE_INVAL;
        break;
    }

    return patch_type;
}

struct patch_manager *get_patch_manager(struct aml_audio_device *adev)
{
    if (adev->patch_manager == NULL)
    {
        struct patch_manager *mgr = aml_audio_calloc(1, sizeof(struct patch_manager));
        adev->patch_manager = mgr;
    }

    return adev->patch_manager;
}

static bool is_contain_d2d_patch(struct aml_audio_device *adev, struct audio_patch *unused_patch)
{
    struct listnode *node = NULL;
    struct audio_patch_set *patch_set_tmp = NULL;
    struct audio_patch *patch_tmp = NULL;
    struct patch_manager *pm_tmp = adev->patch_manager;
    /* find if mix->dev / dev->mix exists and remove from list */
    list_for_each(node, &pm_tmp->patch_list) {
        patch_set_tmp = node_to_item(node, struct audio_patch_set, list_node);
        patch_tmp = &patch_set_tmp->audio_patch;
        if ((unused_patch != NULL && unused_patch == patch_tmp) ||
            (patch_tmp->sources[0].ext.device.type == AUDIO_DEVICE_IN_TV_TUNER_DTV)) {
            continue;
        }
        if (patch_tmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
            patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
            return true;
        }
    }
    return false;
}

/* remove audio patch from dev list */
static int unregister_audio_patch(struct audio_hw_device *dev __unused,
                                struct audio_patch_set *patch_set)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, patch_set,);
#ifdef DEBUG_PATCH_SET
    dump_audio_patch_set(patch_set);
#endif
    AM_LOGI("delete the Patch: %d", patch_set->audio_patch.id);
    list_remove(&patch_set->list_node);
    aml_audio_free(patch_set);
    return 0;
}

/* add new audio patch to dev list */
static struct audio_patch_set *register_audio_patch(struct audio_hw_device *dev,
                                                unsigned int num_sources,
                                                const struct audio_port_config *sources,
                                                unsigned int num_sinks,
                                                const struct audio_port_config *sinks,
                                                audio_patch_handle_t *handle)
{
    /* init audio patch */
    struct aml_audio_device *aml_dev = (struct aml_audio_device *)dev;
    struct patch_manager *pm_tmp = aml_dev->patch_manager;
    struct audio_patch_set *patch_set_new = NULL;
    struct audio_patch *patch_new = NULL;
    struct audio_patch_set *patch_set_tmp = NULL;
    struct audio_patch *patch_tmp = NULL;
    struct listnode *node = NULL;

    patch_set_new = aml_audio_calloc(1, sizeof(struct audio_patch_set));
    R_CHECK_POINTER_LEGAL(NULL, patch_set_new, "no memory");

    patch_new = &patch_set_new->audio_patch;

    /* init audio patch new */
    patch_new->num_sources = num_sources;
    memcpy(patch_new->sources, sources, num_sources * sizeof(struct audio_port_config));
    patch_new->num_sinks = num_sinks;
    memcpy(patch_new->sinks, sinks, num_sinks * sizeof (struct audio_port_config));
#ifdef DEBUG_PATCH_SET
    ALOGD("%s(), patch set new to register:", __func__);
    dump_audio_patch_set(patch_set_new);
#endif

    /* find if mix->dev / dev->mix exists and remove from list */
    list_for_each(node, &pm_tmp->patch_list) {
        patch_set_tmp = node_to_item(node, struct audio_patch_set, list_node);
        patch_tmp = &patch_set_tmp->audio_patch;
        if (patch_tmp->sources[0].type == AUDIO_PORT_TYPE_MIX &&
            patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE &&
            sources[0].ext.mix.handle == patch_tmp->sources[0].ext.mix.handle) {
            if (audio_patches_are_equal(patch_tmp, patch_new)) {
#if !defined(AUDIOHAL_ENABLE_AIDL)// handle was created in module for aidl service
                *handle = patch_tmp->id;
#endif
                AM_LOGI("Patch %d:id:%d mix(io:%d)->dev_0[%s(id:%d)] found, register the same patch, do nothing.",
                    *handle, patch_tmp->id, patch_tmp->sources[0].ext.mix.handle,
                    audioDevType2Str(patch_tmp->sinks[0].ext.device.type), patch_tmp->sinks[0].id);
                aml_audio_free(patch_set_new);
                return patch_set_tmp;
            } else {
                AM_LOGI("Patch %d:id:%d mix(io:%d)->dev_0[%s(id:%d)] found, remove it, and register new patch.", *handle,patch_tmp->id,
                    patch_tmp->sources[0].ext.mix.handle, audioDevType2Str(patch_tmp->sinks[0].ext.device.type), patch_tmp->sinks[0].id);
                unregister_audio_patch(dev, patch_set_tmp);
                break;
            }
        } else if (patch_tmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
            patch_tmp->sinks[0].type == AUDIO_PORT_TYPE_MIX &&
            sinks[0].ext.mix.handle == patch_tmp->sinks[0].ext.mix.handle) {
            AM_LOGI("Patch %d:id:%d dev_0[%s(id:%d)]->mix(io:%d) found, remove it, and register new patch.", *handle,patch_tmp->id,
                 audioDevType2Str(patch_tmp->sources[0].ext.device.type), patch_tmp->sources[0].id, patch_tmp->sources[0].ext.mix.handle);
            unregister_audio_patch(dev, patch_set_tmp);
            break;
        }
    }
#if !defined(AUDIOHAL_ENABLE_AIDL)// handle was created in module for aidl service
    *handle = (audio_patch_handle_t) android_atomic_inc(&pm_tmp->next_unique_ID);
#endif
    patch_new->id = *handle;
    /* add new patch set to dev patch list */
    list_add_head(&pm_tmp->patch_list, &patch_set_new->list_node);

    /* audio patch data of mix->dev is mixed in audio patch of dev->dev.
       Routing devices is obtained by dev->dev sinks. */
    if (sinks[0].type == AUDIO_PORT_TYPE_DEVICE &&
        !(sources->type == AUDIO_PORT_TYPE_MIX && is_contain_d2d_patch(aml_dev, NULL))) {
        audio_devices_t out_devices = 0;
        for (int i = 0; i < num_sinks; i++) {
            audio_devices_t sink = sinks[i].ext.device.type;
            /* we think EARC is ARC device. */
            if (sink == AUDIO_DEVICE_OUT_HDMI_EARC) {
                sink = AUDIO_DEVICE_OUT_HDMI_ARC;
            }
            out_devices |= sink;
        }

        //No need to do routing when start dummy_output -> earpiece
        if (sinks[0].ext.device.type != AUDIO_DEVICE_OUT_EARPIECE) {
            aml_audio_output_routing(aml_dev, out_devices);
        }
    }
    return patch_set_new;
}


int init_patch_manager(struct aml_audio_device *adev)
{
    int ret = 0;
    struct patch_manager *patch_mgr = get_patch_manager(adev);
    if (!patch_mgr)
    {
        ALOGW("%s() error! patch_mgr = NULL!", __func__);
        return -EINVAL;
    }

    //new & init tv_private_object
    ret = init_tv_object(adev);
    if (ret != 0) {
        goto err_exit;
    }

    ret = init_dtv_object(adev);
    if (ret != 0) {
        goto err_exit;
    }

    patch_mgr->adev = adev;
    list_init(&patch_mgr->patch_list);
    patch_mgr->next_unique_ID = 1;
    patch_mgr->audio_patching = false;
    patch_mgr->patch_start = false;
    patch_mgr->patch_src = SRC_INVAL;
    patch_mgr->valid = false;
    patch_mgr->create_patch = create_patch_internal;
    patch_mgr->release_patch = release_patch_internal;
    pthread_mutex_init(&patch_mgr->lock, NULL);

    ALOGI("%s() OK", __func__);
    return 0;

err_exit:
    destroy_tv_object(adev);
    destroy_dtv_object(adev);
    free(patch_mgr);
    adev->patch_manager = NULL;
    ALOGE("%s() Fail!", __func__);
    return -EINVAL;
}

void destroy_patch_manager(struct aml_audio_device *adev)
{
    struct patch_manager *patch_mgr = get_patch_manager(adev);
    if (!patch_mgr)
    {
        ALOGW("%s() error! patch_mgr = NULL!", __func__);
        return;
    }

    destroy_tv_object(adev);
    destroy_dtv_object(adev);

    deinit_noise_gate_wrap(adev);

    patch_mgr->valid = false;
    patch_mgr->audio_patching = false;
    //patch_mgr->audio_patch = NULL;
    pthread_mutex_destroy(&patch_mgr->lock);
    free(patch_mgr);
    adev->patch_manager = NULL;
    ALOGI("%s() done!", __func__);
}

int patch_mgr_create_patch(struct aml_audio_device *aml_dev,
                           unsigned int num_sources,
                           const struct audio_port_config *sources,
                           unsigned int num_sinks,
                           const struct audio_port_config *sinks,
                           audio_patch_handle_t *handle)
{
    patch_manager *patch_mgr = aml_dev->patch_manager;
    struct audio_hw_device *dev =  (struct audio_hw_device *)aml_dev;
    //ret = patch_mgr->create_patch(patch_mgr, patch_source, input, output, type, handle);
    //return ret;

    struct audio_patch_set *patch_set;
    const struct audio_port_config *src_config = sources;
    const struct audio_port_config *sink_config = sinks;
    enum input_source input_src = HDMIIN;
    uint32_t sample_rate = 48000, channel_cnt = 2;
    enum IN_PORT inport = INPORT_HDMIIN;
    unsigned int i = 0;
    int ret = -1;
    int patch_source = 0;

    patch_set = register_audio_patch(dev, num_sources, sources, num_sinks, sinks, handle);
    R_CHECK_POINTER_LEGAL(-ENOMEM, patch_set, "create patch fail");

    AM_LOGI("Patch %d: %s->%s, num_src:%d num_sink:%d patch_src:%s", *handle, audioPortType2Str(src_config->type),
        audioPortType2Str(sink_config->type), num_sources, num_sinks, patchSrc2Str(get_dev_patch_src(aml_dev)));
    if (sink_config->type == AUDIO_PORT_TYPE_DEVICE) /* sink config categorization -1 */
    {
        for (i = 0; i < num_sinks; i++) {
            AM_LOGI("sink[%d]: %s(id:%d)", i, audioDevType2Str(sinks[i].ext.device.type), sinks[i].id);
        }

        /* 1.device to device audio patch. TODO: unify with the android device type */
        if (src_config->type == AUDIO_PORT_TYPE_DEVICE) {

            ret = android_dev_convert_to_hal_dev(src_config->ext.device.type, (int *)&inport);
            if (ret != 0) {
                ALOGE("[%s:%d] device->device patch: unsupport input dev:%#x.", __func__, __LINE__, src_config->ext.device.type);
                ret = -EINVAL;
                unregister_audio_patch(dev, patch_set);
                patch_set = NULL;
            }

            patch_source = get_patch_source(aml_dev, src_config->ext.device.type, PATCH_ROUTE_DEV_DEV);
            AM_LOGI("Patch %d: dev[%s(id:%d)] -> dev_0[%s(id:%d)], patch_src:%s", *handle,
                audioDevType2Str(src_config->ext.device.type), src_config->id,
                audioDevType2Str(sink_config->ext.device.type), sink_config->id, patchSrc2Str(patch_source));
            AM_LOGI("hal input port:%s, all output dev:%#x", inputPort2Str(inport), aml_dev->out_device);
            if (patch_source != SRC_ATV) {
                ret = patch_mgr->create_patch(patch_mgr,
                                              patch_source, src_config->ext.device.type,
                                              aml_dev->cur_out_devices,
                                              (patch_source == SRC_DTV) ? PATCH_TYPE_DTV:PATCH_TYPE_TV,
                                              handle);
                if (ret != 0) {
                    unregister_audio_patch(dev, patch_set);
                    patch_set = NULL;
                    ret = -EINVAL;
                    ALOGE("[%s:%d] create tv patch failed, all_out_devices:%#x.", __func__, __LINE__, aml_dev->cur_out_devices);
                }
            }
        } else if (src_config->type == AUDIO_PORT_TYPE_MIX) {  /* 2. mix to device audio patch */
            AM_LOGI("Patch %d: mix(io:%d) -> dev[%s(id:%d)]", *handle, src_config->ext.mix.handle,
                audioDevType2Str(sink_config->ext.device.type), sink_config->id);
            ret = 0;
        } else {
            AM_LOGE("invalid patch, source error, source:%d(%s)->DEVICE", src_config->type, audioPortType2Str(src_config->type));
            ret = -EINVAL;
            unregister_audio_patch(dev, patch_set);
        }
    }
    else if (sink_config->type == AUDIO_PORT_TYPE_MIX) /* sink config categorization -2 */
    {
        if (src_config->type == AUDIO_PORT_TYPE_DEVICE) { /* 3.device to mix audio patch */
            ret = android_dev_convert_to_hal_dev(src_config->ext.device.type, (int *)&inport);
            if (ret != 0) {
                AM_LOGE("device->mix patch: unsupport input dev:%#x.", src_config->ext.device.type);
                unregister_audio_patch(dev, patch_set);
                patch_set = NULL;
            }
            patch_source = get_patch_source(aml_dev, src_config->ext.device.type, PATCH_ROUTE_DEV_MIX);
            AM_LOGI("Patch %d: dev[%s(id:%d)] -> mix(io:%d), patch_src:%s", *handle, audioDevType2Str(src_config->ext.device.type),
                src_config->id, sink_config->ext.mix.handle, patchSrc2Str(patch_source));
            {
                aml_dev->dev2mix_patch = true;
                ret = patch_mgr->create_patch(patch_mgr,
                                  patch_source, src_config->ext.device.type,
                                  aml_dev->cur_out_devices,
                                  (patch_source == SRC_DTV) ? PATCH_TYPE_DTV:PATCH_TYPE_TV,
                                  handle);
                if (ret) {
                    AM_LOGE("create patch failed, cur out dev:%#x.", aml_dev->cur_out_devices);
                    unregister_audio_patch(dev, patch_set);
                    patch_set = NULL;
                }
            }
            ret = 0;
        } else {
            AM_LOGE("invalid patch, source error, source:%d(%s)->MIX", src_config->type, audioPortType2Str(src_config->type));
            ret = -EINVAL;
            unregister_audio_patch(dev, patch_set);
        }
    }
    else /* sink config categorization -3 */
    {
        AM_LOGE("invalid patch, sink:%d(%s) error", sink_config->type, audioPortType2Str(sink_config->type));
        ret = -EINVAL;
        unregister_audio_patch(dev, patch_set);
    }
    return ret;
}

int patch_mgr_release_patch(struct aml_audio_device *aml_dev, aml_audio_patch_handle_t handle)
{
    int ret = 0;
    //ret = patch_mgr->release_patch(patch_mgr, type, handle);


    struct patch_manager *patch_manager = aml_dev->patch_manager;
    struct audio_patch_set *patch_set = NULL;
    struct audio_patch *patch = NULL;
    struct listnode *node = NULL;
    int patch_source = 0;
    if (list_empty(&patch_manager->patch_list)) {
        AM_LOGE("No patch in list to release");
#ifdef AUDIOHAL_ENABLE_AIDL
        AM_LOGD("return -ENOSYS for aidl hal");
        ret = -ENOSYS;//for aidl hal
#else
        ret = -EINVAL;
#endif
        goto exit;
    }

    /* find audio_patch in patch_set list */
    list_for_each(node, &patch_manager->patch_list) {
        patch_set = node_to_item(node, struct audio_patch_set, list_node);
        patch = &patch_set->audio_patch;
        if (patch->id == handle) {
            break;
        } else {
            patch_set = NULL;
            patch = NULL;
        }
    }
    R_CHECK_POINTER_LEGAL(-EINVAL, patch_set, "Can't get patch id:%d in list", handle);
    R_CHECK_POINTER_LEGAL(-EINVAL, patch, "Can't get patch id:%d in list", handle);

    AM_LOGI("Patch %d: %s->%s patch_src:%s", handle, audioPortType2Str(patch->sources[0].type),
        audioPortType2Str(patch->sinks[0].type), patchSrc2Str(get_dev_patch_src(aml_dev)));
    //1.Release device to device patch
    if (patch->sources[0].type == AUDIO_PORT_TYPE_DEVICE) {
        /* aml_dev patch doesn't match the released patch, go to exit */
        audio_devices_t release_src_dev = patch->sources[0].ext.device.type;
        struct aml_audio_patch *aml_patch = get_dev_patch(aml_dev);
        patch_source = get_patch_source(aml_dev, release_src_dev, PATCH_ROUTE_DEV_DEV);
        if (aml_patch) {
#if 0
            if (aml_patch->is_dvi_signal && release_src_dev == AUDIO_DEVICE_IN_HDMI) {
                release_src_dev = AUDIO_DEVICE_IN_LINE;
            }
#endif
            if (aml_patch->input_src != release_src_dev) {
                AM_LOGW("src device:%s audio patch not found", audioDevType2Str(release_src_dev));
                goto exit_unregister;
            }
        }

        if (patch->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
            AM_LOGI("Patch %d: dev[%s(id:%d)] -> dev_0[%s(id:%d)]", handle, audioDevType2Str(patch->sources[0].ext.device.type),
                patch->sources[0].id, audioDevType2Str(patch->sinks[0].ext.device.type), patch->sinks[0].id);
            ret = patch_manager->release_patch(patch_manager,
                                               (patch_source == SRC_DTV) ? PATCH_TYPE_DTV:PATCH_TYPE_TV,
                                               handle);
        } else if (patch->sinks[0].type == AUDIO_PORT_TYPE_MIX) {
            AM_LOGI("Patch %d: dev[%s(id:%d)] -> mix(io:%d)", handle, audioDevType2Str(patch->sources[0].ext.device.type),
                patch->sources[0].id, patch->sinks[0].ext.mix.handle);
        } else {
            AM_LOGW("Unsupported patches");
        }
    } else if (patch->sources[0].type == AUDIO_PORT_TYPE_MIX) {
        if (patch->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
            AM_LOGI("Patch %d: mix(io:%d) -> dev_0[%s(id:%d)]", handle, patch->sources[0].ext.mix.handle,
                audioDevType2Str(patch->sinks[0].ext.device.type), patch->sinks[0].id);
        } else {
            AM_LOGW("Unsupported patches");
        }
    } else {
        AM_LOGW("Unsupported patches");
    }

    //2.Release device to Mix patch
    if (patch->sources[0].type == AUDIO_PORT_TYPE_DEVICE
        && patch->sinks[0].type == AUDIO_PORT_TYPE_MIX) {

        ret = patch_manager->release_patch(patch_manager,
                                           (patch_source == SRC_DTV) ? PATCH_TYPE_DTV:PATCH_TYPE_TV,
                                           handle);

        if (is_dev_patch_running(aml_dev)) {
            ALOGI("patch src reset to  DTV now line= %d \n", __LINE__);
            //aml_dev->patch_src = SRC_DTV;
            set_input_device_avail(aml_dev, AUDIO_DEVICE_IN_TV_TUNER, true);
        }
        aml_dev->dev2mix_patch = false;
    }
exit_unregister:
    unregister_audio_patch(&aml_dev->hw_device, patch_set);
exit:
    return ret;
}

int set_tv_source_switch_parameters(struct audio_hw_device *dev, struct str_parms *parms)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    struct patch_manager *patch_manager = adev->patch_manager;
    int ret = -1;
    char value[64] = {'\0'};

    /*----ATV <-> DTV switch----*/
    ret = str_parms_get_str(parms, "hal_param_tuner_in", value, sizeof(value));
    // tuner_in=atv: tuner_in=dtv
    if (ret >= 0 && is_TV(adev)) {

        if (strncmp(value, "atv", 3) == 0) {
            ALOGI("[audiohal_kpi] %s, create atv patching", __func__);
            struct audio_patch *pAudPatchTmp = NULL;
            get_audio_patch_by_src_dev(dev, AUDIO_DEVICE_IN_TV_TUNER, &pAudPatchTmp);
            if (pAudPatchTmp == NULL) {
                ALOGE("%s,There is no audio patch using tuner as input", __func__);
                goto exit;
            }
            ret = patch_manager->create_patch(patch_manager,
                            SRC_ATV,
                            AUDIO_DEVICE_IN_TV_TUNER,
                            pAudPatchTmp->sinks[0].ext.device.type,
                            PATCH_TYPE_TV,
                            &pAudPatchTmp->id);
            if (ret) {
                AM_LOGE("create patch failed, cur out dev:%#x.", adev->cur_out_devices);
            }
        }
        goto exit;
    }

    /*----HDMIIN <-> LINEIN switch----*/
    ret = str_parms_get_str(parms, "audio", value, sizeof(value));
    if (ret >= 0) {
        /*
         * This is a work around when plug in HDMI-DVI connector
         * first time application only recognize it as HDMI input device
         * then it can know it's DVI in, and then send "audio=linein" message to audio hal
         */
        struct audio_patch *pAudPatchTmp = NULL;
        if (strncmp(value, "linein", 6) == 0) {
            get_audio_patch_by_src_dev(dev, AUDIO_DEVICE_IN_HDMI, &pAudPatchTmp);
            if (pAudPatchTmp == NULL) {
                ALOGE("%s,There is no audio patch using HDMI as input", __func__);
                goto exit;
            }
            if (pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI) {
                ALOGE("%s, pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI", __func__);
                goto exit;
            }

            // dev->dev (example: HDMI in-> speaker out)
            if (pAudPatchTmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE
                && pAudPatchTmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
                // This "adev->audio_patch" will be created in create_patch() function
                if (is_dev_patch_exist(adev) && is_same_patch_src(adev, SRC_HDMIIN)) {
                    ALOGI("%s, create hdmi-dvi patching dev->dev", __func__);


                    patch_manager->release_patch(patch_manager, PATCH_TYPE_TV, pAudPatchTmp->id);
                    ret = patch_manager->create_patch(patch_manager,
                                    SRC_LINEIN,
                                    AUDIO_DEVICE_IN_LINE,
                                    pAudPatchTmp->sinks[0].ext.device.type,
                                    PATCH_TYPE_TV,
                                    &pAudPatchTmp->id);
                }
            }

            set_dev_patch_src(adev, SRC_LINEIN);
            pAudPatchTmp->sources[0].ext.device.type = AUDIO_DEVICE_IN_LINE;
            set_audio_source_routing(adev, LINEIN);
        } else if (strncmp(value, "hdmi", 4) == 0 && is_dev_patch_exist(adev)) {

            get_audio_patch_by_src_dev(dev, AUDIO_DEVICE_IN_LINE, &pAudPatchTmp);
            if (pAudPatchTmp == NULL) {
                ALOGE("%s,There is no audio patch using LINEIN as input", __func__);
                goto exit;
            }
            if (pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_LINE) {
                ALOGE("%s, pAudPatchTmp->sources[0].ext.device.type != AUDIO_DEVICE_IN_HDMI", __func__);
                goto exit;
            }

            // dev->dev (example: LINE in -> speaker out)
            if (pAudPatchTmp->sources[0].type == AUDIO_PORT_TYPE_DEVICE
                && pAudPatchTmp->sinks[0].type == AUDIO_PORT_TYPE_DEVICE) {
                // This "adev->audio_patch" will be created in create_patch() function
                if (is_dev_patch_exist(adev) && is_same_patch_src(adev, SRC_LINEIN)) {
                    ALOGI("%s, create dvi-hdmi patching dev->dev", __func__);

                    patch_manager->release_patch(patch_manager, PATCH_TYPE_TV, pAudPatchTmp->id);
                    ret = patch_manager->create_patch(patch_manager,
                                                SRC_HDMIIN,
                                                AUDIO_DEVICE_IN_HDMI,
                                                pAudPatchTmp->sinks[0].ext.device.type,
                                                PATCH_TYPE_TV,
                                                &pAudPatchTmp->id);
                }
            }

            set_dev_patch_src(adev, SRC_HDMIIN);
            pAudPatchTmp->sources[0].ext.device.type = AUDIO_DEVICE_IN_HDMI;
            set_audio_source_routing(adev, HDMIIN);
        }
        goto exit;
    }

exit:
    return ret;
}


void audio_patch_dump(struct audio_patch *patch, int fd)
{
    int i = 0;

    dprintf(fd, " handle %d\n", patch->id);
    for (i = 0; i < patch->num_sources; i++) {
        dprintf(fd, "    [src  %d]\n", i);
        aml_audio_port_config_dump(&patch->sources[i], fd);
    }

    for (i = 0; i < patch->num_sinks; i++) {
        dprintf(fd, "    [sink %d]\n", i);
        aml_audio_port_config_dump(&patch->sinks[i], fd);
    }
}

//1.2 dump all registered android audio patch
void audio_patch_list_dump(struct patch_manager *patch_manager, int fd)
{
    struct audio_patch_set *patch_set = NULL;
    struct audio_patch *patch = NULL;
    struct listnode *node = NULL;
    int i = 0;

    dprintf(fd, "\nAML Audio Patches:\n");
    list_for_each(node, &patch_manager->patch_list) {
        dprintf(fd, "  patch %d:", i);
        patch_set = node_to_item (node, struct audio_patch_set, list_node);
        if (patch_set) {
            audio_patch_dump(&patch_set->audio_patch, fd);
        }
        i++;
    }
}

