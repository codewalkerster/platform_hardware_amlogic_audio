/*
 * Copyright (C) 2019 Amlogic Corporation.
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
 *
 * Function:
 * this file is created for starting play avsync
 */

#define LOG_TAG "audio_hw_input_dtv"

#include <cutils/atomic.h>
#include <cutils/log.h>
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
#include <system/audio.h>
#include <time.h>
#include <utils/Timers.h>
#include <hardware/audio.h>
#include <aml_android_utils.h>
#include <aml_data_utils.h>
#include "aml_audio_stream.h"
#include "audio_hw_utils.h"
#include "aml_audio_ms12_sync.h"
#include "dtv_patch.h"
#include "dtv_patch_hal_avsync.h"
#include "dtv_private_object.h"
#include "aml_ddp_dec_api.h"
#include "audio_hw_resource_mgr.h"
#include "dolby_lib_api.h"

int32_t PtsServ_close(int PServerDev)
{
    int32_t ret = -1;
    if (PServerDev < 0) {
        ALOGE("PServerDev is fail\n");
        return ret;
    }

    ret = close(PServerDev);
    if (ret < 0) {
        ALOGE("close is fail\n");
    } else {
        PServerDev = -1;
    }
    return ret;
}

int32_t PtsServ_open()
{
    int32_t r = -1;
    int32_t retry_open_times = 0;
retry_open:
    r = open(PTSSERVER_DEVICE, O_WRONLY);
    if (r < 0) {
        if (errno != -13) {
            retry_open_times++;
            usleep(10000);
            if (retry_open_times < 20) {
                goto retry_open;
            }
        }
        ALOGE("PtsServ_open [%s] failed,ret=%d error=%d(%s) used_times=%d*10(ms)\n",
                                PTSSERVER_DEVICE ,
                                r,
                                errno,
                                strerror(errno),
                                retry_open_times);
    }
    return r;
}

int32_t PtsServ_ioctl(int32_t PServerDevId,
                             int32_t PServerCmd,
                             uint64_t param)
{
    int32_t ret = -1;
    if (PServerDevId < 0) {
        ALOGE("PtsServ_ioctl PServerDevId:%d\n", PServerDevId);
        return ret;
    }

    ret = ioctl(PServerDevId, PServerCmd, param);
    if (ret < 0) {
        ALOGE("PtsServ_ioctl cmd [%d] faided,ret:%d error:%d(%s)\n",
                PServerCmd, ret, errno,strerror(errno));
    }
    return ret;
}

int64_t lookup_apts_by_data_offset(struct aml_dtv_audio_instance *dtv_audio_instance, int64_t data_offset)
{
    checkout_pts_offset checkout_pts;
    struct aml_audio_device *adev = aml_adev_get_handle();
    int64_t out_pts;
    /*look up the checkin list by dynamic margin size that valued by es data framesize */
    if (dtv_audio_instance->in_read_frame_size  && dtv_audio_instance->pts_margin != dtv_audio_instance->in_read_frame_size) {
        dtv_audio_instance->pts_margin = dtv_audio_instance->in_read_frame_size;
        PtsServ_ioctl(dtv_audio_instance->PServerDev, PTSSERVER_IOC_SET_OFFSET_MARGIN, (unsigned long)&dtv_audio_instance->pts_margin);
    }

    checkout_pts.offset = data_offset;
    if (dtv_audio_instance->PServerDev != -1) {
        PtsServ_ioctl(dtv_audio_instance->PServerDev, PTSSERVER_IOC_CHECKOUT_APTS, (unsigned long)&checkout_pts);
    }

    if (checkout_pts.pts_90k != -1) {
        out_pts = checkout_pts.pts_90k;
    } else {
        out_pts = 0;
    }
    if (adev->debug_flag > 1)
        ALOGD("offset:%" PRId64 " PtsServ_checkout_pts64:%" PRId64 " out_pts  %" PRId64 "\n",checkout_pts.offset, checkout_pts.pts_64, out_pts);

    return out_pts;

}

void dtv_audio_sync_prepare (aml_dec_t *aml_dec, aml_audio_buffer_t *audioBuffer)
{
    if (aml_dec) {
        if (audioBuffer->apts != DTVSYNC_INVALID_PTS) {
            if (audioBuffer->apts != 0) {
                aml_dec->in_frame_pts = audioBuffer->apts;
                aml_dec->out_frames = 0;
            }
        }
        aml_dtv_audiopara_t *dtv_audio_info = (aml_dtv_audiopara_t *)audioBuffer->privObject;
        if (dtv_audio_info) {
            aml_dec->ad_data = dtv_audio_info->ad_data;
            aml_dec->ad_size =  dtv_audio_info->ad_size;
        }
    }
}
void dtv_audio_sync_ms12_raw_check_in (struct audio_stream_out *stream, void *abuffer)
{
   aml_audio_buffer_t *inBuffer = (aml_audio_buffer_t *)abuffer;
   struct aml_audio_device *adev = aml_adev_get_handle();
   struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;

    /* audio data/apts, we send the APTS at first*/
    if (adev->debug_flag) {
        ALOGI("%s dolby pts %" PRIu64 " decoder_offset =%" PRIu64 "", __func__, inBuffer->apts, aml_out->hwsync->payload_offset);
    }
    if (inBuffer->apts != DTVSYNC_INVALID_PTS) {
        //set_ms12_main_audio_pts(ms12, patch->cur_package->pts, decoder_offset);
        aml_audio_hwsync_checkin_apts(aml_out->hwsync, aml_out->hwsync->payload_offset, inBuffer->apts);
    }
    aml_out->hwsync->payload_offset += inBuffer->size;
}

void dtv_audio_sync_nonms12_pts_update(struct audio_stream_out *stream, dec_data_info_t *dec_pcm_data, int frame_size)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    aml_dec_t *aml_dec = aml_out->aml_dec;
    int decoder_remain_size = 0;
    int decoder_latency = 0;
    int decoder_remain_cache = 0;

    if (is_dolby_ddp_support_compression_format(aml_dec->format)) {
        struct dolby_ddp_dec *ddp_dec = (struct dolby_ddp_dec *)aml_dec;
        decoder_remain_size = ddp_dec->remain_size;
        decoder_remain_cache = (decoder_remain_size > frame_size / 2) ? DDP_DECODER_CACHE : 0;
        decoder_latency = DDP_DECODER_CACHE + decoder_remain_cache;
    }
    aml_dec->out_frame_pts = aml_dec->in_frame_pts + (90 * aml_dec->out_frames /(dec_pcm_data->data_sr / 1000));
    aml_dec->out_frames += dec_pcm_data->data_len / (int)(audio_bytes_per_sample(get_primary_out_format(adev)) * dec_pcm_data->data_ch);
    if (get_debug_value(AML_DEBUG_AUDIOHAL_AUT)) {
       ALOGI("pes_pts: %" PRIx64 ", frame_pts: %" PRIx64 ", pcm[len:%d, pcm_dur:%dms, total_dur:%dms].",\
           aml_dec->in_frame_pts, aml_dec->out_frame_pts, dec_pcm_data->data_len,\
           dec_pcm_data->data_len * 1000 / ((int)audio_bytes_per_sample(get_primary_out_format(adev)) * dec_pcm_data->data_ch * dec_pcm_data->data_sr),\
           aml_dec->out_frames /(dec_pcm_data->data_sr / 1000));
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        aml_audio_hwsync_checkin_apts(aml_out->hwsync, aml_out->hwsync->payload_offset, aml_dec->out_frame_pts);
        aml_out->hwsync->payload_offset += dec_pcm_data->data_len;
    }
}

dtvsync_process_res dtv_audio_sync_non_ms12_process(struct audio_stream_out *stream, void *abuffer) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    int path_id = aml_out->demux_id;
    aml_dtv_audio_context_t *dtv_audio_context = get_dtv_audio_context(adev);
    aml_dtv_audiopara_t *dtv_audio_info = &dtv_audio_context->instances[path_id].dtv_audio_info;
    dtvsync_process_res process_result = DTVSYNC_AUDIO_OUTPUT;
    aml_dtvsync_t *dtvsync = (aml_dtvsync_t *)aml_out->hwsync->mediasync;
    aml_audio_buffer_t *outPcmBuffer = (aml_audio_buffer_t *)abuffer;
    int duration = 0;
    bool speed_enabled = false;
    aml_dec_t *aml_dec = aml_out->aml_dec;

    if (outPcmBuffer->bufFormat.channelCount != 0)
        duration =  (outPcmBuffer->size * 1000) / (2 * outPcmBuffer->bufFormat.channelCount * aml_out->config.rate);

    if (aml_out->offload_mute) {
        memset(outPcmBuffer->pData, 0, outPcmBuffer->size);
    }
    aml_audio_switch_output_mode((int16_t *)outPcmBuffer->pData, outPcmBuffer->size, aml_dec->output_format, dtv_audio_info->output_mode);

    if (dtvsync) {
        int alsa_latency = 90 *(out_get_alsa_latency_frames(stream)  * 1000) / aml_out->config.rate;
        int ddp_tuning_latency = 90 * aml_audio_dtv_get_nonms12_latency(stream) / 48;
        int force_setting_delay = 0;
        if (is_arc_connected(adev)) {
            force_setting_delay = 90 * aml_getprop_int(PROPERTY_LOCAL_PASSTHROUGH_LATENCY);
        }

        dtvsync->cur_outapts = aml_dec->out_frame_pts - alsa_latency + ddp_tuning_latency + force_setting_delay;
        if (get_debug_value(AML_DEBUG_AUDIOHAL_AUT)) {
            ALOGI("frame_pts:%" PRIx64 ", output_pts:%" PRIx64 ", latency:%" PRId64 " ms.",\
                aml_dec->out_frame_pts, dtvsync->cur_outapts,\
                (aml_dec->out_frame_pts - dtvsync->cur_outapts) / 90);
        }
    }

    //sync process here
    if (aml_out->dtvsync_enable && dtvsync) {
        process_result = aml_dtvsync_nonms12_process(stream, duration, &speed_enabled);
    }
    return process_result;
}

