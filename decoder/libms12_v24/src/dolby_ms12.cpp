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

#define LOG_TAG "audio_hw_decoder_ms12v2"
// #define LOG_NDEBUG 0
// #define LOG_NALOGV 0

#include <utils/Log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dlfcn.h>

#include "DolbyMS12.h"
#include "DolbyMS12ConfigParams.h"


/*--------------------------------------------------------------------------*/
/*C Plus Plus to C adapter*/
static android::Mutex mLock;
static android::DolbyMS12* gInstance = NULL;
static android::DolbyMS12* getInstance()
{
    // ALOGV("+%s() dolby ms12\n", __FUNCTION__);
    android::Mutex::Autolock autoLock(mLock);
    if (gInstance == NULL) {
        gInstance = new android::DolbyMS12();
    }
    // ALOGV("-%s() dolby ms12 gInstance %p\n", __FUNCTION__, gInstance);
    return gInstance;
}

extern "C" void dolby_ms12_self_cleanup(void)
{
    ALOGV("+%s()\n", __FUNCTION__);
    android::Mutex::Autolock autoLock(mLock);
    if (gInstance) {
        delete gInstance;
        gInstance = NULL;
    }
    ALOGV("-%s() gInstance %p\n", __FUNCTION__, gInstance);
    return ;
}

extern "C" int get_libdolbyms12_handle(char *dolby_ms12_path)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->GetLibHandle(dolby_ms12_path);
    } else {
        return -1;
    }
}

extern "C" int release_libdolbyms12_handle(void)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        dolby_ms12_instance->ReleaseLibHandle();
        return 0;
    } else {
        return -1;
    }
}

extern "C" int get_dolby_ms12_output_max_size(void)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->GetMS12OutputMaxSize();
    } else {
        return -1;
    }
}

extern "C" void * dolby_ms12_init(int configNum, char **configParams)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12Init(configNum, configParams);
    } else {
        return NULL;
    }
}

extern "C" char * dolby_ms12_get_version(void)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbMS12GetVersion();
    } else {
        return NULL;
    }
}

extern "C" void dolby_ms12_release(void *dolbyMS12_pointer)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        dolby_ms12_instance->DolbyMS12Release(dolbyMS12_pointer);
    }
}

extern "C" int dolby_ms12_init_all_params(void *dolbyMS12_pointer, int configNum, char **configParams)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12InitAllParams(dolbyMS12_pointer, configNum, configParams);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_encoder_open(void *dolbyMS12_pointer, int configNum, char **configParams)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMs12EncoderOpen(dolbyMS12_pointer, configNum, configParams);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_encoder_close(void *dolbyMS12_pointer)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMs12EncoderClose(dolbyMS12_pointer);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_input_system(void *dolbyMS12_pointer
                                       , const void *audio_stream_out_buffer //ms12 input buffer
                                       , size_t audio_stream_out_buffer_size //ms12 input buffer size
                                       , int audio_stream_out_format
                                       , int audio_stream_out_channel_num
                                       , int audio_stream_out_sample_rate
                                      )
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance)
        return dolby_ms12_instance->DolbyMS12InputSystem(dolbyMS12_pointer
                , audio_stream_out_buffer //ms12 input buffer
                , audio_stream_out_buffer_size //ms12 input buffer size
                , audio_stream_out_format
                , audio_stream_out_channel_num
                , audio_stream_out_sample_rate
                                                        );
    else {
        return -1;
    }
}

extern "C" int dolby_ms12_input_deep_buffer(void *dolbyMS12_pointer
                                       , const void *audio_stream_out_buffer //ms12 input buffer
                                       , size_t audio_stream_out_buffer_size //ms12 input buffer size
                                       , int audio_stream_out_format
                                       , int audio_stream_out_channel_num
                                       , int audio_stream_out_sample_rate
                                      )
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance)
        return dolby_ms12_instance->DolbyMS12InputDeepBuffer(dolbyMS12_pointer
                , audio_stream_out_buffer //ms12 input buffer
                , audio_stream_out_buffer_size //ms12 input buffer size
                , audio_stream_out_format
                , audio_stream_out_channel_num
                , audio_stream_out_sample_rate);
    else {
        return -1;
    }
}


extern "C" int dolby_ms12_input_app(void *dolbyMS12_pointer
                                       , const void *audio_stream_out_buffer //ms12 input buffer
                                       , size_t audio_stream_out_buffer_size //ms12 input buffer size
                                       , int audio_stream_out_format
                                       , int audio_stream_out_channel_num
                                       , int audio_stream_out_sample_rate
                                      )
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance)
        return dolby_ms12_instance->DolbyMS12InputApp(dolbyMS12_pointer
                , audio_stream_out_buffer //ms12 input buffer
                , audio_stream_out_buffer_size //ms12 input buffer size
                , audio_stream_out_format
                , audio_stream_out_channel_num
                , audio_stream_out_sample_rate
                                                        );
    else {
        return -1;
    }
}

extern "C" int dolby_ms12_dap_process(
    void *dolbyMS12_pointer
    , const void *audio_stream_out_buffer //ms12 input buffer
    , size_t audio_stream_out_buffer_size //ms12 input buffer size
    , int audio_stream_out_format
    , int audio_stream_out_channel_num
    , int audio_stream_out_sample_rate
)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance)
        return dolby_ms12_instance->DolbyMS12DapProcess(dolbyMS12_pointer
                , audio_stream_out_buffer //ms12 input buffer
                , audio_stream_out_buffer_size //ms12 input buffer size
                , audio_stream_out_format
                , audio_stream_out_channel_num
                , audio_stream_out_sample_rate);
    else {
        return -1;
    }
}


#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
extern "C" int dolby_ms12_register_output_callback(void *callback, void *priv_data)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12RegisterOutputCallback((android::output_callback)callback, priv_data);
    } else {
        return -1;
    }
}
#else

extern "C" int dolby_ms12_output(void *dolbyMS12_pointer
                                 , const void *ms12_out_buffer //ms12 output buffer
                                 , size_t request_out_buffer_size //ms12 output buffer size
                                )
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance)
        return dolby_ms12_instance->DolbyMS12Output(dolbyMS12_pointer
                , ms12_out_buffer //ms12 output buffer
                , request_out_buffer_size //ms12 output buffer size
                                                   );
    else {
        return -1;
    }
}
#endif

extern "C" int dolby_ms12_continuous_register_callback(void *dolbyMS12_pointer, int callback_type, void *callback, void *priv_data)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12ContinuousRegisterCallback(dolbyMS12_pointer, callback_type, callback, priv_data);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_continuous_unregister_callback(void *dolbyMS12_pointer, int callback_type)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12ContinuousUnregisterCallback(dolbyMS12_pointer, callback_type);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_update_runtime_params(void *dolbyMS12_pointer, int configNum, char **configParams)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12UpdateRuntimeParams(dolbyMS12_pointer, configNum, configParams);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_update_runtime_params_nolock(void *dolbyMS12_pointer, int configNum, char **configParams)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12UpdateRuntimeParamsNoLock(dolbyMS12_pointer, configNum, configParams);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_scheduler_run(void *dolbyMS12_pointer)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12SchedulerRun(dolbyMS12_pointer);
    } else {
        return -1;
    }
}


extern "C" int dolby_ms12_set_quit_flag(int is_quit)
{
    ALOGI("%s() is_quit %d\n", __FUNCTION__, is_quit);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        dolby_ms12_instance->DolbyMS12SetQuitFlag(is_quit);
        return 0;
    } else {
        return -1;
    }
}

extern "C" void dolby_ms12_flush_app_input_buffer(void)
{
    ALOGI("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        dolby_ms12_instance->DolbyMS12FlushAppInputBuffer();
    }
}

/*
   idx = 0, primary
   idx = 1, secondary
   idx = 2, system
 */
extern "C" int dolby_ms12_get_gain(int idx)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetGain(idx);
    } else {
        return -1;
    }
}

extern "C" void dolby_ms12_get_bitstream_output_size(unsigned long long *all_output_size, unsigned long long *ms12_generate_zero_size)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        dolby_ms12_instance->DolbyMS12GetBitstreamOutputSize(all_output_size, ms12_generate_zero_size);
    }
}

extern "C" int dolby_ms12_get_system_buffer_avail(int * max_size)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetSystemBufferAvail(max_size);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_get_deep_buffer_avail_frames(int * max_size)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetDeepBufferAvailFrames(max_size);
    } else {
        return -1;
    }
}


extern "C" int dolby_ms12_set_main_volume(float volume)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12SetMainVolume(volume);
    }
    return -1;
}

extern "C" int dolby_ms12_set_fadein_max_detect_time_ms(int time_ms)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12SetFadeInMaxDetectTimeMs(time_ms);
    }
    return -1;
}

extern "C" int dolby_ms12_set_mat_stream_profile(int stream_profile)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12SetMATStreamProfile(stream_profile);
    }
    return -1;
}

extern "C" int dolby_ms12_enable_atmos_drop(int atmos_drop)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12SetAtmosDrop(atmos_drop);
    }
    return -1;
}

extern "C" int dolby_ms12_info_dump(int fd)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        dolby_ms12_instance->DumpDolbyMS12Info(fd);
    }
    return -1;
}

extern "C" int dolby_ms12_get_input_atmos_info()
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetInputISDolbyAtmos();
    } else {
        return -1;
    }
}

extern "C" unsigned int dolby_ms12_get_aac_profile()
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetAacProfile();
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_get_mat_dec_latency()
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetMATDecLatency();
    } else {
        return -1;
    }
}


extern "C" int dolby_ms12_enable_mixer_max_size(int enable)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12EnableMixerMaxSize(enable);
    }
    return -1;
}

extern "C" int dolby_ms12_set_dolby_compression_format(int compression_format)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12SetCompressionFormat(compression_format);
    }
    return -1;
}

extern "C" int dolby_ms12_set_scheduler_state(int sch_state)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12SetSchedulerState(sch_state);
    }
    return -1;
}

extern "C" unsigned long long dolby_ms12_get_continuous_nframes_pcm_output(void *ms12_pointer, int index)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetContinuousNFramesPcmOutput(ms12_pointer, index);
    } else {
        return -1;
    }
}


extern "C" void dolby_ms12_set_debug_level(int level)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        dolby_ms12_instance->DolbyMS12SetDebugLevel(level);
    }
}

extern "C" unsigned long long dolby_ms12_get_consumed_sys_audio(void)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetNBytesConsumedSysSound();
    } else {
        return -1;
    }
}

extern "C" unsigned long long dolby_ms12_get_consumed_deep_buffer_audio(void)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetFramesConsumedDeepBufferAudio();
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_get_total_nframes_delay(void *ms12_pointer)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetTotalNFramesDelay(ms12_pointer);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_get_latency_for_stereo_out(int *latency)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetLatencyForStereoOut(latency);
    }
    return -1;
}

extern "C" int dolby_ms12_get_latency_for_multichannel_out(int *latency)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetLatencyForMultiChannelOut(latency);
    }
    return -1;
}

extern "C" int dolby_ms12_get_latency_for_dap_speaker_out(int *latency)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetLatencyForDAPSpeakerOut(latency);
    }
    return -1;
}

extern "C" int dolby_ms12_get_latency_for_dap_headphone_out(int *latency)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetLatencyForDAPHeadphoneOut(latency);
    }
    return -1;
}

extern "C" int dolby_ms12_get_latency_for_ddp_out(int *latency)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetLatencyForDDPOut(latency);
    }
    return -1;
}

extern "C" int dolby_ms12_get_latency_for_dd_out(int *latency)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetLatencyForDDOut(latency);
    }
    return -1;
}

extern "C" int dolby_ms12_get_latency_for_mat_out(int *latency)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetLatencyForMATOut(latency);
    }
    return -1;
}

extern "C" int dolby_ms12_mat_encoder_init(int b_lfract_precision
        , int b_chmod_locking
        , unsigned int *p_matenc_maxoutbufsize
        , int b_iec_header
        , int dbg_enable
        , void **mat_enc_handle
        )
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12MATEncoderInit(b_lfract_precision, b_chmod_locking, p_matenc_maxoutbufsize, b_iec_header, dbg_enable, mat_enc_handle);
    }
    return -1;
}

extern "C" void dolby_ms12_mat_encoder_cleanup(void *mat_enc_handle)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        dolby_ms12_instance->DolbyMS12MatEncoderCleanup(mat_enc_handle);
    }
}

extern "C" int dolby_ms12_mat_encoder_process(void *mat_enc_handle
    , const unsigned char *in_buf
    , int n_bytes_in_buf
    , const unsigned char *out_buf
    , int *n_bytes_out_buf
    , int out_buf_max_size
    , int *nbytes_consumed
    )
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12MATEncoderProcess(mat_enc_handle, in_buf, n_bytes_in_buf, out_buf, n_bytes_out_buf, out_buf_max_size, nbytes_consumed);
    }
    return -1;
}

extern "C" int dolby_ms12_mat_encoder_config(void *mat_enc_handle
    , int config_type
    , int *config)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12MATEncoderConfig(mat_enc_handle, (mat_enc_config_type_t)config_type, (mat_enc_config_t *)config);
    }
    return -1;
}

extern "C" int dolby_ms12_get_ac4_active_presentation(int *presentation_group_index)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12GetAC4ActivePresentation(presentation_group_index);
    }
    return -1;
}

extern "C" int dolby_ms12_ac4dec_check_the_pgi_is_present(int presentation_group_index)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12AC4DecCheckThePgiIsPresent(presentation_group_index);
    }
    return -1;
}

extern "C" int dolby_ms12_set_alsa_delay_frame(int delay_frame)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12SetAlsaDelayFrame(delay_frame);
    }
    return -1;
}

extern "C" int dolby_ms12_set_alsa_limit_frame(int limit_frame)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12SetAlsaLimitFrame(limit_frame);
    }
    return -1;
}

extern "C" int dolby_ms12_set_scheduler_sleep(int enable_sleep)
{
    ALOGV("%s()\n", __FUNCTION__);
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->DolbyMS12SetSchedulerSleep(enable_sleep);
    }
    return -1;
}

extern "C" int dolby_ms12_deocder_open(void *dolbyMS12_pointer, int *ms12_decid, void *codec_info)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DeocderOpen(dolbyMS12_pointer, ms12_decid, codec_info);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_deocder_close(void *dolbyMS12_pointer, int ms12_decid)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DeocderClose(dolbyMS12_pointer, ms12_decid);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_decoder_process(void *dolbyMS12_pointer, int ms12_decid)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DecoderProcess(dolbyMS12_pointer, ms12_decid);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_deocder_pause(void *dolbyMS12_pointer, int ms12_decid)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DeocderPause(dolbyMS12_pointer, ms12_decid);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_deocder_resume(void *dolbyMS12_pointer, int  ms12_decid)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DeocderResume(dolbyMS12_pointer, ms12_decid);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_deocder_flush(void *dolbyMS12_pointer, int ms12_decid)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DeocderFlush(dolbyMS12_pointer, ms12_decid);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_decoder_main_write(void *dolbyMS12_pointer, int ms12_decid, void *buffer, int size, void *pcm_info)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DecoderMainWrite(dolbyMS12_pointer, ms12_decid, buffer, size, pcm_info);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_decoder_associate_write(void *dolbyMS12_pointer, int ms12_decid, void *buffer, int size, void *pcm_info)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DecoderAssociateWrite(dolbyMS12_pointer, ms12_decid, buffer, size, pcm_info);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_decoder_setparameter(void *dolbyMS12_pointer, int ms12_decid, int parameter_type, void *parameter, int size)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DecoderSetparameter(dolbyMS12_pointer, ms12_decid, parameter_type, parameter, size);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_decoder_getparameter(void *dolbyMS12_pointer, int ms12_decid, int parameter_type, void *parameter, int size)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DecoderGetparameter(dolbyMS12_pointer, ms12_decid, parameter_type, parameter, size);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_decoder_register_callback(void *dolbyMS12_pointer, int ms12_decid, int callback_type, void *callback, void *priv_data)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DecoderRegisterCallback(dolbyMS12_pointer, ms12_decid, callback_type, callback, priv_data);
    } else {
        return -1;
    }
}

extern "C" int dolby_ms12_decoder_unregister_callback(void *dolbyMS12_pointer, int ms12_decid, int callback_type)
{
    android::DolbyMS12* dolby_ms12_instance = getInstance();
    if (dolby_ms12_instance) {
        return dolby_ms12_instance->MS12DecoderUnregisterCallback(dolbyMS12_pointer, ms12_decid, callback_type);
    } else {
        return -1;
    }
}



