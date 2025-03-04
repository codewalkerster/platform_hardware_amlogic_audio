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
#include <errno.h>
#include <fcntl.h>
#include <hardware/hardware.h>
#include <inttypes.h>
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
#include <sys/utsname.h>

#if ANDROID_PLATFORM_SDK_VERSION >= 25 // 8.0
#include <system/audio-base.h>
#endif

#include <hardware/audio.h>
#include <aml_android_utils.h>
#include <aml_data_utils.h>

#include "aml_audio_stream.h"
#include "audio_hw.h"
#include "dtv_patch.h"
#include "audio_hw_utils.h"
#include "audio_hw_ms12.h"
#include "audio_hw_ms12_common.h"
#include "dolby_lib_api.h"
#include "alsa_config_parameters.h"
#include "dtv_patch_hal_avsync.h"
#include "aml_audio_timer.h"
#include "dmx_audio_es.h"
#include "uio_audio_api.h"
#include "dtv_patch_utils.h"
#include "aml_ddp_dec_api.h"
#include "aml_audio_ac3parser.h"
#include "aml_audio_ac4parser.h"
#include "aml_audio_heaacparser.h"
#include "audio_hw_resource_mgr.h"
#include "device_patch.h"
#include "dtv_private_object.h"
#include "aml_stream_manager.h"

static int create_dtv_output_stream_thread(struct aml_dtv_audio_instance *instance);
static int release_dtv_output_stream_thread(struct aml_dtv_audio_instance *instance);
static int create_dtv_input_stream_thread(struct aml_dtv_audio_instance *instance);
static int release_dtv_input_stream_thread(struct aml_dtv_audio_instance *instance);
static ssize_t dtv_stream_out_write(struct audio_stream_out *stream_out,
                                     void *buffer,
                                     size_t bytes);
static ssize_t dtv_audio_package_write(struct package *p_package,
                                         struct aml_dtv_stream_out *stream_out);
static int dtv_patch_handle_event(struct audio_hw_device *dev, int cmd, int val) {

    struct aml_audio_device *adev = (struct aml_audio_device *) dev;
    int ret  = 0;;
    float dtv_volume_switch = 1.0;

    acquire_dtv_mutex_lock(adev);

    unsigned int path_id = val >> DVB_DEMUX_ID_BASE;
    ALOGI("%s path_id %d cmd %d",__FUNCTION__,path_id, cmd);
    if ((int)path_id < 0  ||  path_id >= DVB_DEMUX_SUPPORT_MAX_NUM) {
        ALOGI("path_id %d invalid ! ",path_id);
        goto exit;
    }

    aml_dtv_audio_context_t *context = get_dtv_audio_context(adev);
    aml_dtv_audio_instance_t *dtv_audio_instance =  &context->instances[path_id];
    aml_dtv_audiopara_t *dtv_audio_info = &dtv_audio_instance->dtv_audio_info;
    val = val & ((1 << DVB_DEMUX_ID_BASE) - 1);
    switch (cmd) {
        case AUDIO_DTV_PATCH_CMD_SET_PLAYBACK_MODE:
            ALOGI("DTV playback_mode %d", val);
            if (val == 0) {
                dtv_audio_info->playback_mode = NORMAL_MODE;
            } else if (val == 1) {
                dtv_audio_info->playback_mode = CACHE_MODE;
            } else {
                ALOGI("invalid playback mode %d", val);
            }
            break;
        case AUDIO_DTV_PATCH_CMD_SET_DTV_DEMUX_ID:
            context->dtv_demux_id = val;
            ALOGI("dtv_audio_context->dtv_demux_id %d", val);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_MEDIA_SYNC_ID:
            dtv_audio_info->media_sync_id = val;
            ALOGI("demux_info->media_sync_id  %d", dtv_audio_info->media_sync_id);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_OUTPUT_MODE:
            ALOGI("DTV sound mode %d ", val);
            //FIXME. In the SWPL-173108, when play the special stream, Left and right channel are mixed together
            //and lost some channel information.
            //1. Actually, dtv_output_mode is the acmod(Audio Code Mode) which transmit from the dtvkit
            //2. We Erroneously means that it is equal to the sound track mode.
            //3. If the code mode is equal to mono or dual mono, dtvkit will send LRmix to audohal,
            //therefore, Left and right channel are mixed together.
            //4. Depend on the UI and reference TV, LR mix is useless in the dtv case.Therefore, when the dtv_output_mode is equal to
            //   LR mix, it need be stero output.
            if (val == AM_AOUT_OUTPUT_LRMIX)
                val = AM_AOUT_OUTPUT_STEREO;
            dtv_audio_info->output_mode = val;
            adev->sound_track_mode = val;
            break;
        case AUDIO_DTV_PATCH_CMD_SET_MUTE:
            ALOGE ("Amlogic_HAL - %s: TV-Mute:%d.", __FUNCTION__,val);
            dtv_audio_info->tv_mute = val;
            break;
        case AUDIO_DTV_PATCH_CMD_SET_VOLUME:
            dtv_volume_switch = (float)val / 100; // val range is [0, 100], conversion range is [0, 1]
            if (dtv_audio_info->volume != dtv_volume_switch) {
                dtv_audio_info->volume = dtv_volume_switch;
                ALOGI ("dtv set volume:%f", dtv_audio_info->volume);
            } else {
                ALOGE("[%s:%d] dtv set volume error! volume:%f", __func__, __LINE__, dtv_volume_switch);
            }
            break;
        case AUDIO_DTV_PATCH_CMD_SET_HAS_VIDEO:
            dtv_audio_info->has_video = val;
            ALOGI("has_video %d",dtv_audio_info->has_video);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_DEMUX_INFO:
            dtv_audio_info->demux_id = val;
            ALOGI("demux_id %d",dtv_audio_info->demux_id);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_SECURITY_MEM_LEVEL:
            dtv_audio_info->security_mem_level = val;
            ALOGI("security_mem_level set to %d\n", dtv_audio_info->security_mem_level);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_PID:
            dtv_audio_info->main_pid = val;
            ALOGI("main_pid %d",dtv_audio_info->main_pid);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_FMT:
            dtv_audio_info->main_fmt = val;
            ALOGI("main_fmt %d",dtv_audio_info->main_fmt);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_AD_FMT:
            dtv_audio_info->ad_fmt = val;
            ALOGI("ad_fmt %d",dtv_audio_info->ad_fmt);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_AD_PID:
            dtv_audio_info->ad_pid = val;
            ALOGI("ad_pid %d",dtv_audio_info->ad_pid);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_AD_SUPPORT:
            dtv_audio_info->dual_decoder_support = val;
            ALOGI("dual_decoder_support set to %d\n", dtv_audio_info->dual_decoder_support);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_AD_ENABLE:
            dtv_audio_info->associate_audio_mixing_enable = val;
            ALOGI("associate_audio_mixing_enable set to %d\n", dtv_audio_info->associate_audio_mixing_enable);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_AD_VOL_LEVEL:
            if (val < 0) {
                val = 0;
            }
            else if (val > 100) {
                val = 100;
            }

            dtv_audio_info->advol_level = val;
            ALOGI("advol_level set to %d\n", dtv_audio_info->advol_level);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_AD_MIX_LEVEL:
            if (val < 0) {
                val = 0;
            } else if (val > 100) {
                val = 100;
            }
            dtv_audio_info->mixing_level = (val * 64 - 32 * 100) / 100; //[0,100] mapping to [-32,32]
            ALOGI("mixing_level set to %d\n", dtv_audio_info->mixing_level);
            audio_format_t source_format = aml_fmt_convert_to_android_fmt(dtv_audio_info->main_fmt);
            if (!is_dolby_ms12_support_compression_format(source_format)) {
                 //for shine ad menu dolby low -10 medium 0 high 10 match -6db 0db 6db
                 dtv_audio_info->mixing_level = adapt_mixing_level_db(dtv_audio_info->mixing_level);
            }
            break;
        case AUDIO_DTV_PATCH_CMD_SET_MEDIA_PRESENTATION_ID:

            dtv_audio_info->media_presentation_id = val;
            ALOGI("media_presentation_id %d",dtv_audio_info->media_presentation_id);
            break;
        case AUDIO_DTV_PATCH_CMD_SET_MEDIA_FIRST_LANG:
            dtv_audio_info->media_first_lang = val;
            ALOGI("media_first_lang %0x", val);
            break;

        case AUDIO_DTV_PATCH_CMD_SET_MEDIA_SECOND_LANG:
            dtv_audio_info->media_second_lang = val;
            ALOGI("media_second_lang %0x", val);
            break;

         case AUDIO_DTV_PATCH_CMD_SET_SPDIF_PROTECTION_MODE:
            dtv_audio_set_spdif_protection_mode(val);
            ALOGI("AUDIO SET SPDIF_PROTECTION__STATUS: %d\n", val);
            break;

        case AUDIO_DTV_PATCH_CMD_CONTROL:
            if (val <= AUDIO_DTV_PATCH_CMD_NULL || val > AUDIO_DTV_PATCH_CMD_NUM) {
                ALOGW("[%s:%d] Unsupported dtv patch cmd:%d", __func__, __LINE__, val);
                break;
            }
            ALOGI("[%s:%d] Send dtv patch cmd:%s cmd_id %d", __func__, __LINE__, dtvAudioPatchCmd2Str(val), val);
            pthread_mutex_lock(&context->dtv_cmd_process_mutex);
            dtv_audio_add_cmd(&context->dtv_cmd_list, val, path_id);
            pthread_cond_signal(&context->dtv_cmd_process_cond);
            pthread_mutex_unlock(&context->dtv_cmd_process_mutex);
            break;
        default:
            ALOGI("invalid cmd %d", cmd);
    }

exit:
    release_dtv_mutex_lock(adev);
    return ret;
}

bool is_need_check_ad_substream(struct aml_dtv_audio_instance *instance) {
    bool is_need_check_ad_substream =  ((instance->aformat == AUDIO_FORMAT_E_AC3 ||
                                             instance->aformat == AUDIO_FORMAT_AC3 ) &&
                                           !instance->ad_substream_checked_flag);
    return is_need_check_ad_substream;
}

int audio_dtv_patch_parser_process_write(struct package *p_package,
                                         struct aml_dtv_stream_out *dtv_stream_out)
{
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    struct aml_stream_out *aml_out= dtv_stream_out->stream_out;
    struct audio_stream_out *stream_out = (struct audio_stream_out *)aml_out;
    struct aml_dtv_audio_instance *instance = node_to_item(dtv_stream_out, struct aml_dtv_audio_instance, dtv_stream_out);
    aml_dec_t *aml_dec = aml_out->aml_dec;
    int ret = 0;

    unsigned char *mute_buffer = aml_audio_malloc(DTV_DD_MUTE_FRAME_SIZE);
    if (!mute_buffer) {
        ALOGE("audio_dtv_patch_output_dual_decoder aml_audio_malloc fail");
        return -1;
    }

    aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)aml_out->audio_buffer;
    aml_audio_buffer_t *audioBuffer = pBuffer->inBuffer;
    aml_dtv_audiopara_t *dtv_audio_info = audioBuffer->privObject;

    int parser_used_size = 0;
    int used_size = 0;
    int ad_parser_used_size = 0;
    int ad_used_size = 0;
    char *main_frame_buffer = NULL;
    char *ad_frame_buffer = NULL;
    void *ad_data_buffer = p_package->ad_data;
    int ad_data_size = p_package->ad_size;
    //bool ad_data_valid = ad_data_buffer && (ad_data_size > 0);

    if (!ad_data_size && !aml_out->aml_dec && instance->aformat == AUDIO_FORMAT_E_AC3 && !aml_out->ad_substream_supported) {
        aml_out->ad_substream_supported = is_ad_substream_supported((unsigned char *)p_package->data, p_package->size);
    }

    if (dtv_audio_info->dual_decoder_support) {
        if (!instance->ad_remain_buf) {
            instance->ad_remain_buf = aml_audio_malloc(DTV_AD_BUFFER_SIZE);
            if (!instance->ad_remain_buf)  {
                ALOGI("ad_remain_buf malloc failed !!!");
            }
            instance->ad_remain_size = 0;
        }
        if (p_package->ad_size > DTV_AD_BUFFER_SIZE) {
                ALOGI("p_package->ad_size %d invalid skip", p_package->ad_size);
                ad_data_size = p_package->ad_size = 0;
        }
        if (instance->ad_remain_size)  {
            if (instance->ad_remain_size + p_package->ad_size > DTV_AD_BUFFER_SIZE) {
                ALOGW("ad_remain_size %d + p_package->ad_size %d over flow ,reset ad_remain_size", instance->ad_remain_size, p_package->ad_size);
                instance->ad_remain_size = 0;
            }
            memcpy((char *)instance->ad_remain_buf + instance->ad_remain_size, ad_data_buffer, p_package->ad_size);
            ad_data_buffer = instance->ad_remain_buf;
            ad_data_size =  p_package->ad_size + instance->ad_remain_size;
        }
    }

    if (instance->aformat == AUDIO_FORMAT_AC3 ||
        instance->aformat == AUDIO_FORMAT_E_AC3) {

        if (!instance->ac3_parser_handle) {
            aml_ac3_parser_open(&instance->ac3_parser_handle);
            ALOGI("instance->ac3_parser_handle %p", instance->ac3_parser_handle);
        }
        if (ad_data_size && !instance->ad_ac3_parser_handle) {
            aml_ac3_parser_open(&instance->ad_ac3_parser_handle);
        }

        struct ac3_parser_info ac3_info = { 0 };
        while (p_package->size > used_size && !instance->input_thread_exit) {
            int main_frame_size = 0;
            int ad_frame_size = 0;
            aml_ac3_parser_process(instance->ac3_parser_handle,
                                   p_package->data + used_size,
                                   p_package->size - used_size,
                                   &parser_used_size,
                                   (void *)&main_frame_buffer,
                                   &main_frame_size, &ac3_info);
            if (main_frame_size <= 0) {
                if (aml_dev->debug_flag > 0)
                    ALOGD("do not get main dolby frames !!!");
                break;
            }

            /*only the first frame has the correct pts*/
            if (used_size != 0) {
                audioBuffer->apts = DTVSYNC_INVALID_PTS;
            }  else {
                if (p_package->pts == DTVSYNC_INVALID_PTS) {
                    audioBuffer->apts = instance->dtvsync.last_package_pts;
                }
            }
            used_size += parser_used_size;
            instance->in_read_frame_size = main_frame_size;
            if (aml_dev->debug_flag) {
                ALOGD("p_package->pts %0" PRIx64 "",p_package->pts);
            }

            if (dtv_audio_info->dual_decoder_support && ad_data_size > ad_used_size) {
                aml_ac3_parser_process(instance->ad_ac3_parser_handle,
                                       (char *)ad_data_buffer + ad_used_size,
                                       ad_data_size - ad_used_size,
                                       &ad_parser_used_size,
                                       (void *)&ad_frame_buffer,
                                       &ad_frame_size, &ac3_info);
                ad_used_size += ad_parser_used_size;
            }

            if (aml_dev->debug_flag)
                ALOGD("main size %d p_package->size %d used_size %d",main_frame_size, p_package->size, used_size);

            if (aml_dev->debug_flag)
                ALOGD("ad size %d p_package->ad_size %d ad_used_size %d",ad_frame_size,p_package->ad_size,ad_used_size);
            if (dtv_audio_info->dual_decoder_support) {
                if (ad_frame_buffer && ad_frame_size) {
                    dtv_audio_info->ad_data = ad_frame_buffer;
                    dtv_audio_info->ad_size = ad_frame_size;
                } else {
                      if (instance->aformat == AUDIO_FORMAT_AC3) {
                           dtv_audio_info->ad_size = DTV_DD_MUTE_FRAME_SIZE;
                           dtv_audio_info->ad_data = (char *)mute_buffer;
                           dtv_audio_copy_raw_mute_frame(mute_buffer, AUDIO_FORMAT_AC3);
                      } else if (instance->aformat == AUDIO_FORMAT_E_AC3) {
                           dtv_audio_info->ad_size = DTV_DDP_MUTE_FRAME_SIZE;
                           dtv_audio_info->ad_data = (char *)mute_buffer;
                           dtv_audio_copy_raw_mute_frame(mute_buffer, AUDIO_FORMAT_E_AC3);
                      }
                }
            }
            ret = dtv_stream_out_write(stream_out, main_frame_buffer, main_frame_size);
        }
    }
    else if (is_dolby_ms12_support_compression_format(instance->aformat)&&
        is_aac_format(instance->aformat)) {
        if (!instance->heaac_parser_handle) {
            aml_heaac_parser_open(&instance->heaac_parser_handle);
            if (instance->aformat == AUDIO_FORMAT_AAC_LATM) {
                instance->main_heaac_info.is_loas = 1;
                instance->main_heaac_info.is_adts = 0;
                instance->ad_heaac_info.is_loas = 1;
                instance->ad_heaac_info.is_adts = 0;
            }
            else {
                instance->main_heaac_info.is_loas = 0;
                instance->main_heaac_info.is_adts = 1;
                instance->ad_heaac_info.is_loas = 0;
                instance->ad_heaac_info.is_adts = 1;
            }
        }
        if (ad_data_size && !instance->ad_heaac_parser_handle) {
            aml_heaac_parser_open(&instance->ad_heaac_parser_handle);
        }

        instance->main_heaac_info.debug_print = aml_dev->debug_flag;

        while (p_package->size > used_size && !instance->input_thread_exit) {
            int32_t main_frame_size = 0;
            int32_t ad_frame_size = 0;
            aml_heaac_parser_process(instance->heaac_parser_handle,
                                   p_package->data + used_size,
                                   p_package->size - used_size,
                                   &parser_used_size,
                                   (void *)&main_frame_buffer,
                                   &main_frame_size, &(instance->main_heaac_info));
            if (main_frame_size <= 0) {
                if (aml_dev->debug_flag > 0)
                    ALOGW("do not get main aac frames !!!");
                break;
            }

            /*only the first frame has the correct pts*/
            if (used_size != 0) {
                audioBuffer->apts = DTVSYNC_INVALID_PTS;
            } else {
                if (p_package->pts == DTVSYNC_INVALID_PTS) {
                    audioBuffer->apts = instance->dtvsync.last_package_pts;
               }
            }
            used_size += parser_used_size;
            instance->in_read_frame_size = main_frame_size;
            if (aml_dev->debug_flag) {
                ALOGD("p_package->pts %0" PRIx64 "",p_package->pts);
                ALOGD("main size %d p_package->size %d used_size %d",main_frame_size, p_package->size, used_size);
            }

            if (dtv_audio_info->dual_decoder_support && ad_data_size > ad_used_size) {
                instance->ad_heaac_info.debug_print = aml_dev->debug_flag;

                aml_heaac_parser_process(instance->ad_heaac_parser_handle,
                                       (char *)ad_data_buffer + ad_used_size,
                                       ad_data_size - ad_used_size,
                                       &ad_parser_used_size,
                                       (void *)&ad_frame_buffer,
                                       &ad_frame_size, &(instance->ad_heaac_info));
                ad_used_size += ad_parser_used_size;
                if (aml_dev->debug_flag) {
                    ALOGD("ad frame size %d total data size %d used_size %d",ad_frame_size, ad_data_size, ad_used_size);
                }
            }

            if (ad_frame_buffer && ad_frame_size > 0) {
                dtv_audio_info->ad_data = ad_frame_buffer;
                dtv_audio_info->ad_size = ad_frame_size;
            }

            if (aml_dev->debug_flag) {
                ALOGI("p_package->size %d main_frame_size %d ad p_package->size %d p_package->ad_size %d",
                    p_package->size, main_frame_size, p_package->ad_size, ad_frame_size);
            }

            if (instance->main_heaac_info.is_adts) {
                 aml_out->hal_internal_format = AUDIO_FORMAT_AAC;
            }
            if (instance->main_heaac_info.is_loas) {
                aml_out->hal_internal_format = AUDIO_FORMAT_AAC_LATM;
            }
            ret = dtv_stream_out_write(stream_out, main_frame_buffer, main_frame_size);
        }
    } else if (instance->aformat == AUDIO_FORMAT_AC4) {
        if (!instance->ac4_parser_handle) {
            aml_ac4_parser_open(&instance->ac4_parser_handle);
        }
        void *main_frame_buffer = p_package->data;
        int main_frame_size = p_package->size;
        int used_size = 0;
        int32_t parser_used_size = 0;
        struct ac4_parser_info ac4_info = { 0 };
        aml_ac4_parser_process(instance->ac4_parser_handle, p_package->data, p_package->size, &parser_used_size, &main_frame_buffer, &main_frame_size, &ac4_info);
        ALOGV("frame size =%d frame rate=%d sample rate=%d used =%d", ac4_info.frame_size, ac4_info.frame_rate, ac4_info.sample_rate, parser_used_size);
        if (main_frame_size == 0 && parser_used_size == 0) {
            used_size = p_package->size;
            ALOGE("wrong ac4 frame size");
        }
        if (ac4_info.frame_size) {
            instance->in_read_frame_size = ac4_info.frame_size;
        }
        ret = dtv_stream_out_write(stream_out, p_package->data, p_package->size);
    } else {
         ret = dtv_stream_out_write(stream_out, p_package->data, p_package->size);
         if (ad_data_size) {
            instance->ad_remain_size = 0;
         }
    }

    if (dtv_audio_info->dual_decoder_support) {
        if (ad_data_size >= ad_used_size) {
            if (aml_dev->debug_flag > 0)
              ALOGD("ad_data_size  %d ad_used_size %d", ad_data_size, ad_used_size);
            if (ad_data_size > ad_used_size) {
                memmove((char *)instance->ad_remain_buf, (char *)ad_data_buffer + ad_used_size, ad_data_size - ad_used_size);
            }
            instance->ad_remain_size = ad_data_size - ad_used_size;
        }
    }

exit:
    aml_audio_free(mute_buffer);
    return ret;
}

static ssize_t dtv_audio_package_write(struct package *p_package,
                                         struct aml_dtv_stream_out *dtv_stream_out)
{
    int ret = 0;
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    struct aml_stream_out *stream_out= dtv_stream_out->stream_out;
    struct aml_dtv_audio_instance *instance = node_to_item(dtv_stream_out, struct aml_dtv_audio_instance, dtv_stream_out);
    aml_dtv_audiopara_t *dtv_audio_info = &instance->dtv_audio_info;
    if (!p_package  || !p_package->data || !p_package->size) {
        ret = -1;
        ALOGI("p_package invalid");
        goto free_package;
    }
    aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)stream_out->audio_buffer;
    aml_audio_buffer_t *audioBuffer = pBuffer->inBuffer;
    if (stream_out->audio_buffer && audioBuffer) {
        dtv_audio_info->ad_data = p_package->ad_data;
        dtv_audio_info->ad_size = p_package->ad_size;
        audioBuffer->privObject = dtv_audio_info;
        audioBuffer->apts = p_package->pts;
    }

    if (aml_dev->debug_flag) {
        ALOGD("AD pid %d fade %d  pan %d mixing_level %d advol %d package_pts %"PRIx64" package_ad_pts %"PRIx64"",
            dtv_audio_info->ad_pid,  dtv_audio_info->ad_fade, dtv_audio_info->ad_pan,dtv_audio_info->mixing_level, dtv_audio_info->advol_level,p_package->pts,p_package->ad_pts);
    }
    audio_dtv_patch_parser_process_write(p_package, dtv_stream_out);
free_package:
  if (p_package) {
        if (p_package->data) {
            aml_audio_free(p_package->data);
        }
        if (p_package->ad_data) {
            aml_audio_free(p_package->ad_data);
        }
        aml_audio_free(p_package);
    }
    p_package = NULL;
    return ret;

}
static ssize_t dtv_stream_out_write(struct audio_stream_out *stream_out,
                                     void *buffer,
                                      size_t bytes)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream_out;
    aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)aml_out->audio_buffer;
    aml_audio_buffer_t *audioBuffer = pBuffer->inBuffer;
    struct aml_audio_device *adev = aml_adev_get_handle();
    aml_dtv_audiopara_t *dtv_audio_info = NULL;
    int ret = 0;

    //packet audio buffer
    if (aml_out->audio_buffer && audioBuffer) {
        audioBuffer->isDtv = true;
        dtv_audio_info = (aml_dtv_audiopara_t *)audioBuffer->privObject;
        //audioBuffer->apts = p_package->pts;
        audioBuffer->pData = buffer;
        audioBuffer->size = bytes;
        //audioBuffer->isAptsValid = aml_out->hw_sync_mode;
        //if it is iec stream, maybe it's better to get these format from iec parser.
        audioBuffer->bufFormat.channelCount = audio_channel_count_from_out_mask(aml_out->hal_channel_mask);
        audioBuffer->bufFormat.channelMask = aml_out->hal_channel_mask;
        audioBuffer->bufFormat.format = aml_out->hal_internal_format;
        audioBuffer->bufFormat.sampleRate = aml_out->hal_rate;
        //audioBuffer->privObject = demux_info;
        if (get_debug_value(AML_DUMP_AUDIOHAL_DTV)) {
            aml_dump_audio_bitstreams("/data/audio/audio_main.es",  buffer, bytes);
            aml_dump_audio_bitstreams("/data/audio/audio_ad.es", dtv_audio_info->ad_data, dtv_audio_info->ad_size);
        }
    } else {
        ALOGW("audio_buffer:%p, please check it.", aml_out->audio_buffer);
    }
    if (!is_dolby_ms12_support_compression_format (aml_out->hal_internal_format)
        || eDolbyMS12Lib != adev->dolby_lib_type) {
        if (!aml_out->aml_dec) {
             config_output(stream_out, true);
        }
        dtv_audio_sync_prepare(aml_out->aml_dec, audioBuffer);
    }

    pthread_mutex_lock(&adev->lock);
    adev->active_outputs[aml_out->streamType] = aml_out;
    ret = _get_stream_write_func(aml_out);
    if (ret < 0) {
        AM_LOGE("%s() failed", __func__);
        pthread_mutex_unlock(&adev->lock);
        return ret;
    }
    pthread_mutex_unlock(&adev->lock);
    if (aml_out->write) {
          ret = aml_out->write(stream_out, audioBuffer);
    }

    return ret;
}

static int dtv_uio_read(unsigned char *buffer, int buffer_size, int *exit) {
    int nRet =0, trycount = 0,rlen = 0,nNextReadSize = buffer_size;
    while (nNextReadSize > 0) {
        nRet = uio_read_buffer((unsigned char *)(buffer + rlen), nNextReadSize, exit);
        if (nRet <= 0) {
            trycount++;
            if (trycount == 10) {
                ALOGV("wait %d ms buffer_size %d left %d", 3 * trycount, buffer_size, nNextReadSize);
                break;
            } else {
                ALOGV("wait %d ms buffer_size %d left %d", 3 * trycount, buffer_size, nNextReadSize);
                usleep(3000);
                continue;
            }
        }
        rlen += nRet;
        nNextReadSize -= nRet;
    }
    if (rlen) {
         return 0;
    } else {
         ALOGV("dtv_uio_read len %d buffer_size %d",rlen, buffer_size);
         return -1;
    }
}

void *audio_dtv_patch_input_threadloop(void *data)
{
    struct aml_dtv_audio_instance *dtv_audio_instance = (struct aml_dtv_audio_instance *)data;
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    package_list *list = dtv_audio_instance->dtv_package_list;
    void *demux_handle = NULL;
    int read_bytes = DEFAULT_PLAYBACK_PERIOD_SIZE * PLAYBACK_PERIOD_COUNT;
    int ret = 0;
    int nInBufferSize = read_bytes * 2; //full buffer size
    char *main_buffer = NULL;
    struct package *dtv_package = NULL;
    struct mAudioEsDataInfo *mEsData = NULL ,*mAdEsData = NULL;

    struct mediasync_audio_queue_info audio_queue_info;
    aml_dtvsync_t *Dtvsync = NULL ;
    aml_dtv_audiopara_t *dtv_audio_info = NULL;
    int64_t last_queue_es_apts = 0, data_offset = 0;
    bool need_ad_main_align = property_get_bool("vendor.dtv.audio.need_ad_main_align",true);
    bool is_multi_demux = is_dtv_multi_demux(aml_dev);
    ALOGI("[audiohal_kpi]++%s start input now patch->input_thread_exit %d!!!\n ",
          __FUNCTION__, dtv_audio_instance->input_thread_exit);

    prctl(PR_SET_NAME, (unsigned long)"dtv_input_patch");
    aml_set_thread_sched_priority("dtv_input_patch", dtv_audio_instance->audio_input_threadID, AUDIO_FIFO_THREAD_DEFAULT_PRIORITY - 2);
    /*affinity the thread to cpu/apu which has few IRQ*/
    aml_audio_set_cpu_affinity(false);
    dtv_package_list_init(list);

    while (!dtv_audio_instance->input_thread_exit) {

        int nRet = 0;
        demux_handle = dtv_audio_instance->demux_handle;
        dtv_audio_info = &dtv_audio_instance->dtv_audio_info;
        Dtvsync = &dtv_audio_instance->dtvsync;

        if (is_multi_demux) {
            if (demux_handle == NULL) {
                usleep(5000);
                continue;
            }
        } else {
            if (dtv_audio_instance->uio_fd < 0) {
                 usleep(5000);
                 continue;
             }
        }
        if (dtv_package == NULL) {
            dtv_package = aml_audio_calloc(1, sizeof(struct package));
            if (!dtv_package) {
                ALOGI("dtv_package malloc failed ");
                goto exit;
            }
        }
        /* get main data */
        if (mEsData == NULL) {
            if (is_multi_demux) {
                nRet = Get_MainAudio_Es(demux_handle,&mEsData);
                if (nRet != AM_AUDIO_Dmx_SUCCESS) {
                    if (aml_dev->debug_flag)
                        ALOGD("Get_MainAudio_Es failed");
                    usleep(2000);
                    continue;
                } else {
                   if (aml_dev->debug_flag)
                       ALOGI("mEsData->size %d",mEsData->size);
                }
            } else {
                if (!main_buffer) {
                    main_buffer = aml_audio_calloc(1, nInBufferSize);
                    if (!main_buffer) {
                        ALOGE("main_buffer malloc failed");
                        goto exit;
                    }
                }

                int nNextReadSize = 0;
                if (dtv_audio_instance->in_read_frame_size) {
                   nNextReadSize = dtv_audio_instance->in_read_frame_size;
                   if (nNextReadSize > nInBufferSize) {
                      main_buffer = aml_audio_realloc(main_buffer, nNextReadSize);
                   }
                } else {
                    if (dtv_audio_info->main_fmt == ACODEC_FMT_AC4 ||
                        dtv_audio_info->main_fmt == ACODEC_FMT_AAC ||
                        dtv_audio_info->main_fmt == ACODEC_FMT_AAC_LATM ||
                        dtv_audio_info->main_fmt == ACODEC_FMT_EAC3) {
                        //aac and single-pid-ad ddp need more data to do format parser
                        nNextReadSize = read_bytes;
                    } else if ((dtv_audio_instance->aformat == AUDIO_FORMAT_MP3) || (dtv_audio_instance->aformat == AUDIO_FORMAT_MP2)) {
                        nNextReadSize = read_bytes / 4;
                    } else {
                        nNextReadSize = read_bytes / 2;
                    }
                }
                /*
                FIXME:in the SWPL-164051, when dtv exit, Timecheck audio crash.
                If Abuffer no audio data and it could not transmit output_thread_exit to the api waiting_bits,
                it wll take 5s in the while loop.
                */
                nRet = dtv_uio_read((unsigned char *)main_buffer, nNextReadSize, &dtv_audio_instance->output_thread_exit);
                if (nRet == 0)  {
                    mEsData = aml_audio_malloc(sizeof( struct mAudioEsDataInfo));
                    mEsData->size = nNextReadSize;
                    mEsData->data = (uint8_t *)main_buffer;
                    mEsData->pts = lookup_apts_by_data_offset(dtv_audio_instance, data_offset);
                    if (mEsData->pts > 0) {
                        mEsData->pts_dts_flag = DVB_AUDIO_ES_PTS_VALID;
                    }
                    data_offset += mEsData->size;
                    main_buffer = NULL;
                    if (aml_dev->debug_flag)
                       ALOGI("mEsData->size %d",mEsData->size);
                } else {
                    usleep(5000);
                    continue;
                }
            }
        }

        /* get ad data */
        if (mAdEsData == NULL) {
            if (dtv_audio_info->dual_decoder_support && VALID_PID(dtv_audio_info->ad_pid)) {
                nRet = Get_ADAudio_Es(demux_handle, &mAdEsData);
                if (nRet != AM_AUDIO_Dmx_SUCCESS) {
                    if (aml_dev->debug_flag > 2)
                       ALOGI("do not get mEsData");
                }
                if (mAdEsData == NULL) {
                    ALOGV("do not get ad es data");
                    dtv_audio_info->ad_package_status = AD_PACK_STATUS_HOLD;
                } else {
                    if (aml_dev->debug_flag)
                       ALOGI("ad data %p size %d pts %0" PRIx64 "",
                           mAdEsData->data,mAdEsData->size, mAdEsData->pts);

                    /* align ad and main data by pts compare */
                    dtv_audio_info->ad_package_status = check_ad_package_status(mEsData->pts, mAdEsData->pts, dtv_audio_info);
                    if (dtv_audio_info->ad_package_status == AD_PACK_STATUS_DROP) {
                        if (mAdEsData->data) {
                            aml_audio_free(mAdEsData->data);
                            mAdEsData->data = NULL;
                        }
                        aml_audio_free(mAdEsData);
                        mAdEsData = NULL;
                        continue;
                    }
                }
            }
        }

         /* dtv pack main ad and data data */
        if (mEsData) {
            dtv_package->size = mEsData->size;
            dtv_package->data = (char *)mEsData->data;
            dtv_package->pts = mEsData->pts;
            dtv_package->pts_dts_flag = mEsData->pts_dts_flag;
            if (dtv_audio_instance->audio_pts_dts_flag != mEsData->pts_dts_flag) {
                dtv_audio_instance->audio_pts_dts_flag = mEsData->pts_dts_flag;
                ALOGV("patch->audio_pts_dts_flag = %d", mEsData->pts_dts_flag);
            }
            aml_audio_free(mEsData);
            mEsData = NULL;
        } else {
            continue;
        }

        if (mAdEsData) {
            dtv_audio_info->ad_package_status = check_ad_package_status(dtv_package->pts, mAdEsData->pts, dtv_audio_info);
            if (dtv_audio_info->ad_package_status == AD_PACK_STATUS_DROP) {
                if (mAdEsData->data) {
                    aml_audio_free(mAdEsData->data);
                    mAdEsData->data = NULL;
                }
                aml_audio_free(mAdEsData);
                mAdEsData = NULL;
            } else if (dtv_audio_info->ad_package_status == AD_PACK_STATUS_HOLD) {
                dtv_package->ad_size = 0;
                dtv_package->ad_data = NULL;
            } else {
                dtv_package->ad_size = mAdEsData->size;
                dtv_package->ad_data = (char *)mAdEsData->data;
                dtv_package->adpan  = mAdEsData->adpan;
                dtv_package->adfade = mAdEsData->adfade;
                dtv_package->ad_pts = mAdEsData->pts;
                aml_audio_free(mAdEsData);
                mAdEsData = NULL;
            }
        } else {
            dtv_package->ad_size = 0;
            dtv_package->ad_data = NULL;
        }

        /* mediasync check dmx package */

dtvsync_queue:
        if (Dtvsync->last_queue_apts == DTVSYNC_INIT_PTS) {
            ALOGI("[audiohal_kpi][%s,%d] get first audio es data , pts:%" PRIx64 ".\n",
                   __FUNCTION__, __LINE__, dtv_package->pts);
        }
        Dtvsync->last_queue_apts = dtv_package->pts;
        if (is_multi_demux) {
            if ((dtv_package->pts_dts_flag & 0x0F) != DVB_AUDIO_ES_PTS_INVALID &&
                dtv_package->pts) {
                audio_queue_info.apts = dtv_package->pts;
                audio_queue_info.duration = Dtvsync->duration;
            } else {
                audio_queue_info.apts = -1;
                audio_queue_info.duration = -1;
            }

            if (dtv_audio_info->playback_mode == NORMAL_MODE) {
                audio_queue_info.isworkingchannel = true;
            } else {
                audio_queue_info.isworkingchannel = false;
                if (Get_Audio_LastES_Apts(demux_handle, &last_queue_es_apts) == 0 && dtv_package->pts) {
                    audio_queue_info.duration = (int)(last_queue_es_apts - dtv_package->pts);
                }
            }
            audio_queue_info.tunit = MEDIASYNC_UNIT_PTS;
            aml_dtvsync_queue_audio_frame(Dtvsync, &audio_queue_info);
            if (aml_dev->debug_flag > 0)
                 ALOGI("path_no %d working_channel:%d,queue pts:[%" PRIx64 ",%" PRIx64 "], size:%d,"
                       "dur:%d ms, isneedupdate %d flag %0x.\n",\
                       dtv_audio_info->demux_id, audio_queue_info.isworkingchannel, dtv_package->pts,last_queue_es_apts,\
                       dtv_package->size, audio_queue_info.duration/90,audio_queue_info.isneedupdate, dtv_package->pts_dts_flag);

            if (!audio_queue_info.isworkingchannel) {
                if (audio_queue_info.isneedupdate) {
                    if (dtv_package) {
                       if (dtv_package->data) {
                           aml_audio_free(dtv_package->data);
                           dtv_package->data = NULL;
                       }
                       if (dtv_package->ad_data) {
                           aml_audio_free(dtv_package->ad_data);
                           dtv_package->ad_data = NULL;
                       }
                    }
                } else {
                   if (dtv_audio_instance->input_thread_exit) {
                       break;
                   }
                   usleep(20000);
                   goto dtvsync_queue;
                }
                continue;
            }
        }

package_queue:
        /* add dtv package to package list */
        if (dtv_package) {
            pthread_mutex_lock(&dtv_audio_instance->mutex);
            ret = dtv_package_add(list, dtv_package);
            if (ret == 0) {
                if (aml_dev->debug_flag > 0)
                    ALOGI("pthread_cond_signal dtv_package %p ", dtv_package);
                pthread_cond_signal(&dtv_audio_instance->cond);
                pthread_mutex_unlock(&dtv_audio_instance->mutex);
                dtv_package = NULL;
                continue;
            } else {
                ALOGI("list->pack_num %d full !!!", list->pack_num);
                pthread_mutex_unlock(&dtv_audio_instance->mutex);
                usleep(150000);
                if (dtv_audio_instance->input_thread_exit) {
                   break;

                }
                goto package_queue;
            }
        }
    }
exit:
    if (!is_multi_demux) {
        if (main_buffer) {
            aml_audio_free(main_buffer);
            main_buffer = NULL;
        }
    }
    if (dtv_package) {
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
    }

    ALOGI("--%s leave dtv_audio_instance->input_thread_exit %d ", __FUNCTION__, dtv_audio_instance->input_thread_exit);
    pthread_exit(NULL);
}

void update_dtv_audio_instance_format_info(struct aml_dtv_audio_instance *instance)
{
    struct aml_stream_out *aml_out = instance->dtv_stream_out.stream_out;
    struct codec_format_info codec_format = {0};
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    bool bypass_aml_dec = false;
    if (aml_dev->dolby_lib_type == eDolbyMS12Lib) {
        if (is_dolby_ms12_support_compression_format(aml_out->hal_internal_format)
            || is_multi_channel_pcm(&aml_out->stream)) {
            bypass_aml_dec = true;
        }
    }
    if (instance->update_stable_count <= FORMAT_STABLE_COUNT) {
        instance->update_stable_count++;
    }

    if (bypass_aml_dec) {
#ifndef AUDIO_HAL_DISABLE_MS12
        get_ms12_codec_format_info((struct audio_stream_out *)aml_out, &codec_format);
        instance->in_format = codec_format.encoding_format;
        instance->in_chanmask = codec_format.channel_mask;
        instance->input_sample_rate = codec_format.sampe_rate;
#endif
    } else {
        aml_dec_info_t dec_info = {0};
        aml_dec_t *aml_dec = aml_out->aml_dec;
        if (aml_dec) {
            aml_decoder_get_info(aml_out->aml_dec, AML_DEC_STREAM_INFO, &dec_info);
            if (is_aac_format(instance->aformat)) {
                instance->in_format = dec_info.dec_info.stream_format;
            } else {
                instance->in_format = aml_dec->format;
            }
            instance->in_chanmask = audio_channel_out_mask_from_count(dec_info.dec_info.stream_ch);
            instance->input_sample_rate = dec_info.dec_info.stream_sr;
        }
    }
    if (aml_dev->debug_flag > 0)
        ALOGI("in_format %0x chanmask %0x sample_rate %d",instance->in_format, instance->in_chanmask, instance->input_sample_rate);
}
float dtv_get_volume_on_non_TV_device(struct aml_stream_out *aml_out)
{
    struct aml_audio_device *adev = aml_out->dev;

    float out_gain = 1.0f;

    if (is_TV(adev)) {
        return out_gain;
    }

    /* For dev->mix case, eg: dtv -> usb card. We control the volume in in_read function. */
    if (!adev->dev2mix_patch) {
        out_gain = adev->sink_gain[get_output_by_devices(adev->cur_out_devices)];
    }
    if (aml_out->offload_mute && is_tv_stream_out(aml_out)) {
        out_gain = 0.0f;
    }
    /*
    for tv case, volume control it in audio_hal_data_processing
    for non tv case, dtv stream vol control in dolby_ms12_set_main_volume
    */
    if (!is_TV(adev) && !adev->enable_soundbar_mode) {
        if (is_dtv_stream_out(&aml_out->stream)) {
            //when Dolby MS12 use not 1.0 volume "-sys_prim_mixgain <3 int>
            //the PCM Render can not output at a same volume for both DDP and AC4.
            //AC4 should use the 1.0 volume and control the volume through the PCM output.
            //After add this patch, the Bitstream output volume will always 1.0,
            //its volume should be control by the Sink Device.
            if (!is_AC4_stream_with_pcm_sink_on_stb(aml_out)) {
                //out_gain *= aml_out->volume_l;
            }
            else {
                out_gain = 1.0f;
            }
            //aml_out->stream.set_volume(&aml_out->stream, out_gain, out_gain);
            aml_out->ms12_vol_ctrl = true;
        }
    }
    return out_gain;
}

void update_dtv_audio_decoder_runtime_params(struct aml_stream_out *aml_out, aml_dtv_audiopara_t *dtv_audio_info)
{
    aml_dec_t *aml_dec = aml_out->aml_dec;
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    if (aml_dec) {
        if (dtv_audio_info->dual_decoder_support != aml_out->dec_config.ad_decoder_supported ) {
            aml_out->dec_config.ad_decoder_supported = dtv_audio_info->dual_decoder_support;
            aml_decoder_set_config(aml_dec, AML_DEC_CONFIG_AD_DECODER_ENABLE, &aml_out->dec_config);
        }

        if (dtv_audio_info->mixing_level != aml_out->dec_config.mixer_level ) {
            aml_out->dec_config.mixer_level = dtv_audio_info->mixing_level;
            aml_decoder_set_config(aml_dec, AML_DEC_CONFIG_MIXER_LEVEL, &aml_out->dec_config);
        }

        if (dtv_audio_info->associate_audio_mixing_enable != aml_out->dec_config.ad_mixing_enable) {
           aml_out->dec_config.ad_mixing_enable = dtv_audio_info->associate_audio_mixing_enable;
           aml_decoder_set_config(aml_dec, AML_DEC_CONFIG_MIXING_ENABLE, &aml_out->dec_config);
        }

        if (dtv_audio_info->advol_level != aml_out->dec_config.advol_level) {
           aml_out->dec_config.advol_level = dtv_audio_info->advol_level;
           aml_decoder_set_config(aml_dec, AML_DEC_CONFIG_AD_VOL, &aml_out->dec_config);
        }

        if (dtv_audio_info->ad_fade != aml_out->dec_config.ad_fade) {
            aml_out->dec_config.ad_fade = dtv_audio_info->ad_fade;
            aml_decoder_set_config(aml_dec, AML_DEC_CONFIG_FADE, &aml_out->dec_config);
        }

        if (dtv_audio_info->ad_pan != aml_out->dec_config.ad_pan) {
            aml_out->dec_config.ad_pan = dtv_audio_info->ad_pan;
            aml_decoder_set_config(aml_dec, AML_DEC_CONFIG_PAN, &aml_out->dec_config);
        }

        if (dtv_audio_info->ad_placement != aml_out->dec_config.ad_placement) {
            aml_out->dec_config.ad_placement = dtv_audio_info->ad_placement;
            aml_decoder_set_config(aml_dec, AML_DEC_CONFIG_PLACEMENT, &aml_out->dec_config);
        }
    }else {
#ifndef AUDIO_HAL_DISABLE_MS12
        if (eDolbyMS12Lib == aml_dev->dolby_lib_type_last) {
            if (dtv_audio_info->mixing_level != aml_out->dec_config.mixer_level) {
                aml_out->dec_config.mixer_level = dtv_audio_info->mixing_level;
                dolby_ms12_set_user_control_value_for_mixing_main_and_associated_audio(dtv_audio_info->mixing_level);
                set_ms12_ad_mixing_level(&aml_out->stream, dtv_audio_info->mixing_level);
            }

            if (dtv_audio_info->advol_level != aml_out->dec_config.advol_level) {
                aml_out->dec_config.advol_level = dtv_audio_info->advol_level;
                set_ms12_ad_vol(&aml_out->stream, dtv_audio_info->advol_level);
            }
            if (dtv_audio_info->associate_audio_mixing_enable != aml_out->dec_config.ad_mixing_enable) {
                aml_out->dec_config.ad_mixing_enable = dtv_audio_info->associate_audio_mixing_enable;
                set_ms12_ad_mixing_enable(&aml_out->stream, dtv_audio_info->associate_audio_mixing_enable);
            }
            if (dtv_audio_info->main_fmt == ACODEC_FMT_AC4) {
                if (dtv_audio_info->media_presentation_id != aml_out->dec_config.media_presentation_id) {
                    aml_out->dec_config.media_presentation_id = dtv_audio_info->media_presentation_id;
                    set_ms12_ac4_presentation_group_index(&aml_out->stream, dtv_audio_info->media_presentation_id);
                }

                if (dtv_audio_info->media_first_lang != aml_out->dec_config.media_first_lang) {
                    char first_lang[4] = {0};
                    dtv_convert_language_to_string(dtv_audio_info->media_first_lang,first_lang);
                    ALOGI("media_first_lang %s",first_lang);
                    aml_out->dec_config.media_first_lang = dtv_audio_info->media_first_lang;
                    set_ms12_ac4_1st_preferred_language_code(&aml_out->stream, first_lang);
                }

                if (dtv_audio_info->media_second_lang != aml_out->dec_config.media_second_lang) {
                    char second_lang[4] = {0};
                    dtv_convert_language_to_string(dtv_audio_info->media_second_lang,second_lang);
                    ALOGI("media_second_lang %s",second_lang);
                    aml_out->dec_config.media_second_lang = dtv_audio_info->media_second_lang;
                    set_ms12_ac4_2nd_preferred_language_code(&aml_out->stream, second_lang);
                }
            }

            if (dtv_audio_info->ad_fade != aml_out->dec_config.ad_fade ||
                dtv_audio_info->ad_pan != aml_out->dec_config.ad_pan) {
                aml_out->dec_config.ad_fade = dtv_audio_info->ad_fade;
                aml_out->dec_config.ad_pan = dtv_audio_info->ad_pan;
                set_ms12_fade_pan(&aml_out->stream
                        , dtv_audio_info->ad_fade /* fade byte*/
                        , 0 /*gain_byte_center*/
                        , 0 /*gain_byte_front */
                        , 0 /*gain_byte_surround*/
                        , dtv_audio_info->ad_pan /*pan byte*/
                        );
            }
        }
#endif
    }
    if (aml_out->offload_mute != dtv_audio_info->tv_mute) {
        aml_out->offload_mute = dtv_audio_info->tv_mute;
#ifndef AUDIO_HAL_DISABLE_MS12
        if (eDolbyMS12Lib == aml_dev->dolby_lib_type_last) {
            set_ms12_decoder_mute(&aml_out->stream,  dtv_audio_info->tv_mute ? true: false, 0);
        }
#endif
    }
    float dtv_vol = dtv_audio_info->volume * dtv_get_volume_on_non_TV_device(aml_out);
    if (aml_out->volume_l != dtv_vol) {
        ALOGI("aml_out->volume_l %f dtv_audio_info->volume %f ", aml_out->volume_l, dtv_audio_info->volume);
        aml_out->volume_l = aml_out->volume_r = dtv_vol;
        aml_out->stream.set_volume(&aml_out->stream, aml_out->volume_l, aml_out->volume_r);
    }
}


void dtv_audio_reset_instance(struct aml_dtv_audio_instance *instance)
{
    struct aml_audio_device *aml_dev =  (struct aml_audio_device *)aml_adev_get_handle();
    if (!instance) {
        return;
    }
    if (instance->ac3_parser_handle) {
        aml_ac3_parser_reset(instance->ac3_parser_handle);
    }
    if (instance->ad_ac3_parser_handle) {
        aml_ac3_parser_reset(instance->ad_ac3_parser_handle);
    }

    if (instance->heaac_parser_handle) {
        aml_heaac_parser_reset(instance->heaac_parser_handle);
    }
    if (instance->ad_heaac_parser_handle) {
        aml_heaac_parser_reset(instance->ad_heaac_parser_handle);
    }
    if (instance->ac4_parser_handle) {
        aml_ac4_parser_reset(instance->ac4_parser_handle);
    }

    if (instance->ad_remain_buf) {
        instance->ad_remain_size = 0;
    }

    ALOGI("reset_dtvsync (mediasync:%p)", instance->dtvsync.mediasync);
    aml_dtvsync_reset(&instance->dtvsync);
    if (aml_dev->audio_ease) {
        stop_dtv_patch(aml_dev);
    }
}

int dtv_audio_check_package(struct aml_dtv_audio_instance *instance, struct package *p_package, struct timespec *package_get_ts)
{

    struct aml_audio_device *aml_dev =  (struct aml_audio_device *)aml_adev_get_handle();
    aml_dtv_audiopara_t *dtv_audio_info = &instance->dtv_audio_info;
    int ret = 0;
    int64_t data_arrive_jitter_ms = 0;
    int64_t data_pts_jitter_ms = 0;
    bool package_data_valid = ((p_package->pts_dts_flag & 0x0F) != DVB_AUDIO_ES_PTS_INVALID) && (p_package->pts != 0);

    //check package data to get first valid data
    if (!instance->package_checked_flag) {
        if (package_data_valid) {
            instance->package_checked_flag = true;
        } else {
            ALOGI("dtv package pts_dts_flag %0x pts %"PRIx64" invalid, free the package.",
                                                  p_package->pts_dts_flag, p_package->pts);
            if (p_package->data) {
                aml_audio_free(p_package->data);
                p_package->data = NULL;
            }

            if (p_package->ad_data) {
                aml_audio_free(p_package->ad_data);
                p_package->ad_data = NULL;
            }
            aml_audio_free(p_package);
            p_package = NULL;
            ret = -1;
            return ret;
        }
    }

    // If have ad data, use the fade-pan value of package and save in the struct demux_info
    // if no ad data, use the fade-pan value which is saved in the struct demux_info.
    // if use the default value(0), DB will jump frequently and audio output not smooth
    if (p_package->ad_data) {
        dtv_audio_info->ad_fade = p_package->adfade;
        dtv_audio_info->ad_pan = p_package->adpan;
    }
    struct timespec current_ts;
    clock_gettime(CLOCK_MONOTONIC, &current_ts);
    data_arrive_jitter_ms = calc_time_interval_us(package_get_ts, &current_ts) / 1000;
    package_get_ts->tv_sec = current_ts.tv_sec;
    package_get_ts->tv_nsec = current_ts.tv_nsec;
    if (package_data_valid) {
        data_pts_jitter_ms = DIFF_ABS(instance->dtvsync.last_package_pts,p_package->pts)/90;
    } else {
        data_pts_jitter_ms = 0;
        p_package->pts = DTVSYNC_INVALID_PTS;
    }

    if (aml_dev->debug_flag > 0) {
        ALOGI("cur_package size %u pts %"PRIx64" jitter %"PRIx64" ms pts diff %"PRIx64" ms",
          p_package->size, p_package->pts, data_arrive_jitter_ms, data_pts_jitter_ms);
    }
    //do fade in
    if (instance->dtvsync.last_package_pts != DTVSYNC_INIT_PTS &&
        ((data_arrive_jitter_ms >= DTV_AUDIO_DATA_JITTERMS_THRESHOLD) ||
        (data_pts_jitter_ms >= AUDIO_PTS_DISCONTINUE_THRESHOLD / 90))) {
        tv_set_ease(instance->dtv_stream_out.stream_out, EaseIn);
    }

    if (p_package->pts != DTVSYNC_INVALID_PTS) {
        instance->dtvsync.last_package_pts = p_package->pts;
    }
    return ret;

}

void *audio_dtv_patch_output_threadloop(void *data)
{
    struct aml_dtv_audio_instance *instance = (struct aml_dtv_audio_instance *)data;
    struct aml_audio_device *aml_dev =  (struct aml_audio_device *)aml_adev_get_handle();
    aml_dtv_audiopara_t *dtv_audio_info = &instance->dtv_audio_info;
    package_list *list = instance->dtv_package_list;
    struct audio_stream_out *stream_out = NULL;
    struct aml_stream_out *aml_out = NULL;
    struct audio_config stream_config = AUDIO_CONFIG_INITIALIZER;
    int ret;
    struct timespec ts,package_get_ts;
    clock_gettime(CLOCK_MONOTONIC, &package_get_ts);
    ALOGI("[audiohal_kpi]++%s created.", __FUNCTION__);
    stream_config.sample_rate = 48000;
    stream_config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    stream_config.format = instance->aformat;

#ifdef TV_AUDIO_OUTPUT
    instance->output_sink = AUDIO_DEVICE_OUT_SPEAKER;
#else
    instance->output_sink = AUDIO_DEVICE_OUT_AUX_DIGITAL;
#endif
    if (aml_dev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP)
        instance->output_sink = aml_dev->out_device;

    ret = aml_dev->hw_device.open_output_stream(&aml_dev->hw_device, 0,
                                      instance->output_sink,        // devices_t
                                      AUDIO_OUTPUT_FLAG_DIRECT, // flags
                                      &stream_config, &stream_out, "AML_DTV_SOURCE");
    if (ret < 0) {
        ALOGE("open output stream failed, ret = %d", ret);
        goto exit;
    }

    aml_out = (struct aml_stream_out *)stream_out;
    aml_out->is_eos = false;
    aml_out->output_speed = 1.0f;
    aml_out->offload_mute = 0;
    aml_out->volume_l = aml_out->volume_r = 0;
    aml_out->stream.set_volume(&aml_out->stream, aml_out->volume_l, aml_out->volume_r);
    aml_out->hwsync->mediasync = &instance->dtvsync;
    aml_out->dtvsync_enable =  property_get_int32("vendor.media.dtvsync.enable", 1);
    ALOGI("output_speed=%f,dtvsync_enable=%d\n", aml_out->output_speed, aml_out->dtvsync_enable);

    instance->dtv_stream_out.stream_out = aml_out;
    tv_set_ease(aml_out, EaseIn);

    ALOGI("path_index %d ++%s create a output stream %p success now!!!\n ", dtv_audio_info->demux_id, __FUNCTION__, stream_out);


    ALOGI("path_id %d,[audiohal_kpi]++%s start output pcm now patch->output_thread_exit %d!!!\n ",
         dtv_audio_info->demux_id,  __FUNCTION__, instance->output_thread_exit);

    prctl(PR_SET_NAME, (unsigned long)"dtv_output_patch");
    aml_set_thread_sched_priority("dtv_output_patch", instance->audio_output_threadID, AUDIO_FIFO_THREAD_DEFAULT_PRIORITY - 2);
    /*affinity the thread to cpu/apu which has few IRQ*/
    aml_audio_set_cpu_affinity(true);

    while (!instance->output_thread_exit) {

        if (instance->dtv_audio_state == AUDIO_DTV_PATCH_DECODER_STATE_PAUSED) {
            usleep(1000);
            continue;
        }
        pthread_mutex_lock(&instance->mutex);
        struct package *p_package = NULL;
        p_package = dtv_package_get(list);
        if (!p_package) {
            ts_wait_time(&ts, 100000);
            pthread_cond_timedwait(&instance->cond, &instance->mutex, &ts);
            pthread_mutex_unlock(&instance->mutex);
            continue;
        } else {
            if (dtv_audio_check_package(instance, p_package, &package_get_ts) != 0) {
                pthread_mutex_unlock(&instance->mutex);
                continue;
            }
        }

        pthread_mutex_unlock(&instance->mutex);
        pthread_mutex_lock(&(instance->dtv_output_mutex));

        if (aml_out->hal_internal_format != instance->aformat) {
            instance->aformat = aml_out->hal_format = aml_out->hal_internal_format;
            get_sink_format(stream_out);
            if (is_aac_format(instance->aformat)) {
                 aml_out->is_heaac_changed = true;
            }
        }

        dtv_audio_package_write(p_package, &instance->dtv_stream_out);
        update_dtv_audio_decoder_runtime_params(aml_out, dtv_audio_info);
        update_dtv_audio_instance_format_info(instance);
        pthread_mutex_unlock(&(instance->dtv_output_mutex));
    }

exit:
    aml_out->hwsync->mediasync = NULL;
    aml_out->stream.common.standby((struct audio_stream *)(aml_out));
    aml_dev->hw_device.close_output_stream(&aml_dev->hw_device, stream_out);
    instance->dtv_stream_out.stream_out = NULL;
    dtv_audio_reset_instance(instance);
    ALOGI("--%s path_id %d", __FUNCTION__, dtv_audio_info->demux_id);
    pthread_exit(NULL);
}


void  clean_dtv_audio_info(aml_dtv_audiopara_t *dtv_audio_info)
{
    dtv_audio_info->demux_id = -1;
    dtv_audio_info->security_mem_level  = -1;
    dtv_audio_info->output_mode  = -1;
    dtv_audio_info->has_video  = 0;
    dtv_audio_info->main_fmt  = -1;
    dtv_audio_info->main_pid  = -1;
    dtv_audio_info->ad_fmt  = -1;
    dtv_audio_info->ad_pid  = -1;
    dtv_audio_info->dual_decoder_support = 0;
    dtv_audio_info->advol_level = 0;
    dtv_audio_info->mixing_level = -32;
    dtv_audio_info->associate_audio_mixing_enable  = 0;
    dtv_audio_info->media_sync_id  = -1;
    dtv_audio_info->media_presentation_id  = -1;
    dtv_audio_info->media_first_lang  = -1;
    dtv_audio_info->media_second_lang  = -1;
    dtv_audio_info->ad_package_status  = -1;
    dtv_audio_info->ad_fade = 0;
    dtv_audio_info->ad_pan = 0;
    dtv_audio_info->playback_mode = NORMAL_MODE;
    dtv_audio_info->volume = 1.0f;
    dtv_audio_info->tv_mute = 0;
}

static void set_dtv_audio_datasource(aml_dtv_audio_instance_t *instance)
{

    void *demux_handle = instance->demux_handle;
    aml_dtv_audiopara_t *dtv_audio_info = &instance->dtv_audio_info;
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    if (is_dtv_multi_demux(aml_dev)) {
        Open_Dmx_Audio(&demux_handle,dtv_audio_info->demux_id, dtv_audio_info->security_mem_level);
        ALOGI("demux_handle %p ", demux_handle);
        instance->demux_handle = demux_handle;
        Init_Dmx_Main_Audio(demux_handle, dtv_audio_info->main_fmt, dtv_audio_info->main_pid);
        if (dtv_audio_info->dual_decoder_support) {
            if (property_get_bool("vendor.media.dtv.pesmode",true)) {
                if ((VALID_AD_FMT_UK(dtv_audio_info->ad_fmt))) {
                    Init_Dmx_AD_Audio(demux_handle, dtv_audio_info->ad_fmt, dtv_audio_info->ad_pid, 1);
                } else {
                    Init_Dmx_AD_Audio(demux_handle, dtv_audio_info->ad_fmt, dtv_audio_info->ad_pid, 0);
                }
            } else {
                Init_Dmx_AD_Audio(demux_handle, dtv_audio_info->ad_fmt, dtv_audio_info->ad_pid, 0);
            }
        }
        Start_Dmx_Main_Audio(demux_handle);
        if (dtv_audio_info->dual_decoder_support)
            Start_Dmx_AD_Audio(demux_handle);
        dtv_audio_info->ad_package_status = AD_PACK_STATUS_HOLD;
    } else {
        int ret = uio_init_new(&instance->uio_fd);
        if (ret < 0) {
            ALOGE("uio init error! \n");
        }
        if (dtv_audio_info->dual_decoder_support) {
            Open_Dmx_Audio(&demux_handle,dtv_audio_info->demux_id, dtv_audio_info->security_mem_level);
            ALOGI(" demux_hanle %p ", demux_handle);
            instance->demux_handle = demux_handle;
            ALOGI("ad es mode ");
            Init_Dmx_AD_Audio(demux_handle, dtv_audio_info->ad_fmt, dtv_audio_info->ad_pid, 1);
            Start_Dmx_AD_Audio(demux_handle);
        }
    }
}

static void unset_dtv_audio_datasource(aml_dtv_audio_instance_t *instance)
{
    void *demux_handle = instance->demux_handle;
    aml_dtv_audiopara_t *dtv_audio_info = &instance->dtv_audio_info;
    struct aml_audio_device *aml_dev = aml_adev_get_handle();

    if (is_dtv_multi_demux(aml_dev)) {
        if (demux_handle) {
            Stop_Dmx_Main_Audio(demux_handle);
            if (dtv_audio_info->dual_decoder_support)
                Stop_Dmx_AD_Audio(demux_handle);
            Destroy_Dmx_Main_Audio(demux_handle);
            if (dtv_audio_info->dual_decoder_support)
                Destroy_Dmx_AD_Audio(demux_handle);
            Close_Dmx_Audio(demux_handle);
            demux_handle = NULL;
            instance->demux_handle = NULL;
            ALOGI("receive close cmd, release mediasync.\n");
        }
    } else {
        if (demux_handle) {
            Stop_Dmx_AD_Audio(demux_handle);
            Destroy_Dmx_AD_Audio(demux_handle);
            Close_Dmx_Audio(demux_handle);
            demux_handle = NULL;
            instance->demux_handle = NULL;
        }
        uio_deinit_new(&instance->uio_fd);
    }

}
static void set_dtv_audio_mediasync(aml_dtvsync_t *dtvsync, aml_dtv_audiopara_t *dtv_audio_info)
{
    int has_audio = 1;
    int audio_sync_mode = 0;
    int path_id = dtv_audio_info->demux_id;
    if (dtvsync->mediasync == NULL) {
       dtvsync->mediasync = aml_dtvsync_create(dtvsync);
       if (dtvsync->mediasync == NULL)
           ALOGI("mediasync create failed\n");
       else {
           dtvsync->mediasync_id = dtv_audio_info->media_sync_id;
           ALOGI("path_id:%d,dtvsync media_sync_id=%d, init cur_outapts: %" PRId64 "\n", path_id, dtvsync->mediasync_id, dtvsync->cur_outapts);
           mediasync_wrap_setParameter(dtvsync->mediasync, MEDIASYNC_KEY_ISOMXTUNNELMODE, &audio_sync_mode);
           mediasync_wrap_bindInstance(dtvsync->mediasync, dtvsync->mediasync_id, MEDIA_AUDIO);
           ALOGI("normal output version CMD open audio bind syncId:%d\n", dtvsync->mediasync_id);
           mediasync_wrap_setParameter(dtvsync->mediasync, MEDIASYNC_KEY_HASAUDIO, &has_audio);
       }
   }
   ALOGI("%s create mediasync:%p\n", __FUNCTION__, dtvsync->mediasync);

}

static void unset_dtv_audio_mediasync(aml_dtvsync_t *dtvsync)
{
    if (dtvsync) {
        aml_dtvsync_release(dtvsync);
    }
    ALOGI("%s release mediasync.\n",__FUNCTION__);
}
int dtv_audio_context_get_cmd(struct aml_dtv_audio_context *context, int *cmd, int *path_id)

{
    if (context == NULL) {
        return -1;
    }

    if (context->dtv_cmd_list.initd == 0) {
        return -1;
    }

    if (dtv_audio_cmd_is_empty(&context->dtv_cmd_list) == 1) {
        return -1;
    } else {
        return dtv_audio_get_cmd(&context->dtv_cmd_list,cmd, path_id);
    }
}

static void *audio_dtv_cmd_process_threadloop(void *data)
{
    struct aml_dtv_audio_context *dtv_audio_context = (struct aml_dtv_audio_context *)data;
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    int ret = 0;
    int cmd = AUDIO_DTV_PATCH_CMD_NUM;
    int path_id  = 0;
    aml_dtvsync_t *dtvsync;
    struct timespec ts;
    struct mediasync_audio_format audio_format;
    struct media_out_portinfo audio_outport;
    struct aml_dtv_audio_instance *dtv_audio_instance;
    audio_patch_handle_t handle = 0;;
    //struct aml_dtv_audio_patch *patch;

    ALOGI("[audiohal_kpi]++%s Enter.\n", __FUNCTION__);
    while (!dtv_audio_context->cmd_process_thread_exit) {

        pthread_mutex_lock(&dtv_audio_context->dtv_cmd_process_mutex);
        if (dtv_audio_context->cmd_process_thread_exit == 1) {
            pthread_mutex_unlock(&dtv_audio_context->dtv_cmd_process_mutex);
            goto exit;
        }
        if (dtv_audio_context_get_cmd(dtv_audio_context, &cmd, &path_id) != 0) {
            ts_wait_time(&ts, 3000000);
            pthread_cond_timedwait(&dtv_audio_context->dtv_cmd_process_cond, &dtv_audio_context->dtv_cmd_process_mutex, &ts);
            pthread_mutex_unlock(&dtv_audio_context->dtv_cmd_process_mutex);
            continue;
        }

        dtv_audio_instance = &dtv_audio_context->instances[path_id];
        aml_dtvsync_t *dtvsync = &dtv_audio_instance->dtvsync;
        aml_dtv_audiopara_t *dtv_audio_info = &dtv_audio_instance->dtv_audio_info;

        switch (dtv_audio_instance->dtv_audio_state) {
        case AUDIO_DTV_PATCH_DECODER_STATE_IDLE:
            if (cmd == AUDIO_DTV_PATCH_CMD_OPEN) {
                const struct audio_port_config sources = {
                    .id = 1,
                    .role = AUDIO_PORT_ROLE_SOURCE,
                    .type = AUDIO_PORT_TYPE_DEVICE,
                    .ext = {.device =
                           {.type = AUDIO_DEVICE_IN_TV_TUNER_DTV}
                    }
                };

                const struct audio_port_config sinks = {
                    .id = 2,
                    .role = AUDIO_PORT_ROLE_SINK,
                    .type = AUDIO_PORT_TYPE_DEVICE,
                    .ext = {.device =
                           {.type = aml_dev->cur_out_devices}
                    }
                };
                dtv_audio_context->dtv_demux_id = path_id;
                ret = aml_dev->hw_device.create_audio_patch(&aml_dev->hw_device,
                                1,
                                &sources,
                                1,
                                &sinks,
                                &handle);
                if (ret != 0) {
                    ALOGE("[%s:%d] create dtv patch failed, all_out_devices:%#x.", __func__, __LINE__, aml_dev->cur_out_devices);
                    goto exit;
                }
                dtv_audio_instance->audio_patch_base.patch_id = handle;
                set_dtv_audio_datasource(dtv_audio_instance);
                set_dtv_audio_mediasync(dtvsync, dtv_audio_info);
                dtv_audio_instance->dtv_audio_state = AUDIO_DTV_PATCH_DECODER_STATE_PREPARED;
                create_dtv_input_stream_thread(dtv_audio_instance);
            } else {
                 ALOGI("++%s line %d  state unsupport state %d cmd %d !\n",
                      __FUNCTION__, __LINE__, dtv_audio_instance->dtv_audio_state, cmd);
            }
            ALOGI("%d %s",__LINE__, __FUNCTION__);
            break;
        case AUDIO_DTV_PATCH_DECODER_STATE_PREPARED:

            if (cmd == AUDIO_DTV_PATCH_CMD_START) {
                dtv_audio_info = &dtv_audio_instance->dtv_audio_info;
                dtv_audio_instance->mode = dtv_audio_info->output_mode;
                dtv_audio_instance->dtv_aformat = dtv_audio_info->main_fmt;
                dtv_audio_instance->dtv_has_video = dtv_audio_info->has_video;
                dtv_audio_instance->update_stable_count = 0;
                ALOGI("dtv_has_video %d demux_info->media_presentation_id %d",dtv_audio_instance->dtv_has_video,dtv_audio_info->media_presentation_id);
                stop_dtv_patch(aml_dev);

                ALOGI("patch->demux_handle %p patch->aformat %0x", dtv_audio_instance->demux_handle, dtv_audio_instance->aformat);
                dtv_audio_instance->in_format = dtv_audio_instance->aformat;
                ALOGI("dtvsync->mediasync %p dtvsync %p ", dtvsync->mediasync, dtvsync);
                if (dtvsync) {
                    audio_format.format = dtv_audio_instance->dtv_aformat;
                    bool alsa_status = true;
                    mediasync_wrap_setParameter(dtvsync->mediasync, MEDIASYNC_KEY_AUDIOFORMAT, &audio_format);
                    audio_outport.output_port = (audio_out_port)get_output_by_devices(aml_dev->cur_out_devices);
                    mediasync_wrap_setParameter(dtvsync->mediasync, MEDIASYNC_KEY_AUDIO_EQUIPMENT, &audio_outport);
                    mediasync_wrap_setParameter(dtvsync->mediasync, MEDIASYNC_KEY_HASVIDEO, &dtv_audio_instance->dtv_has_video);

                    ALOGI("aml_dtvsync_setParameter (MEDIASYNC_KEY_ALSAREADY)");
                    aml_dtvsync_setParameter(dtvsync, MEDIASYNC_KEY_ALSAREADY, &alsa_status);
                    dtvsync->cur_outapts = DTVSYNC_INIT_PTS;
                    dtvsync->out_start_apts = DTVSYNC_INIT_PTS;
                    dtvsync->out_end_apts = DTVSYNC_INIT_PTS;
                    dtvsync->last_package_pts = DTVSYNC_INIT_PTS;
                    dtvsync->last_queue_apts = DTVSYNC_INIT_PTS;
                    dtvsync->last_lookup_apts = DTVSYNC_INVALID_PTS;
                }

                dtv_audio_instance->aformat = aml_fmt_convert_to_android_fmt(dtv_audio_info->main_fmt);
                ALOGI("dtv_audio_instance->demux_handle %p dtv_audio_instance->aformat %0x", dtv_audio_instance->demux_handle, dtv_audio_instance->aformat);
                dtv_audio_instance->dtv_pcm_wrote = 0;
                dtv_audio_instance->in_read_frame_size = 0;
                dtv_audio_instance->package_checked_flag = false;
                create_dtv_output_stream_thread(dtv_audio_instance);
                dtv_audio_instance->dtv_audio_state = AUDIO_DTV_PATCH_DECODER_STATE_RUNNING;
           } else if (cmd == AUDIO_DTV_PATCH_CMD_STOP) {
               ALOGI("[audiohal_kpi]++%s now  stop  the audio decoder now \n", __FUNCTION__);
               dtv_audio_instance->dtv_audio_state = AUDIO_DTV_PATCH_DECODER_STATE_RELEASE;
           } else {
                ALOGI("++%s line %d  state unsupport state %d cmd %d !\n",
                      __FUNCTION__, __LINE__, dtv_audio_instance->dtv_audio_state, cmd);
            }
            break;
        case AUDIO_DTV_PATCH_DECODER_STATE_RUNNING:

            if (cmd == AUDIO_DTV_PATCH_CMD_PAUSE) {
                ALOGI("++%s now start  pause  the audio decoder now \n",
                      __FUNCTION__);
                if (dtvsync->mediasync) {
                    aml_dtvsync_setPause(dtvsync, true);
                }
                dtv_audio_instance->dtv_audio_state = AUDIO_DTV_PATCH_DECODER_STATE_PAUSED;
                ALOGI("++%s now end  pause  the audio decoder now \n",
                      __FUNCTION__);
            } else if (cmd == AUDIO_DTV_PATCH_CMD_STOP) {
                ALOGI("[audiohal_kpi]++%s now  stop  the audio decoder now \n",
                      __FUNCTION__);
                release_dtv_output_stream_thread(dtv_audio_instance);
                dtv_audio_instance->dtv_audio_state = AUDIO_DTV_PATCH_DECODER_STATE_RELEASE;
           } else if (cmd == AUDIO_DTV_PATCH_CMD_RESET_OUTPUT) {
                release_dtv_output_stream_thread(dtv_audio_instance);
                dtv_audio_instance->dtv_audio_state = AUDIO_DTV_PATCH_DECODER_STATE_PREPARED;
                ALOGI("[audiohal_kpi]++%s now resetthe audio decoder now \n",
                      __FUNCTION__);
           } else {
                ALOGI("++%s line %d state unsupport state %d cmd %d !\n",
                      __FUNCTION__, __LINE__, dtv_audio_instance->dtv_audio_state, cmd);
            }
            break;

        case AUDIO_DTV_PATCH_DECODER_STATE_PAUSED:

            if (cmd == AUDIO_DTV_PATCH_CMD_RESUME) {

                if (dtvsync) {
                    aml_dtvsync_setPause(dtvsync, false);
                }
                dtv_audio_instance->dtv_audio_state = AUDIO_DTV_PATCH_DECODER_STATE_RUNNING;
            } else if (cmd == AUDIO_DTV_PATCH_CMD_STOP) {

                ALOGI("[audiohal_kpi]++%s now  stop  the audio decoder now \n",
                     __FUNCTION__);
                /*
                 * When stop after pause, need release output thread.
                 * Or it will lead next channel no sound which has diff format.
                 * And can't add flush action, it maybe will lead freeze.
                 * */
                release_dtv_output_stream_thread(dtv_audio_instance);
                dtv_audio_instance->dtv_audio_state = AUDIO_DTV_PATCH_DECODER_STATE_RELEASE;
            } else {
                ALOGI("++%s line %d state unsupport state %d cmd %d !\n",
                      __FUNCTION__, __LINE__, dtv_audio_instance->dtv_audio_state, cmd);
            }
            break;
        case AUDIO_DTV_PATCH_DECODER_STATE_RELEASE:

            if (cmd == AUDIO_DTV_PATCH_CMD_CLOSE) {
                release_dtv_input_stream_thread(dtv_audio_instance);
                unset_dtv_audio_datasource(dtv_audio_instance);
                unset_dtv_audio_mediasync(dtvsync);
                handle = dtv_audio_instance->audio_patch_base.patch_id;
                ret = aml_dev->hw_device.release_audio_patch(&aml_dev->hw_device, handle);
                if (ret != 0) {
                   ALOGE("[%s:%d] release dtv patch failed, all_out_devices:%#x.", __func__, __LINE__, aml_dev->cur_out_devices);
                   goto exit;
                }
                clean_dtv_audio_info(dtv_audio_info);
                dtv_audio_instance->dtv_audio_state = AUDIO_DTV_PATCH_DECODER_STATE_IDLE;
                pthread_cond_signal(&dtv_audio_context->dtv_cmd_processed_cond);
                ALOGI("pthread_cond_signal dtv_cmd_processed_cond");
            } else {
                ALOGI("++%s line %d state unsupport state %d cmd %d !\n",
                      __FUNCTION__, __LINE__, dtv_audio_instance->dtv_audio_state, cmd);
            }
            break;
        default:
            ALOGI("invalid dtv_audio_state %d ", dtv_audio_instance->dtv_audio_state);
            break;
        }
        pthread_mutex_unlock(&dtv_audio_context->dtv_cmd_process_mutex);
    }

exit:

    ALOGI("[audiohal_kpi]++%s Exit", __FUNCTION__);
    pthread_exit(NULL);
}

static int create_dtv_output_stream_thread(struct aml_dtv_audio_instance *dtv_audio_instance)
{
    int ret = 0;
    ALOGI("++%s ---- %d\n", __FUNCTION__, dtv_audio_instance->output_thread_created);

    if (dtv_audio_instance->output_thread_created == 0) {
        dtv_audio_instance->output_thread_exit = 0;
        pthread_mutex_init(&dtv_audio_instance->dtv_output_mutex, NULL);
        ret = pthread_create(&(dtv_audio_instance->audio_output_threadID), NULL,
                             audio_dtv_patch_output_threadloop, dtv_audio_instance);
        if (ret != 0) {
            ALOGE("%s, Create output thread fail!\n", __FUNCTION__);
            pthread_mutex_destroy(&dtv_audio_instance->dtv_output_mutex);
            return -1;
        }
        dtv_audio_instance->output_thread_created = 1;
    }
    ALOGI("--%s", __FUNCTION__);
    return 0;
}

static int release_dtv_output_stream_thread(struct aml_dtv_audio_instance *dtv_audio_instance)
{
    int ret = 0;
    ALOGI("++%s ---- %d\n", __FUNCTION__, dtv_audio_instance->output_thread_created);
    if (dtv_audio_instance->output_thread_created == 1) {
        //tv_set_ease(dtv_audio_instance->dtv_stream_out.stream_out, EaseOut);
        dtv_audio_instance->output_thread_exit = 1;
        if (dtv_audio_instance->dtv_stream_out.stream_out) {
            dtv_audio_instance->dtv_stream_out.stream_out->is_eos = true;
        }
        pthread_cond_signal(&dtv_audio_instance->cond);
        pthread_join(dtv_audio_instance->audio_output_threadID, NULL);
        pthread_mutex_destroy(&dtv_audio_instance->dtv_output_mutex);
        dtv_audio_instance->output_thread_created = 0;
    }
    ALOGI("--%s", __FUNCTION__);
    return 0;
}

static int create_dtv_input_stream_thread(struct aml_dtv_audio_instance *dtv_audio_instance)
{
    int ret = 0;
    ALOGI("++%s ---- %d\n", __FUNCTION__, dtv_audio_instance->input_thread_created);

    if (dtv_audio_instance->input_thread_created == 0) {
        dtv_audio_instance->input_thread_exit = 0;
        pthread_mutex_init(&dtv_audio_instance->dtv_input_mutex, NULL);
        ret = pthread_create(&(dtv_audio_instance->audio_input_threadID), NULL,
                             audio_dtv_patch_input_threadloop, dtv_audio_instance);
        if (ret != 0) {
            ALOGE("%s, Create output thread fail!\n", __FUNCTION__);
            pthread_mutex_destroy(&dtv_audio_instance->dtv_input_mutex);
            return -1;
        }

        dtv_audio_instance->input_thread_created = 1;
    }
    ALOGI("--%s", __FUNCTION__);
    return 0;
}

static int release_dtv_input_stream_thread(struct aml_dtv_audio_instance *dtv_audio_instance)
{
    int ret = 0;
    ALOGI("++%s ---- %d\n", __FUNCTION__, dtv_audio_instance->input_thread_created);
    if (dtv_audio_instance->input_thread_created == 1) {
        dtv_audio_instance->input_thread_exit = 1;
        pthread_join(dtv_audio_instance->audio_input_threadID, NULL);
        pthread_mutex_destroy(&dtv_audio_instance->dtv_input_mutex);
        dtv_audio_instance->input_thread_created = 0;
    }
    ALOGI("--%s", __FUNCTION__);
    return 0;
}

int create_dtv_cmd_process_thread(struct aml_dtv_audio_context *context)
{
    int ret = 0;
    ALOGI("++%s ---- \n", __FUNCTION__);
    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
    pthread_cond_init(&context->dtv_cmd_process_cond, &cattr);
    pthread_cond_init(&context->dtv_cmd_processed_cond, &cattr);

    pthread_mutex_init(&context->dtv_cmd_process_mutex, NULL);

    init_cmd_list(&context->dtv_cmd_list);

    ret = pthread_create(&(context->audio_cmd_process_threadID), NULL,
                     audio_dtv_cmd_process_threadloop, context);
    if (ret != 0) {
        ALOGE("%s, Create process thread fail!\n", __FUNCTION__);
        pthread_mutex_destroy(&context->dtv_cmd_process_mutex);
        pthread_cond_destroy(&context->dtv_cmd_process_cond);
        pthread_cond_destroy(&context->dtv_cmd_processed_cond);
        ret = -1;
    }

    ALOGI("--%s", __FUNCTION__);
    return ret;
}

int release_dtv_cmd_process_thread(struct aml_dtv_audio_context *context)
{
    int ret = 0;
    ALOGI("++%s ----\n", __FUNCTION__);

    context->cmd_process_thread_exit = 1;
    pthread_cond_signal(&context->dtv_cmd_process_cond);

    pthread_join(context->audio_cmd_process_threadID, NULL);
    pthread_mutex_destroy(&context->dtv_cmd_process_mutex);
    pthread_cond_destroy(&context->dtv_cmd_process_cond);
    pthread_cond_destroy(&context->dtv_cmd_processed_cond);
    deinit_cmd_list(&context->dtv_cmd_list);
    ALOGI("--%s", __FUNCTION__);
    return ret;
}

int create_dtv_patch_l(struct aml_audio_patch **audio_patch, audio_devices_t input,
                       audio_devices_t output __unused)
{
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    int ret = 0;
    aml_dtv_audio_context_t *context = get_dtv_audio_context(aml_dev);
    struct aml_dtv_audio_instance  *dtv_audio_instance = &context->instances[context->dtv_demux_id];
    // save dev to patch
    dtv_audio_instance->input_src = input;
    dtv_audio_instance->is_dtv_src = true;
    dtv_audio_instance->ad_substream_checked_flag = false;
    dtv_audio_instance->output_thread_exit = 0;
    dtv_audio_instance->PServerDev = -1;


    *audio_patch = &dtv_audio_instance->audio_patch_base;
    pthread_mutex_init(&dtv_audio_instance->mutex, NULL);
    pthread_cond_init(&dtv_audio_instance->cond, NULL);

    dtv_audio_instance->dtv_package_list = aml_audio_calloc(1, sizeof(package_list));
    if (!dtv_audio_instance->dtv_package_list) {
        ret = -1;
        goto err;
    }

    if (aml_dev->dev2mix_patch) {
        create_tvin_buffer(&dtv_audio_instance->audio_patch_base);
    }

    if (!is_dtv_multi_demux(aml_dev) && aml_dev->singleDmxNonTunnelMode) {
        int PServerInsId = 12;
        ptsserver_alloc_para mAllocPara;
        dtv_audio_instance->PServerDev = PtsServ_open();
        ALOGI(" [%s:%d] PServerDev %d\n",__FUNCTION__,__LINE__, dtv_audio_instance->PServerDev);
        if (dtv_audio_instance->PServerDev != -1) {
            mAllocPara.mMaxCount = 500;
            mAllocPara.mLookupThreshold = 1024;
            mAllocPara.kDoubleCheckThreshold = 5;
            PtsServ_ioctl(dtv_audio_instance->PServerDev, PTSSERVER_IOC_INSTANCE_SET_ID, (unsigned long)&PServerInsId);
            PtsServ_ioctl(dtv_audio_instance->PServerDev, PTSSERVER_IOC_INSTANCE_STATIC_BINDER, (unsigned long)&mAllocPara);
        }
    }

    dtv_audio_instance->mode = get_dtv_sound_mode(aml_dev);
    dtv_audio_instance->audio_pts_dts_flag = 0;
    dtv_audio_instance->in_read_frame_size = 0;
    dtv_audio_instance->pts_margin = 0;

    ALOGI("--%s", __FUNCTION__);
    return 0;

err:
    aml_audio_free(dtv_audio_instance);
    return ret;
}

int release_dtv_patch_l(struct aml_dtv_audio_instance *dtv_audio_instance)
{

    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    ALOGI("[audiohal_kpi]++%s Enter\n", __FUNCTION__);
    if (dtv_audio_instance == NULL) {
        ALOGI("release the dtv patch patch == NULL\n");
        return 0;
    }

    if (!is_dtv_multi_demux(aml_dev) && aml_dev->singleDmxNonTunnelMode) {
        int success = PtsServ_close(dtv_audio_instance->PServerDev);
        ALOGI("PtsServ_close %d\n",success);
    }

    release_tvin_buffer(&dtv_audio_instance->audio_patch_base);
    if (dtv_audio_instance->dtv_package_list) {
        dtv_package_list_flush(dtv_audio_instance->dtv_package_list);
        aml_audio_free(dtv_audio_instance->dtv_package_list);
        dtv_audio_instance->dtv_package_list = NULL;
    }

    if (dtv_audio_instance->ac3_parser_handle) {
        aml_ac3_parser_close(dtv_audio_instance->ac3_parser_handle);
        dtv_audio_instance->ac3_parser_handle = NULL;
    }
    if (dtv_audio_instance->ad_ac3_parser_handle) {
        aml_ac3_parser_close(dtv_audio_instance->ad_ac3_parser_handle);
        dtv_audio_instance->ad_ac3_parser_handle = NULL;
    }

    if (dtv_audio_instance->ad_remain_buf) {
        aml_audio_free(dtv_audio_instance->ad_remain_buf);
        dtv_audio_instance->ad_remain_buf = NULL;
    }
    if (dtv_audio_instance->heaac_parser_handle) {
        aml_heaac_parser_close(dtv_audio_instance->heaac_parser_handle);
        dtv_audio_instance->heaac_parser_handle = NULL;
    }
    if (dtv_audio_instance->ad_heaac_parser_handle) {
        aml_heaac_parser_close(dtv_audio_instance->ad_heaac_parser_handle);
        dtv_audio_instance->ad_heaac_parser_handle = NULL;
    }
    if (dtv_audio_instance->ac4_parser_handle) {
        aml_ac4_parser_close(dtv_audio_instance->ac4_parser_handle);
        dtv_audio_instance->ac4_parser_handle = NULL;
    }
    //aml_audio_free(dtv_audio_instance);

    ALOGI("[audiohal_kpi]--%s Exit", __FUNCTION__);
    return 0;
}

int create_dtv_patch(struct aml_audio_patch **audio_patch, audio_devices_t input,
                     audio_devices_t output)
{
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(aml_dev);
    int ret = create_dtv_patch_l(audio_patch, input, output);
    return ret;
}

int release_dtv_patch(struct aml_audio_patch *audio_patch)
{
    int ret = 0;
    struct aml_audio_device *aml_dev = aml_adev_get_handle();
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(aml_dev);
    struct aml_dtv_audio_instance *dtv_audio_instance =  (struct aml_dtv_audio_instance *)audio_patch;

    ret = release_dtv_patch_l(dtv_audio_instance);
    set_dtv_volume(aml_dev, 1.0);

    stop_dtv_patch(aml_dev);
    return ret;
}

int dtv_patch_get_latency(struct aml_audio_device *aml_dev)
{
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(aml_dev);
    struct aml_dtv_audio_instance *dtv_audio_instance= &dtv_audio_context->instances[dtv_audio_context->dtv_demux_id];
    if (dtv_audio_instance == NULL) {
        ALOGI("dtv patch == NULL");
        return -1;
    } else {
         if (dtv_audio_instance->output_thread_exit || (dtv_audio_instance->output_thread_created == 0)) {
             return -1;
         }
    }
    acquire_dtv_mutex_lock(aml_dev);
    int latencyms = 0;
    int64_t last_queue_es_apts = 0;
    aml_dtvsync_t *Dtvsync = &dtv_audio_instance->dtvsync;
    if (is_dtv_multi_demux(aml_dev)) {
        if (dtv_audio_instance->demux_handle) {
            if (Get_Audio_LastES_Apts(dtv_audio_instance->demux_handle, &last_queue_es_apts) == 0) {
                 dtv_audio_instance->last_checkin_apts = last_queue_es_apts;
                 if (Dtvsync) {
                     ALOGI("last_queue_es_apts %" PRId64 "patch->cur_outapts %" PRId64,last_queue_es_apts,Dtvsync->cur_outapts);
                     if (last_queue_es_apts < Dtvsync->cur_outapts) {
                         if (dtv_audio_instance->last_checkin_apts < dtv_audio_instance->last_min_pts) {
                             dtv_audio_instance->last_checkin_apts = dtv_audio_instance->last_max_pts;
                         } else {
                            dtv_audio_instance->last_checkin_apts = dtv_audio_instance->last_max_pts + (dtv_audio_instance->last_checkin_apts - dtv_audio_instance->last_min_pts);
                         }
                     }
                 }
             }
        }

        if (dtv_audio_instance->last_checkin_apts != 0xffffffff) {
            if (Dtvsync) {
                 ALOGI("dtv_audio_instance->dtvsync->cur_outapts %" PRId64,Dtvsync->cur_outapts);
                if (Dtvsync->cur_outapts > 0 && (dtv_audio_instance->last_checkin_apts - Dtvsync->cur_outapts))
                    latencyms = (dtv_audio_instance->last_checkin_apts - Dtvsync->cur_outapts) / 90;
            } else {
                ALOGV("dtv_audio_instance->dtvsync NULL");
            }
        }
    }
    release_dtv_mutex_lock(aml_dev);
    return latencyms;
}
int dtv_patch_get_es_pts_dts_flag(struct aml_audio_device *aml_dev)
{
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(aml_dev);
    struct aml_dtv_audio_instance *dtv_audio_instance= &dtv_audio_context->instances[dtv_audio_context->dtv_demux_id];
    int pts_dts_flag;
    if (dtv_audio_instance == NULL) {
        //ALOGI("dtv patch == NULL");
        return -1;
    } else {
        pts_dts_flag =  dtv_audio_instance->audio_pts_dts_flag;
    }
    //ALOGI("%s pts_dts_flag %d", __FUNCTION__, pts_dts_flag);
    return pts_dts_flag;
}

int dtv_patch_get_cmd_close_status(struct aml_audio_device *aml_dev)
{
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(aml_dev);
    aml_dtvsync_t *dtvsync = &dtv_audio_context->instances[dtv_audio_context->dtv_demux_id].dtvsync;
    int cmd_close_status = 0;
    if (dtvsync == NULL ||
        dtvsync->mediasync == NULL) {
        cmd_close_status = 1;
    } else {
        cmd_close_status =  0;
    }
    ALOGI("%s cmd_close_status %d", __FUNCTION__, cmd_close_status);
    return cmd_close_status;
}

int dtv_patch_get_decoder_fmt(struct aml_audio_device *aml_dev)
{
    struct aml_dtv_audio_instance *dtv_audio_instance = (struct aml_dtv_audio_instance *)get_dev_patch(aml_dev);
    int decoder_fmt = ACODEC_FMT_NULL;
    if (dtv_audio_instance) {
        //FIXME: in the jira 197044, aac profile and format is not stable in the beginning of output audio.
        //Therefore, we need report the audio format to tsplayer until audio format of parser is stable.
        if (dtv_audio_instance->update_stable_count <= FORMAT_STABLE_COUNT) {
            return decoder_fmt;
        } else {
            if (is_aac_format(dtv_audio_instance->aformat)) {
                if (dtv_audio_instance->in_format == AUDIO_FORMAT_AAC_HE_V1) {
                    decoder_fmt = ACODEC_FMT_HEAAC_V1;
                } else if (dtv_audio_instance->in_format == AUDIO_FORMAT_AAC_HE_V2) {
                    decoder_fmt = ACODEC_FMT_HEAAC_V2;
                } else if (dtv_audio_instance->in_format == AUDIO_FORMAT_AAC_LC) {
                    decoder_fmt = ACODEC_FMT_AAC;
                } else {
                    decoder_fmt = dtv_audio_instance->dtv_aformat;
                }
            } else {
                decoder_fmt = dtv_audio_instance->dtv_aformat;
            }
        }
    }
    ALOGI("%s decoder_fmt %d", __FUNCTION__, decoder_fmt);

    return decoder_fmt;
}

int dtv_patch_get_ac4_acivie_res_id(struct aml_audio_device *aml_dev) {
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(aml_dev);
    aml_dtv_audio_instance_t *instance = &dtv_audio_context->instances[dtv_audio_context->dtv_demux_id];
    int ac4_active_presentation = -1;
    int ret = -1;
#ifndef AUDIO_HAL_DISABLE_MS12
    if (instance->dtv_stream_out.stream_out && instance->aformat == AUDIO_FORMAT_AC4) {
        ret = aml_ms12_decoder_getparameter(&aml_dev->ms12, instance->dtv_stream_out.stream_out->ms12_dec_handle,
            MS12_CODEC_PARAMETER_AC4DE_ACTIVE_PRESENTATION,
                    &ac4_active_presentation , sizeof(int));
        ALOGI("ac4_active_presentation %d instance->dtv_stream_out.stream_out->ms12_dec_handle %p",
            ac4_active_presentation, instance->dtv_stream_out.stream_out->ms12_dec_handle);
    }
#endif
    return ac4_active_presentation;
}
#if ANDROID_PLATFORM_SDK_VERSION > 29
int enable_dtv_patch_for_tuner_framework(struct audio_config *config, struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = (struct aml_audio_device *)aml_out->dev;
    struct audio_hw_device *dev = (struct audio_hw_device *)adev;
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    int ret = 0, val = 0, path_id = 0;
    ALOGI("%s %d", __FUNCTION__, __LINE__);
    /*1.only when config has valid content id and sync id*/
    if (config->offload_info.content_id != 0 && config->offload_info.sync_id != 0)
    {
         pthread_mutex_lock(&dtv_audio_context->dtv_cmd_process_mutex);
        /*2.parser demux id from offload_info, then set it. tuner/filter.cpp for reference.*/
        val = (config->offload_info.content_id >> 16) & 0xF;//demux id
        if (val > DVB_DEMUX_SUPPORT_MAX_NUM - 1)  {
            ALOGW("invalid dmx id %d ", val);
            pthread_mutex_unlock(&dtv_audio_context->dtv_cmd_process_mutex);
            return -1;
        }
        path_id = val;
        aml_out->demux_id = path_id;
        dtv_audio_context->instances[path_id].dtv_scene = DTV_TUNER_FRAMEWORK;
        val = (path_id << DVB_DEMUX_ID_BASE | val);
        ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_DEMUX_INFO, val);

        /*3.parser pid from offload_info, then set it. tuner/filter.cpp for reference.*/
        val = config->offload_info.content_id & 0x0000FFFF;//pid
        val = (path_id << DVB_DEMUX_ID_BASE | val);
        ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_PID, val);

        /*4.parser pid from offload_info, then set it.*/
        val = config->offload_info.sync_id;//sync id
        val = (path_id << DVB_DEMUX_ID_BASE | val);
        ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_MEDIA_SYNC_ID, val);

        /*5.parser format from offload_info, then set it.*/
        if (audio_is_linear_pcm(config->offload_info.format)) {
            val = (config->offload_info.content_id >> 21) & 0x1F;//encoding_fmt
            val = tunerhal_fmt_to_native_fmt(val);//native_fmt
            val = android_fmt_convert_to_dmx_fmt(val);//dmx_fmt
            ALOGI("tunerhal 1.0 case dmx_fmt %d", val);
        } else {
             val = android_fmt_convert_to_dmx_fmt(config->offload_info.format);//fmt
        }

        val = (path_id << DVB_DEMUX_ID_BASE | val);
        ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_FMT, val);

        /*6.set security_mem_level. for tunerframework.*/
        val = (config->offload_info.content_id >> 20) & 0x1;
        if (val == 1) {
            val = 2 << 10;
        } else {
            val = 0;
        }
        val = (path_id << DVB_DEMUX_ID_BASE | val);
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_SECURITY_MEM_LEVEL, val);
        pthread_mutex_unlock(&dtv_audio_context->dtv_cmd_process_mutex);
        /*7.init mediasync via cmds.*/
        val = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_OPEN);
        ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, val);

        aml_dtvsync_t *dtvsync = &dtv_audio_context->instances[path_id].dtvsync;
        if (dtvsync) {
            aml_dtvsync_setPause(dtvsync, false);
        }

        ALOGD("%s[%d] ret: %d", __func__, __LINE__, ret);
    }
    return ret;
}

int disable_dtv_patch_for_tuner_framework(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int ret = 0,val = 0,path_id = aml_out->demux_id;
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    aml_dtv_audio_instance_t *dtv_audio_instance = &dtv_audio_context->instances[path_id];
    struct timespec ts;

    val = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_CLOSE);
    dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, val);
    ALOGD("%s[%d]: ret: %d", __func__, __LINE__, ret);
    pthread_mutex_lock(&dtv_audio_context->dtv_cmd_process_mutex);
    if (dtv_audio_instance->dtv_audio_state != AUDIO_DTV_PATCH_DECODER_STATE_IDLE) {
        ts_wait_time(&ts, 200000);
        ALOGI("wait dtv_cmd_processed_cond");
        pthread_cond_timedwait(&dtv_audio_context->dtv_cmd_processed_cond, &dtv_audio_context->dtv_cmd_process_mutex, &ts);
        ALOGI("get dtv_cmd_processed_cond");
    }
    pthread_mutex_unlock(&dtv_audio_context->dtv_cmd_process_mutex);
    return ret;
}

int out_pause_dtv_stream_for_tunerframework(struct audio_stream_out *stream)
{
    int ret = 0,cmd = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int path_id = aml_out->demux_id;
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    aml_dtv_audiopara_t *dmx_info = &dtv_audio_context->instances[path_id].dtv_audio_info;
    aml_dtvsync_t *dtvsync = &dtv_audio_context->instances[path_id].dtvsync;

    clock_gettime(CLOCK_MONOTONIC, &aml_out->cbs_cmd_timestamp);
    ALOGD("%s[%d]", __func__, __LINE__);
    if (aml_out->stream_status == STREAM_PAUSED) {
        return ret;
    }
    //if (get_dev_patch(adev)) {
        //tv_set_ease(aml_out, EaseOut);
    //}

    cmd = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_PAUSE);
    ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, cmd);

    aml_out->stream_status = STREAM_PAUSED;
    return ret;
}
int out_resume_dtv_stream_for_tunerframework(struct audio_stream_out *stream)
{
    int ret = 0,cmd = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int path_id = aml_out->demux_id;
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    aml_dtv_audiopara_t *dmx_info = &dtv_audio_context->instances[path_id].dtv_audio_info;
    aml_dtvsync_t *dtvsync = &dtv_audio_context->instances[path_id].dtvsync;

    ALOGD("%s[%d]", __func__, __LINE__);

    if (aml_out->stream_status != STREAM_PAUSED) {
        return ret;
    }
    //if (get_dev_patch(adev)) {
       //tv_set_ease(aml_out, EaseIn);
    //}

    cmd = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_RESUME);
    ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, cmd);

    aml_out->stream_status = STREAM_HW_WRITING;
    return ret;
}


int out_flush_dtv_stream_for_tunerframework(struct audio_stream_out *stream)
{
    int ret = 0,cmd = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int path_id = aml_out->demux_id;
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    aml_dtv_audiopara_t *dmx_info = &dtv_audio_context->instances[path_id].dtv_audio_info;
    aml_dtvsync_t *dtvsync = &dtv_audio_context->instances[path_id].dtvsync;
    void *demux_handle = dtv_audio_context->instances[path_id].demux_handle;
    aml_dtv_audio_instance_t *dtv_audio_instance =  &get_dtv_audio_context(adev)->instances[path_id];

    int costtime_ms = 0;
    struct timespec curtime;
    clock_gettime(CLOCK_MONOTONIC, &curtime);
    costtime_ms = calc_time_interval_us(&aml_out->cbs_cmd_timestamp, &curtime) / 1000;
    clock_gettime(CLOCK_MONOTONIC, &aml_out->cbs_cmd_timestamp);
    ALOGI("costtime_ms %d",costtime_ms);

    ALOGD("%s[%d]", __func__, __LINE__);

    //In the ATF case, Only open and close stream, the stream status will be
    //set to STANDBY. However, during flushing the output and input data, ATF will not
    //call standby api, we could not drop the output and input data.
    if (aml_out->stream_status == STREAM_STANDBY) {
        ALOGD("%s[%d] aml_out->stream_status %d", __func__, __LINE__, aml_out->stream_status);
        return ret;
    }
    dtv_package_list_flush(dtv_audio_instance->dtv_package_list);
    if (dmx_info->dual_decoder_support) {
        Stop_Dmx_AD_Audio(demux_handle);
        Start_Dmx_AD_Audio(demux_handle);
    }
    Stop_Dmx_Main_Audio(demux_handle);
    Start_Dmx_Main_Audio(demux_handle);
    Flush_Dmx_Audio(demux_handle);
    int val = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_RESET_OUTPUT);
    dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, val);
    aml_out->standby = 1;
    if (aml_out->stream_status != STREAM_PAUSED || costtime_ms > 200) {
        return ret;
    }
    return ret;
}

int out_standby_dtv_stream_for_tunerframework(struct audio_stream *stream)
{
    int ret = 0,cmd = 0;

    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int path_id = aml_out->demux_id;
    ALOGD("%s[%d]", __func__, __LINE__);

    if (aml_out->stream_status == STREAM_STANDBY) {
        return ret;
    }

    cmd = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_RESET_OUTPUT);
    ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, cmd);
    aml_out->stream_status = STREAM_STANDBY;
    aml_out->standby = true;
    return ret;
}


int out_start_dtv_stream_for_tunerframework(struct audio_stream_out *stream)
{
    int ret = 0, cmd = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    int path_id = aml_out->demux_id;
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    aml_dtv_audiopara_t *dmx_info = &dtv_audio_context->instances[path_id].dtv_audio_info;
    aml_dtvsync_t *dtvsync = &dtv_audio_context->instances[path_id].dtvsync;

    ALOGI("aml_out->demux_id %d aml_out %p", aml_out->demux_id, aml_out);
    struct mediasync_audio_format audio_format;
    /*make dtv patch start via cmds*/
    ALOGD("%s[%d]", __func__, __LINE__);

    if (dtvsync) {
        aml_dtvsync_setPause(dtvsync, false);
    }
    cmd = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_RESUME);
    dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, cmd);
    cmd = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_START);
    ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, cmd);

    aml_out->stream_status = STREAM_HW_WRITING;
    return ret;
}

int out_stop_dtv_stream_for_tunerframework(struct audio_stream_out *stream)
{
    int ret = 0,cmd = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    int path_id = aml_out->demux_id;

    /*make dtv patch stop via cmds*/
    ALOGD("%s[%d]", __func__, __LINE__);

    cmd = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_STOP);
    ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, cmd);

    return ret;
}

int out_get_audio_description_mix_level(struct audio_stream_out *stream, float *leveldB)
{
    ALOGD("func:%s  stream:%p leveldB:%p", __func__, stream, leveldB);
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int path_id = aml_out->demux_id;
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    aml_dtv_audiopara_t *dtv_audio_info = &dtv_audio_context->instances[path_id].dtv_audio_info;

    *leveldB = dtv_audio_info->mixing_level;
    return 0;
}

int out_set_audio_description_mix_level(struct audio_stream_out *stream, const float leveldB)
{
    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    int path_id = aml_out->demux_id;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    aml_dtv_audiopara_t *dtv_audio_info = &dtv_audio_context->instances[path_id].dtv_audio_info;

    ALOGD("%s[%d] stream %p leveldB %f", __func__, __LINE__, stream, leveldB);
    dtv_audio_info->mixing_level = leveldB;

    return ret;
}

int out_set_dual_mono_mode(struct audio_stream_out *stream, audio_dual_mono_mode_t mode)
{
    ALOGD("func:%s  stream:%p mode:%d", __func__, stream, mode);
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    int path_id = aml_out->demux_id;
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    aml_dtv_audiopara_t *dtv_audio_info = &dtv_audio_context->instances[path_id].dtv_audio_info;

    dtv_audio_info->output_mode = convert2_aml_dual_mono_mode(mode);
    return 0;
}

int out_get_dual_mono_mode(struct audio_stream_out *stream, audio_dual_mono_mode_t *mode)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    int path_id = aml_out->demux_id;
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    aml_dtv_audiopara_t *dtv_audio_info = &dtv_audio_context->instances[path_id].dtv_audio_info;

    int sound_mode = dtv_audio_info->output_mode;
    *mode = convert2_android_dual_mono_mode(sound_mode);
    ALOGD("func:%s  stream:%p mode:%d", __func__, stream, *mode);
    return 0;
}

int out_set_volume_for_tunerframework(struct audio_stream_out *stream, float left, float right __unused)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    bool is_cbs_dtv_audio = dtv_tuner_framework(stream);
    int path_id = aml_out->demux_id;
    int ret = 0, val = 0;

    if (is_cbs_dtv_audio) {
        AM_LOGI("out:%p left:%f cbs_dtv_audio", stream, left);
        val = left * 100;
        val = (path_id << DVB_DEMUX_ID_BASE | val);
        ret = dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_VOLUME, val);

    }
    return ret;
}
bool on_stream_format_changed(struct aml_stream_out *aml_out) {
    int ret = false;
    struct aml_audio_device *adev= (struct aml_audio_device *)(aml_out)->dev;
    struct aml_audio_patch *audio_patch = get_dev_patch(adev);
    aml_dtv_audio_instance_t *dtv_audio_instance =  &get_dtv_audio_context(adev)->instances[aml_out->demux_id];
    if (!dtv_audio_instance) {
        ALOGE("dtv_audio_instance null return false");
        return false;
    }
    if (aml_out->hal_format != dtv_audio_instance->in_format) {
        aml_out->hal_format = dtv_audio_instance->in_format;
        ret = true;
    }

    if (aml_out->hal_channel_mask != dtv_audio_instance->in_chanmask) {
        aml_out->hal_channel_mask = dtv_audio_instance->in_chanmask;
        ret = true;
    }

    if (aml_out->hal_rate != dtv_audio_instance->input_sample_rate) {
        aml_out->hal_rate = dtv_audio_instance->input_sample_rate;
        ret = true;
    }

    return ret;
}

int out_set_playback_rate_parameters_for_tunerframework(struct audio_stream_out *stream, const audio_playback_rate_t *playbackRate)
{
    ALOGD("func:%s  stream:%p playbackRate->mSpeed:%f", __func__, stream, playbackRate->mSpeed);
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int path_id = aml_out->demux_id;
    aml_dtv_audio_instance_t *dtv_audio_instance =  &get_dtv_audio_context(adev)->instances[path_id];
    aml_dtvsync_t *dtvsync = &dtv_audio_instance->dtvsync;

    float speed = playbackRate->mSpeed;
    if (speed != 1.0f) {
        if (speed != aml_out->output_speed) {
            ALOGI("aml_audio_set_output_speed set speed :%f --> %f.\n",
                aml_out->output_speed, speed);
        }
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        //set_dolby_ms12_main_speed(&adev->ms12, (double)speed);
    }
    aml_dtvsync_setPlaybackRate(dtvsync, speed);

    aml_out->output_speed = speed;
    return 0;
}

int out_get_playback_rate_parameters_for_tunerframework(struct audio_stream_out *stream, audio_playback_rate_t *playbackRate)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;

    playbackRate->mSpeed = aml_out->output_speed;
    ALOGD("func:%s  stream:%p playbackRate->mSpeed:%f", __func__, stream, playbackRate->mSpeed);
    return 0;
}

ssize_t out_write_dtv_stream_for_tunerframework(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{

    int ret = 0,cmd = 0, val = 0;
    size_t total_bytes = bytes;
    size_t bytes_cost = 0;
    size_t hwsync_cost_bytes = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int path_id = aml_out->demux_id;
    aml_dtv_audio_instance_t *dtv_audio_instance =  &get_dtv_audio_context(adev)->instances[path_id];
    aml_dtv_audiopara_t *dmx_info = &dtv_audio_instance->dtv_audio_info;
    void *demux_handle = dtv_audio_instance->demux_handle;
    if (aml_out->hwsync == NULL) {
        aml_out->hwsync = aml_audio_calloc(1, sizeof(audio_hwsync_t));
        if (!aml_out->hwsync) {
            ALOGE("%s,malloc hwsync failed", __func__);
            return total_bytes;
        }
    }
    audio_hwsync_t *hw_sync = aml_out->hwsync;
    if (aml_out->standby) {
        out_start_dtv_stream_for_tunerframework(stream);
        aml_out->standby = false;
    }
    if (getprop_bool("vendor.media.audiohal.cbs.dump")) {
        aml_dump_audio_bitstreams("/data/audio/cbs_data.raw", buffer, bytes);
    }

    while (bytes_cost < total_bytes) {
        uint64_t  cur_pts = ULLONG_MAX;//defined in limits.h
        int outsize = 0;
        ALOGV("before aml_audio_hwsync_find_frame bytes %zu\n", total_bytes - bytes_cost);
        hwsync_cost_bytes = aml_audio_hwsync_find_frame(aml_out->hwsync, (char *)buffer + bytes_cost, total_bytes - bytes_cost, &cur_pts, &outsize);
        bytes_cost += hwsync_cost_bytes;
        if (cur_pts > ULLONG_MAX) {
            ALOGE("APTS exeed the max 64bit value");
        }
        ALOGV("after aml_audio_hwsync_find_frame bytes remain %zu,cost %zu,outsize %d,pts %"PRIx64"\n",
               total_bytes - bytes_cost - hwsync_cost_bytes, hwsync_cost_bytes, outsize, cur_pts);

        if (hw_sync->hw_sync_metadata_unit_type == AUDIO_ENCAPSULATION_METADATA_TYPE_FRAMEWORK_TUNER) {
            hw_avsync_metadata_unit_info_t *current_metadata_unit = &hw_sync->current_metadata_unit;
            if (current_metadata_unit->broadcast_type == AUDIO_BROADCAST_MAIN) {
                /*when switch to main track, audio hal receive AUDIO_BROADCAST_MAIN event,
                AUDIO_BROADCAST_MAIN means main dtv audiopath info changed, need reset main dtv audio path*/
                int main_fmt = android_fmt_convert_to_dmx_fmt(encodingFormat2AudioFormat(current_metadata_unit->flags));
                int main_pid = current_metadata_unit->stream_id & 0xFFFF;
                if (main_pid != dmx_info->main_pid || main_fmt != dmx_info->main_fmt) {
                    unset_dtv_audio_datasource(dtv_audio_instance);
                    if (dmx_info->dual_decoder_support) {
                        dmx_info->ad_pid = -1;
                        dmx_info->dual_decoder_support = 0;
                    }
                    dmx_info->main_pid = main_pid;
                    dmx_info->demux_id = (current_metadata_unit->stream_id >> 16)& 0xF;//demux id
                    dmx_info->main_fmt = main_fmt;
                    ALOGI("changed to main_pid %d  main_format %d stream_id %d ",dmx_info->main_pid, dmx_info->main_fmt, current_metadata_unit->stream_id);
                    set_dtv_audio_datasource(dtv_audio_instance);
                    val = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_RESET_OUTPUT);
                    dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, val);
                    val = (path_id << DVB_DEMUX_ID_BASE | AUDIO_DTV_PATCH_CMD_START);
                    dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, val);
                } else {
                    //do nothing
                }
            } else if (current_metadata_unit->broadcast_type == AUDIO_BROADCAST_AUDIO_DESCRIPTION) {
                /*when switch to ad track, audio hal receive AUDIO_BROADCAST_AUDIO_DESCRIPTION event,
                AUDIO_BROADCAST_AUDIO_DESCRIPTION means   dtv ad info changed, need enable ad function*/
                if (current_metadata_unit->stream_id != 0) {
                    if ((current_metadata_unit->stream_id & 0xff) != dmx_info->ad_pid) {
                        ALOGI("stream_id  %d ad_pid %d", current_metadata_unit->stream_id, dmx_info->ad_pid);
                        int  dmx_id  = (current_metadata_unit->stream_id >> 16)& 0xF;//demux id
                        dmx_info->ad_pid = current_metadata_unit->stream_id & 0xFFFF;
                        dmx_info->ad_fmt = dmx_info->main_fmt;
                        val = dmx_info->ad_pid;
                        Init_Dmx_AD_Audio(demux_handle, dmx_info->ad_fmt, dmx_info->ad_pid, 1);
                        Start_Dmx_AD_Audio(demux_handle);
                        val = (path_id << DVB_DEMUX_ID_BASE | val);
                        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_PID, val);
                        val = 1;
                        val = (path_id << DVB_DEMUX_ID_BASE | val);
                        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_SUPPORT, val);
                        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_ENABLE, val);
                        val = (path_id << DVB_DEMUX_ID_BASE | 100);
                        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_VOL_LEVEL, val);
                        dmx_info->ad_package_status = AD_PACK_STATUS_HOLD;
                    } else {
                        ALOGV("current_metadata_unit->stream_id %d",current_metadata_unit->stream_id);
                    }
                } else {
                    val = 0;
                    dmx_info->ad_pid = current_metadata_unit->stream_id & 0xFF;
                    if (dmx_info->dual_decoder_support) {
                        val = (path_id << DVB_DEMUX_ID_BASE | val);
                        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_SUPPORT, val);
                        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_ENABLE, val);
                        Stop_Dmx_AD_Audio(demux_handle);
                        Destroy_Dmx_AD_Audio(demux_handle);
                        ALOGI("current_metadata_unit->stream_id %d",current_metadata_unit->stream_id);
                    }
                }
            }
        } else if (hw_sync->hw_sync_metadata_unit_type == AUDIO_ENCAPSULATION_METADATA_TYPE_DVB_AD_DESCRIPTOR) {
             hw_avsync_metadata_dvb_ad_t *metadata_dvb_ad_info = &hw_sync->metadata_dvb_ad_info;
             //to do fade and pan
        } else if (hw_sync->hw_sync_metadata_unit_type == ENCAPSULATION_METADATA_TYPE_AD_PLACEMENT) {
             dmx_info->ad_placement =  hw_sync->hw_sync_metadata_placement;
             ALOGV("hw_sync_metadata_placement %d",  hw_sync->hw_sync_metadata_placement);
        }
        if (outsize > 0) {
            //to do package data
        }
        hw_sync->hw_sync_metadata_unit_type  = AUDIO_ENCAPSULATION_METADATA_TYPE_NONE;
    }

#if 0
    int ad_placement = property_get_int32("vendor.media.audio.ad.placement", -1);
    if (ad_placement != -1) {
         dmx_info->ad_placement = ad_placement;
         ALOGI(" dmx_info->ad_placement %d",  dmx_info->ad_placement);
    }
    float mixing_level = property_get_int32("vendor.media.audio.ad.mixing_level", 0);
    if (mixing_level != 0) {
        out_set_audio_description_mix_level(stream, mixing_level);
    }
#endif
    /*now just report aac heaac format, others to do */
    if (is_aac_format(dtv_audio_instance->aformat) && on_stream_format_changed(aml_out) && dtv_audio_instance->update_stable_count > FORMAT_STABLE_COUNT) {
        ALOGD("stream format changed to format %0x channelmask%0x samplerate %d",
            aml_out->hal_format, aml_out->hal_channel_mask, aml_out->hal_rate );
        out_stream_send_codec_event(stream, __FUNCTION__);
    }
    return bytes_cost;
}


int out_get_presentation_position_for_tunerframework (const struct audio_stream_out *stream, uint64_t *frames, struct timespec *timestamp) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int path_id = aml_out->demux_id;
    aml_dtv_audio_instance_t *dtv_audio_instance =  &get_dtv_audio_context(adev)->instances[path_id];
    struct audio_stream_out *dtv_stream = (struct audio_stream_out *)(dtv_audio_instance->dtv_stream_out.stream_out);
    if (dtv_stream) {
        return dtv_stream->get_presentation_position(dtv_stream, frames, timestamp);
    }
    ALOGI("%s(), not ready yet", __func__);
    return -EINVAL;
}
int out_set_params_for_tunerframework(struct audio_stream_out *stream,struct str_parms *parms) {
    int ret = 0;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct audio_hw_device *dev = (struct audio_hw_device *)(aml_out)->dev;
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int path_id = aml_out->demux_id;
    aml_dtv_audio_instance_t *dtv_audio_instance =  &get_dtv_audio_context(adev)->instances[path_id];
    aml_dtv_audiopara_t *dmx_info = &dtv_audio_instance->dtv_audio_info;
    struct audio_stream_out *dtv_stream = (struct audio_stream_out *)(dtv_audio_instance->dtv_stream_out.stream_out);

    int presentation_id = -1;
    ret = str_parms_get_int(parms, AUDIO_PARAMETER_STREAM_PRESENTATION_ID, &presentation_id);
    if (ret >= 0) {
        ALOGI("presentation_id %d ", presentation_id);
        presentation_id = (path_id << DVB_DEMUX_ID_BASE | presentation_id);
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_MEDIA_PRESENTATION_ID, presentation_id);
        int program_id = -1;
        ret = str_parms_get_int (parms, AUDIO_PARAMETER_STREAM_PROGRAM_ID, &program_id);
        if (ret >= 0) {
            ALOGI("program_id %d ", program_id);
            dmx_info->media_program_id = program_id;
            //program_id = (path_id << DVB_DEMUX_ID_BASE | program_id);
            //dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_MEDIA_PROGRAM_ID, program_id);
        }
    }
    return ret;
}
#endif

int set_dtv_parameters(struct audio_hw_device *dev, struct str_parms *parms)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int ret = -1, val = 0;

    /* dvb cmd deal with start */
    ret = str_parms_get_int(parms, "hal_param_dtv_spdif_protection_mode", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_SPDIF_PROTECTION_MODE, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_media_first_lang", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_MEDIA_FIRST_LANG, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_media_second_lang", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_MEDIA_SECOND_LANG, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_patch_cmd", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_CONTROL, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dual_dec_support", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_SUPPORT ,val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_ad_mix_enable", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_ENABLE ,val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_media_sync_id", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_MEDIA_SYNC_ID ,val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "ad_switch_enable", &val);
    if (ret >= 0) {
        adev->ad_switch_enable = val;
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_ENABLE, val);
        ALOGI("ad_switch_enable set to %d\n", adev->ad_switch_enable);
        goto exit;
    }

    ret = str_parms_get_int(parms, "dual_decoder_advol_level", &val);
    if (ret >= 0) {

        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_VOL_LEVEL, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dual_dec_mix_level", &val);
    if (ret >= 0) {

        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_MIX_LEVEL, val);
        goto exit;
    }
    ret = str_parms_get_int(parms, "hal_param_security_mem_level", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_SECURITY_MEM_LEVEL, val);
        goto exit;
    }
    ret = str_parms_get_int(parms, "hal_param_audio_output_mode", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_OUTPUT_MODE, val);
        goto exit;
    }
    ret = str_parms_get_int(parms, "hal_param_dtv_demux_id", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_DEMUX_INFO, val);
        goto exit;
    }
    ret = str_parms_get_int(parms, "hal_param_dtv_pid", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_PID, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_fmt", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_FMT, val);
        goto exit;
    }
    ret = str_parms_get_int(parms, "hal_param_dtv_audio_fmt", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_FMT, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_audio_id", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_PID, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_sub_audio_fmt", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_FMT, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_sub_audio_pid", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_AD_PID, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_has_dtv_video", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_HAS_VIDEO, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_tv_mute", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_MUTE, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_media_presentation_id", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_MEDIA_PRESENTATION_ID, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_audio_volume", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_VOLUME, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_dmx_id", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_DTV_DEMUX_ID, val);
        goto exit;
    }

    ret = str_parms_get_int(parms, "hal_param_dtv_playback_mode", &val);
    if (ret >= 0) {
        dtv_patch_handle_event(dev, AUDIO_DTV_PATCH_CMD_SET_PLAYBACK_MODE, val);
        goto exit;
    }

    /* dvb cmd deal with end */
exit:
    return ret;
}

