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

#ifndef _DTV_PATCH_HAL_AVSYNC_H_
#define _DTV_PATCH_HAL_AVSYNC_H_

#define PTSSERVER_DEVICE "/dev/ptsserver"
#define PTSSERVER_IOC_MAGIC 'P'
#define PTSSERVER_IOC_CHECKOUT_APTS   _IOW(PTSSERVER_IOC_MAGIC, 0x12, int)
#define PTSSERVER_IOC_INSTANCE_STATIC_BINDER   _IOW(PTSSERVER_IOC_MAGIC, 0x13, int)
#define PTSSERVER_IOC_GET_LIST_SIZE   _IOW(PTSSERVER_IOC_MAGIC, 0x14, int)
#define PTSSERVER_IOC_INSTANCE_SET_ID   _IOW(PTSSERVER_IOC_MAGIC, 0x15, int)
#define PTSSERVER_IOC_SET_OFFSET_MARGIN   _IOW(PTSSERVER_IOC_MAGIC, 0x16, int)


/* property */
#define PROPERTY_LOCAL_PASSTHROUGH_LATENCY  "vendor.media.dtv.passthrough.latencyms"
#define PROPERTY_AUDIO_TUNING_PCR_CLOCK_STEPS "vendor.media.audio.tuning.pcr.clocksteps"
#define PROPERTY_AUDIO_TUNING_CLOCK_FACTOR  "vendor.media.audio.tuning.clock.factor"
#define PROPERTY_AUDIO_DROP_THRESHOLD  "vendor.media.audio.drop.thresholdms"
#define PROPERTY_AUDIO_LEAST_CACHE  "vendor.media.audio.leastcachems"
#define PROPERTY_DEBUG_TIME_INTERVAL  "vendor.media.audio.debug.timeinterval"
#define PROPERTY_AUDIO_JUMPED_THRESHOLD_PROPERTY   "vendor.media.audio.dtv.jumped.threshold"
#define PROPERTY_AUDIO_RETUNE_THRESHOLD_PROPERTY   "vendor.media.audio.dtv.retune.threshold"
#define PROPERTY_AUDIO_MAX_CACHE_THRESHOLD         "vendor.media.audio.dtv.max.cache.threshold"
#define PROPERTY_AUDIO_UNDERRUN_MUTE_PROPERTY      "vendor.media.audio.hal.dtv.underrun.mute.enable"

#define PROPERTY_AUDIO_DISCONTINUE_THRESHOLD  "vendor.media.audio.discontinue_threshold"
#define PROPERTY_DTV_RESAMPLE_DISABLE         "vendor.media.audio.dtv.resample.disable"
#define PROPERTY_DTV_AUDIO_DROP_TIMEOUTMS     "vendor.media.audio.dtv.policy.drop.timeout"
#define PROPERTY_DTV_AUDIO_DROP_DISABLE       "vendor.media.audio.dtv.policy.drop.disable"
#define PROPERTY_DTV_AUDIO_HOLD_DISABLE       "vendor.media.audio.dtv.policy.hold.disable"
#define PROPERTY_DTV_FADED_OUT_MS             "vendor.media.audio.dtv.fadedout.ms"

/* audio clock tuning parameter */
#define DEFAULT_DTV_OUTPUT_CLOCK    (1000*1000)
#define DEFAULT_DTV_ADJUST_CLOCK    (1000)
#define DEFAULT_DTV_MIN_OUT_CLOCK   (1000*1000-100*1000)
#define DEFAULT_DTV_MAX_OUT_CLOCK   (1000*1000+100*1000)
#define DEFAULT_I2S_OUTPUT_CLOCK    (256*48000)
#define DEFAULT_CLOCK_MUL    (4)
#define DEFAULT_SPDIF_PLL_DDP_CLOCK    (256*48000*2)
#define DEFAULT_SPDIF_ADJUST_TIMES    (4)
#define DEFAULT_STRATEGY_ADJUST_CLOCK    (100)
#define DEFAULT_TUNING_PCR_CLOCK_STEPS (256 * 64)
#define DEFAULT_TUNING_CLOCK_FACTOR (7)

typedef struct ps_alloc_para {
    uint32_t mMaxCount;
    uint32_t mLookupThreshold;
    uint32_t kDoubleCheckThreshold;
} ptsserver_alloc_para;

typedef struct checkoutptsoffset {
    uint64_t offset;
    uint64_t pts_90k;
    uint64_t pts_64;
} checkout_pts_offset;
struct aml_dtv_audio_instance;
void dtv_audio_sync_prepare (aml_dec_t *aml_dec, aml_audio_buffer_t *audioBuffer);
void dtv_audio_sync_ms12_raw_check_in (struct audio_stream_out *stream, void *abuffer);
void dtv_audio_sync_nonms12_pts_update(struct audio_stream_out *stream, dec_data_info_t *dec_pcm_data, int frame_size);
int64_t lookup_apts_by_data_offset( struct aml_dtv_audio_instance *dtv_audio_instance, int64_t data_offset);
unsigned int dtv_audio_sync_non_ms12_process(struct audio_stream_out *stream, void *abuffer);
int get_dtv_sound_channel_mode(struct audio_stream_out *stream);


int32_t PtsServ_open();
int32_t PtsServ_close(int PServerDev);
int32_t PtsServ_ioctl(int32_t PServerDevId,
                             int32_t PServerCmd,
                             uint64_t param);


#endif  /* _DTV_PATCH_HAL_AVSYNC_H_ */
