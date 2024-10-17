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
//#define LOG_NDEBUG 0
//#define LOG_NALOGV 0

#include <utils/Log.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <inttypes.h>

#include "DolbyMS12.h"
#include "DolbyMS12ConfigParams.h"

namespace android
{

//static function pointer
int (*FuncGetMS12OutputMaxSize)(void);
void * (*FuncDolbyMS12Init)(int argc, char **pp_argv);
void (*FuncDolbyMS12Release)(void *);

/*this api will use params to config graph info*/
int (*FuncDolbyMS12InitAllParams)(void *, int argc, char **pp_argv);

/* ms12 Encoder API */
int (*FuncDolbyMs12EncoderOpen)(void *, int argc, char **pp_argv);
int (*FuncDolbyMs12EncoderClose)(void *);
/* ms12 Encoder API End*/

int (*FuncDolbyMS12InputSystem)(void *, const void *, size_t, int, int, int);
int (*FuncDolbyMS12InputDeepBuffer)(void *, const void *, size_t, int, int, int);
int (*FuncDolbyMS12InputApp)(void *, const void *, size_t, int, int, int);
int (*FuncDolbyMS12DapProcess)(void *, const void *, size_t, int, int, int);

#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
int (*FuncDolbyMS12RegisterOutputCallback)(output_callback , void *);
#else
int (*FuncDolbyMS12Output)(void *, const void *, size_t);
#endif

int (*FuncMS12ContinuousRegisterCallback)(void *, int, void *, void *);
int (*FuncMS12ContinuousUnregisterCallback)(void *, int);

int (*FuncDolbyMS12UpdateRuntimeParams)(void *, int , char **);
int (*FuncDolbyMS12UpdateRuntimeParamsNoLock)(void *, int , char **);
int (*FuncDolbyMS12SchedulerRun)(void *);
void (*FuncDolbyMS12SetQuitFlag)(int);
void (*FuncDolbyMS12FlushAppInputBuffer)(void);
unsigned long long (*FuncDolbyMS12GetContinuousNFramesPCMOutput)(void *, int);
void (*FuncDolbyMS12GetBitstreamOutputSize)(unsigned long long *, unsigned long long *);

int (*FuncDolbyMS12GetSystemBufferAvail)(int *);
int (*FuncDolbyMS12GetDeepBufferAvailFrames)(int *);

int (*FuncDolbyMS12GetGain)(int);
int (*FuncDolbyMS12Config)(ms12_config_type_t, ms12_config_t *);

int (*FuncDolbyMS12GetAudioInfo)(struct aml_audio_info *);
int (*FuncDolbyMS12GetMATDecLatency)(void);

void (*FuncDolbyMS12SetDebugLevel)(int);
unsigned long long (*FuncDolbyMS12GetNBytesConsumedSysSound)(void);
unsigned long long (*FuncDolbyMS12GetFramesConsumedDeepBufferAudio)(void);
int (*FuncDolbyMS12GetTotalNFramesDelay)(void *);
int (*FuncDumpDolbyMS12Info)(int);
char * (*FunDolbMS12GetVersion)(void);

/* MAT Encoder API Begin */
int (*FuncDolbyMS12MATEncoderInit)(int, int, unsigned int *, int, int, void **);
int (*FuncDolbyMS12MATEncoderCleanup)(void *);
int (*FuncDolbyMS12MATEncoderProcess)(void *, const unsigned char *, int, const unsigned char *, int *, int, int *);
int (*FuncDolbyMS12MATEncoderConfig)(void *, mat_enc_config_type_t, mat_enc_config_t *);
/* MAT Encoder API End */

int (*FuncMS12DeocderOpen)(void *, int *, void *);
int (*FuncMS12DeocderClose)(void *, int);
int (*FuncMS12DecoderProcess)(void *, int);
int (*FuncMS12DeocderPause)(void *, int);
int (*FuncMS12DeocderResume)(void *, int);
int (*FuncMS12DeocderFlush)(void *, int);
int (*FuncMS12DecoderMainWrite)(void *, int , void *, int , void *);
int (*FuncMS12DecoderAssociateWrite)(void *, int , void *, int , void *);
int (*FuncMS12DecoderSetparameter)(void *, int , int , void *, int);
int (*FuncMS12DecoderGetparameter)(void *, int, int, void *, int);
int (*FuncMS12DecoderRegisterCallback)(void *, int , int, void *, void *);
int (*FuncMS12DecoderUnregisterCallback)(void *, int, int);

DolbyMS12::DolbyMS12() :
    mDolbyMS12LibHandle(NULL)
{
    ALOGD("%s()", __FUNCTION__);
}


DolbyMS12::~DolbyMS12()
{
    ALOGD("%s()", __FUNCTION__);
}


int DolbyMS12::GetLibHandle(char *dolby_ms12_path)
{
    ALOGD("+%s()", __FUNCTION__);
    //ReleaseLibHandle();
    if (mDolbyMS12LibHandle) {
        ALOGI("%s lib exists", __func__);
        return 0;
    }
    //here there are two paths, "the DOLBY_MS12_LIB_PATH_A/B", where could exit that dolby ms12 library.
    mDolbyMS12LibHandle = dlopen(dolby_ms12_path, RTLD_NOW);
    if (!mDolbyMS12LibHandle) {
        ALOGE("%s, failed to load libdolbyms12 lib %s\n", __FUNCTION__, dlerror());
        goto ERROR;
    }

    FuncGetMS12OutputMaxSize = (int (*)(void)) dlsym(mDolbyMS12LibHandle, "get_ms12_output_max_size");
    if (!FuncGetMS12OutputMaxSize) {
        ALOGE("%s, dlsym get_ms12_output_max_size fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12Init = (void* (*)(int, char **)) dlsym(mDolbyMS12LibHandle, "ms12_init");
    if (!FuncDolbyMS12Init) {
        ALOGE("%s, dlsym ms12_init fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12Release = (void (*)(void *)) dlsym(mDolbyMS12LibHandle, "ms12_release");
    if (!FuncDolbyMS12Release) {
        ALOGE("%s, dlsym ms12_release fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12InitAllParams = (int (*)(void *, int argc, char **pp_argv)) dlsym(mDolbyMS12LibHandle, "ms12_init_all_params");
    if (!FuncDolbyMS12InitAllParams) {
        ALOGE("%s, dlsym ms12_init_all_params fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12InputSystem = (int (*)(void *, const void *, size_t, int, int, int)) dlsym(mDolbyMS12LibHandle, "ms12_input_system");
    if (!FuncDolbyMS12InputSystem) {
        ALOGE("%s, dlsym ms12_input_system fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12InputDeepBuffer = (int (*)(void *, const void *, size_t, int, int, int)) dlsym(mDolbyMS12LibHandle, "ms12_input_deepbuffer");
    if (!FuncDolbyMS12InputDeepBuffer) {
        ALOGE("%s, dlsym ms12_input_deepbuffer fail\n", __FUNCTION__);
    }

    FuncDolbyMS12InputApp = (int (*)(void *, const void *, size_t, int, int, int)) dlsym(mDolbyMS12LibHandle, "ms12_input_app");
    if (!FuncDolbyMS12InputApp) {
        ALOGE("%s, dlsym ms12_input_app fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12DapProcess = (int (*)(void *, const void *, size_t, int, int, int)) dlsym(mDolbyMS12LibHandle, "ms12_dap_process");
    if (!FuncDolbyMS12DapProcess) {
        ALOGE("%s, dlsym ms12_dap_process fail\n", __FUNCTION__);
        //goto ERROR;
    }

    FuncDolbyMS12FlushAppInputBuffer = (void (*)(void))  dlsym(mDolbyMS12LibHandle, "ms12_flush_app_input_buffer");
    if (!FuncDolbyMS12FlushAppInputBuffer) {
        ALOGE("%s, dlsym FuncDolbyMS12FlushAppInputBuffer fail\n", __FUNCTION__);
        goto ERROR;
    }

#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
    FuncDolbyMS12RegisterOutputCallback = (int (*)(output_callback , void *)) dlsym(mDolbyMS12LibHandle, "ms12_register_output_callback");
    if (!FuncDolbyMS12RegisterOutputCallback) {
        ALOGE("%s, dlsym ms12_output_register_output_callback fail\n", __FUNCTION__);
        goto ERROR;
    }
#else
    FuncDolbyMS12Output = (int (*)(void *, const void *, size_t))  dlsym(mDolbyMS12LibHandle, "ms12_output");
    if (!FuncDolbyMS12Output) {
        ALOGE("%s, dlsym ms12_output fail\n", __FUNCTION__);
        goto ERROR;
    }
#endif

    FuncMS12ContinuousRegisterCallback = (int (*)(void *, int, void *, void *)) dlsym(mDolbyMS12LibHandle, "ms12_continuous_register_callback");
    if (!FuncMS12ContinuousRegisterCallback) {
        ALOGE("%s, dlsym ms12_continuous_register_callback fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncMS12ContinuousUnregisterCallback = (int (*)(void *, int)) dlsym(mDolbyMS12LibHandle, "ms12_continuous_unregister_callback");
    if (!FuncMS12ContinuousUnregisterCallback) {
        ALOGE("%s, dlsym ms12_continuous_unregister_callback fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12UpdateRuntimeParams = (int (*)(void *, int , char **))  dlsym(mDolbyMS12LibHandle, "ms12_update_runtime_params");
    if (!FuncDolbyMS12UpdateRuntimeParams) {
        ALOGE("%s, dlsym ms12_update_runtime_params fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12UpdateRuntimeParamsNoLock = (int (*)(void *, int , char **))  dlsym(mDolbyMS12LibHandle, "ms12_update_runtime_params_nolock");
    if (!FuncDolbyMS12UpdateRuntimeParamsNoLock) {
        ALOGE("%s, dlsym ms12_update_runtime_params_nolock fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12SchedulerRun = (int (*)(void *))  dlsym(mDolbyMS12LibHandle, "ms12_scheduler_run");
    if (!FuncDolbyMS12SchedulerRun) {
        ALOGE("%s, dlsym ms12_scheduler_run fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12SetQuitFlag = (void (*)(int))  dlsym(mDolbyMS12LibHandle, "ms12_set_quit_flag");
    if (!FuncDolbyMS12SetQuitFlag) {
        ALOGE("%s, dlsym ms12_set_quit_flag fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12FlushAppInputBuffer = (void (*)(void))  dlsym(mDolbyMS12LibHandle, "ms12_flush_app_input_buffer");
    if (!FuncDolbyMS12FlushAppInputBuffer) {
        ALOGE("%s, dlsym FuncDolbyMS12FlushAppInputBuffer fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12GetBitstreamOutputSize = (void (*)(unsigned long long *, unsigned long long *))  dlsym(mDolbyMS12LibHandle, "get_bitstream_output_size");
    if (!FuncDolbyMS12GetBitstreamOutputSize) {
        ALOGE("%s, dlsym get_bitstream_output_size fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12GetSystemBufferAvail = (int (*)(int *))  dlsym(mDolbyMS12LibHandle, "get_system_buffer_avail");
    if (!FuncDolbyMS12GetSystemBufferAvail) {
        ALOGE("%s, dlsym get_system_buffer_avail fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12GetDeepBufferAvailFrames = (int (*)(int *))  dlsym(mDolbyMS12LibHandle, "get_deep_buffer_avail_frames");
    if (!FuncDolbyMS12GetDeepBufferAvailFrames) {
        ALOGE("%s, dlsym get_deep_buffer_avail fail\n", __FUNCTION__);
    }

    FuncDolbyMS12GetGain = (int (*)(int))  dlsym(mDolbyMS12LibHandle, "ms12_get_gain_int");
    if (!FuncDolbyMS12GetGain) {
        ALOGE("%s, dlsym get_system_buffer_avail fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12Config = (int (*)(ms12_config_type_t, ms12_config_t *))  dlsym(mDolbyMS12LibHandle, "ms12_audio_config");
    if (!FuncDolbyMS12Config) {
        ALOGE("%s, dlsym ms12_audio_config\n", __FUNCTION__);
    }

    FuncDumpDolbyMS12Info = (int (*)(int))  dlsym(mDolbyMS12LibHandle, "dump_dolby_ms12_info");
    if (!FuncDumpDolbyMS12Info) {
        ALOGE("%s, dlsym dump_dolby_ms12_info\n", __FUNCTION__);
    }

    FuncDolbyMS12GetAudioInfo = (int (*)(struct aml_audio_info *))  dlsym(mDolbyMS12LibHandle, "get_audio_info");
    if (!FuncDolbyMS12GetAudioInfo) {
        ALOGE("%s, dlsym get_audio_info fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMS12GetMATDecLatency = (int (*)(void))  dlsym(mDolbyMS12LibHandle, "get_mat_dec_delay");
    if (!FuncDolbyMS12GetMATDecLatency) {
        ALOGE("%s, dlsym get_mat_dec_delay fail\n", __FUNCTION__);
    }

    FuncDolbyMS12GetContinuousNFramesPCMOutput = (unsigned long long (*)(void *, int))  dlsym(mDolbyMS12LibHandle, "get_continuous_n_frames_pcm_output");
    if (!FuncDolbyMS12GetContinuousNFramesPCMOutput) {
        ALOGE("%s, dlsym get_continuous_n_frames_pcm_output fail\n", __FUNCTION__);
    }
    FuncDolbyMS12SetDebugLevel = (void (*)(int))  dlsym(mDolbyMS12LibHandle, "set_dolbyms12_debug_level");
    if (!FuncDolbyMS12SetDebugLevel) {
        ALOGE("%s, dlsym get_system_buffer_avail fail\n", __FUNCTION__);
    }

    FuncDolbyMS12GetNBytesConsumedSysSound = (unsigned long long (*)(void))  dlsym(mDolbyMS12LibHandle, "get_n_bytes_consumed_of_sys_sound");
    if (!FuncDolbyMS12GetNBytesConsumedSysSound) {
        ALOGW("%s, dlsym FuncDolbyMS12GetNBytesConsumedSysSound fail,ignore it as version difference\n", __FUNCTION__);
    }

    FuncDolbyMS12GetFramesConsumedDeepBufferAudio = (unsigned long long (*)(void))  dlsym(mDolbyMS12LibHandle, "get_frames_consumed_of_deep_buffer_audio");
    if (!FuncDolbyMS12GetFramesConsumedDeepBufferAudio) {
        ALOGW("%s, dlsym FuncDolbyMS12GetFramesConsumedDeepBufferAudio fail,ignore it as version difference\n", __FUNCTION__);
    }

    FuncDolbyMS12GetTotalNFramesDelay  = (int (*)(void *))  dlsym(mDolbyMS12LibHandle, "get_ms12_total_nframes_delay");
    if (!FuncDolbyMS12GetTotalNFramesDelay) {
        ALOGW("%s, dlsym get_ms12_total_delay fail, ignore it as version difference\n", __FUNCTION__);
    }

    /* MAT Encoder API Begin */
    FuncDolbyMS12MATEncoderInit = (int (*)(int, int, unsigned int *, int, int, void **))  dlsym(mDolbyMS12LibHandle, "mat_encoder_init");
    if (!FuncDolbyMS12MATEncoderInit) {
        ALOGW("%s, dlsym FuncDolbyMS12MATEncoderInit fail,ignore it as version difference\n", __FUNCTION__);
    }

    FuncDolbyMS12MATEncoderCleanup = (int (*)(void *))  dlsym(mDolbyMS12LibHandle, "mat_encoder_cleanup");
    if (!FuncDolbyMS12MATEncoderCleanup) {
        ALOGW("%s, dlsym FuncDolbyMS12MATEncoderCleanup fail,ignore it as version difference\n", __FUNCTION__);
    }

    FuncDolbyMS12MATEncoderProcess = (int (*)(void *, const unsigned char *, int, const unsigned char *, int *, int, int *))  dlsym(mDolbyMS12LibHandle, "mat_encoder_process");
    if (!FuncDolbyMS12MATEncoderProcess) {
        ALOGW("%s, dlsym FuncDolbyMS12MATEncoderProcess fail,ignore it as version difference\n", __FUNCTION__);
    }

    FuncDolbyMS12MATEncoderConfig = (int (*)(void *, mat_enc_config_type_t, mat_enc_config_t *))  dlsym(mDolbyMS12LibHandle, "mat_encoder_config");
    if (!FuncDolbyMS12MATEncoderConfig) {
        ALOGW("%s, dlsym FuncDolbyMS12MATEncoderConfig fail,ignore it as version difference\n", __FUNCTION__);
    }
    /* MAT Encoder API End */

    FuncDolbyMs12EncoderOpen = (int (*)(void *, int argc, char **pp_argv)) dlsym(mDolbyMS12LibHandle, "ms12_encoder_open");
    if (!FuncDolbyMs12EncoderOpen) {
        ALOGE("%s, dlsym ms12_encoder_open fail\n", __FUNCTION__);
        goto ERROR;
    }

    FuncDolbyMs12EncoderClose = (int (*)(void *)) dlsym(mDolbyMS12LibHandle, "ms12_encoder_close");
    if (!FuncDolbyMs12EncoderClose) {
        ALOGE("%s, dlsym ms12_encoder_close fail\n", __FUNCTION__);
        goto ERROR;
    }
    FunDolbMS12GetVersion = (char * (*)(void)) dlsym(mDolbyMS12LibHandle, "ms12_get_version");
    if (!FunDolbMS12GetVersion) {
        ALOGW("%s, dlsym FunDolbMS12GetVersion fail, ignore it as version difference\n", __FUNCTION__);
    }

    FuncMS12DeocderOpen = (int (*)(void *, int *, void *)) dlsym(mDolbyMS12LibHandle, "ms12_deocder_open");
    FuncMS12DeocderClose = (int (*)(void *, int)) dlsym(mDolbyMS12LibHandle, "ms12_deocder_close");
    FuncMS12DecoderProcess = (int (*)(void *, int)) dlsym(mDolbyMS12LibHandle, "ms12_decoder_process");
    FuncMS12DeocderPause = (int (*)(void *, int)) dlsym(mDolbyMS12LibHandle, "ms12_deocder_pause");
    FuncMS12DeocderResume = (int (*)(void *, int)) dlsym(mDolbyMS12LibHandle, "ms12_deocder_resume");
    FuncMS12DeocderFlush = (int (*)(void *, int)) dlsym(mDolbyMS12LibHandle, "ms12_deocder_flush");
    FuncMS12DecoderMainWrite = (int (*)(void *, int , void *, int, void *)) dlsym(mDolbyMS12LibHandle, "ms12_decoder_main_write");
    FuncMS12DecoderAssociateWrite = (int (*)(void *, int , void *, int , void *)) dlsym(mDolbyMS12LibHandle, "ms12_decoder_associate_write");
    FuncMS12DecoderSetparameter = (int (*)(void *, int , int , void *, int)) dlsym(mDolbyMS12LibHandle, "ms12_decoder_setparameter");
    FuncMS12DecoderGetparameter = (int (*)(void *, int, int, void *, int)) dlsym(mDolbyMS12LibHandle, "ms12_decoder_getparameter");
    FuncMS12DecoderRegisterCallback = (int (*)(void *, int , int, void *, void *)) dlsym(mDolbyMS12LibHandle, "ms12_decoder_register_callback");
    FuncMS12DecoderUnregisterCallback = (int (*)(void *, int, int)) dlsym(mDolbyMS12LibHandle, "ms12_decoder_unregister_callback");

    ALOGD("-%s() line %d get libdolbyms12 success!", __FUNCTION__, __LINE__);
    return 0;

ERROR:
    ALOGD("-%s() line %d", __FUNCTION__, __LINE__);
    return -1;
}

void DolbyMS12::ReleaseLibHandle(void)
{
    ALOGD("+%s()", __FUNCTION__);

    //re-value the api as NULL
    FuncGetMS12OutputMaxSize = NULL;
    FuncDolbyMS12Init = NULL;
    FuncDolbyMS12Release = NULL;
    FuncDolbyMS12InitAllParams = NULL;
    FuncDolbyMS12InputSystem = NULL;
    FuncDolbyMS12InputDeepBuffer = NULL;
#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
    FuncDolbyMS12RegisterOutputCallback = NULL;
#else
    FuncDolbyMS12Output = NULL;
#endif
    FuncDolbyMS12UpdateRuntimeParams = NULL;
    FuncDolbyMS12SchedulerRun = NULL;
    FuncDolbyMS12SetQuitFlag = NULL;
    FuncDolbyMS12GetBitstreamOutputSize = NULL;
    FuncDolbyMS12GetSystemBufferAvail = NULL;
    FuncDolbyMS12GetDeepBufferAvailFrames = NULL;
    FuncDolbyMS12Config = NULL;
    FuncDumpDolbyMS12Info = NULL;
    FuncDolbyMS12GetAudioInfo = NULL;
    FuncDolbyMS12GetMATDecLatency = NULL;
    FuncDolbyMS12SetDebugLevel = NULL;
    FuncDolbyMS12GetNBytesConsumedSysSound = NULL;
    FuncDolbyMS12GetFramesConsumedDeepBufferAudio = NULL;
    FuncDolbyMS12GetTotalNFramesDelay = NULL;

    /* MAT Encoder API Begin */
    FuncDolbyMS12MATEncoderInit = NULL;
    FuncDolbyMS12MATEncoderCleanup = NULL;
    FuncDolbyMS12MATEncoderProcess = NULL;
    FuncDolbyMS12MATEncoderConfig = NULL;
    /* MAT Encoder API End */

    FuncDolbyMs12EncoderOpen = NULL;
    FuncDolbyMs12EncoderClose = NULL;
    FuncDolbyMS12GetContinuousNFramesPCMOutput = NULL;

    FuncMS12DeocderOpen = NULL;
    FuncMS12DeocderClose = NULL;
    FuncMS12DecoderProcess = NULL;
    FuncMS12DeocderPause = NULL;
    FuncMS12DeocderResume = NULL;
    FuncMS12DeocderFlush = NULL;
    FuncMS12DecoderMainWrite = NULL;
    FuncMS12DecoderAssociateWrite = NULL;
    FuncMS12DecoderSetparameter = NULL;
    FuncMS12DecoderGetparameter = NULL;
    FuncMS12DecoderRegisterCallback = NULL;
    FuncMS12DecoderUnregisterCallback = NULL;
    if (mDolbyMS12LibHandle != NULL) {
        dlclose(mDolbyMS12LibHandle);
        mDolbyMS12LibHandle = NULL;
    }

    ALOGD("-%s()", __FUNCTION__);
    return ;
}

int DolbyMS12::GetMS12OutputMaxSize(void)
{
    ALOGD("+%s()", __FUNCTION__);
    int ret = 0;
    if (!FuncGetMS12OutputMaxSize) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncGetMS12OutputMaxSize)();
    ALOGD("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}


void * DolbyMS12::DolbyMS12Init(int configNum, char **configParams)
{
    void * dolby_ms12_init_ret = NULL;
    ALOGD("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Init) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return NULL;
    }

    dolby_ms12_init_ret = (*FuncDolbyMS12Init)(configNum, configParams);
    ALOGD("-%s() dolby_ms12_init_ret %p", __FUNCTION__, dolby_ms12_init_ret);
    return dolby_ms12_init_ret;
}

char * DolbyMS12:: DolbMS12GetVersion(void)
{
    char *versioninfo = NULL;
    ALOGV("+%s()", __FUNCTION__);
    if (!FunDolbMS12GetVersion) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return NULL;
    }
    versioninfo = (*FunDolbMS12GetVersion)();
    return versioninfo;
}
void DolbyMS12::DolbyMS12Release(void *DolbyMS12Pointer)
{
    ALOGD("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Release) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ;
    }

    (*FuncDolbyMS12Release)(DolbyMS12Pointer);
    ALOGD("-%s()", __FUNCTION__);
    return ;
}


int DolbyMS12::DolbyMS12InitAllParams(void *DolbyMS12Pointer, int configNum, char **configParams)
{
    int ret = 0;
    ALOGD("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12InitAllParams) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12InitAllParams)(DolbyMS12Pointer, configNum, configParams);
    ALOGD("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMs12EncoderOpen(void *DolbyMS12Pointer, int configNum, char **configParams)
{
    ALOGV("+%s()", __FUNCTION__);
    int ret = 0;

    if (!FuncDolbyMs12EncoderOpen) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMs12EncoderOpen)(DolbyMS12Pointer, configNum, configParams);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMs12EncoderClose(void *DolbyMS12Pointer)
{
    ALOGV("+%s()", __FUNCTION__);
    int ret = 0;

    if (!FuncDolbyMs12EncoderClose) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMs12EncoderClose)(DolbyMS12Pointer);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12InputSystem(
    void *DolbyMS12Pointer
    , const void *audio_stream_out_buffer //ms12 input buffer
    , size_t audio_stream_out_buffer_size //ms12 input buffer size
    , int audio_stream_out_format
    , int audio_stream_out_channel_num
    , int audio_stream_out_sample_rate
)
{
    ALOGV("+%s()", __FUNCTION__);
    int ret = 0;

    if (!FuncDolbyMS12InputSystem) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12InputSystem)(DolbyMS12Pointer
                                      , audio_stream_out_buffer //ms12 input buffer
                                      , audio_stream_out_buffer_size //ms12 input buffer size
                                      , audio_stream_out_format
                                      , audio_stream_out_channel_num
                                      , audio_stream_out_sample_rate);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12InputDeepBuffer(
    void *DolbyMS12Pointer
    , const void *audio_stream_out_buffer //ms12 input buffer
    , size_t audio_stream_out_buffer_size //ms12 input buffer size
    , int audio_stream_out_format
    , int audio_stream_out_channel_num
    , int audio_stream_out_sample_rate
)
{
    ALOGV("+%s()", __FUNCTION__);
    int ret = 0;

    if (!FuncDolbyMS12InputDeepBuffer) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12InputDeepBuffer)(DolbyMS12Pointer
                                      , audio_stream_out_buffer //ms12 input buffer
                                      , audio_stream_out_buffer_size //ms12 input buffer size
                                      , audio_stream_out_format
                                      , audio_stream_out_channel_num
                                      , audio_stream_out_sample_rate);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}


int DolbyMS12::DolbyMS12InputApp(
    void *DolbyMS12Pointer
    , const void *audio_stream_out_buffer //ms12 input buffer
    , size_t audio_stream_out_buffer_size //ms12 input buffer size
    , int audio_stream_out_format
    , int audio_stream_out_channel_num
    , int audio_stream_out_sample_rate
)
{
    ALOGV("+%s()", __FUNCTION__);
    int ret = 0;

    if (!FuncDolbyMS12InputApp) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12InputApp)(DolbyMS12Pointer
                                      , audio_stream_out_buffer //ms12 input buffer
                                      , audio_stream_out_buffer_size //ms12 input buffer size
                                      , audio_stream_out_format
                                      , audio_stream_out_channel_num
                                      , audio_stream_out_sample_rate);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12DapProcess(
    void *DolbyMS12Pointer
    , const void *audio_stream_out_buffer //ms12 input buffer
    , size_t audio_stream_out_buffer_size //ms12 input buffer size
    , int audio_stream_out_format
    , int audio_stream_out_channel_num
    , int audio_stream_out_sample_rate
)
{
    ALOGV("+%s()", __FUNCTION__);
    int ret = 0;

    if (!FuncDolbyMS12DapProcess) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12DapProcess)(DolbyMS12Pointer
                                      , audio_stream_out_buffer //ms12 input buffer
                                      , audio_stream_out_buffer_size //ms12 input buffer size
                                      , audio_stream_out_format
                                      , audio_stream_out_channel_num
                                      , audio_stream_out_sample_rate);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

#ifdef REPLACE_OUTPUT_BUFFER_WITH_CALLBACK
int DolbyMS12::DolbyMS12RegisterOutputCallback(output_callback callback, void *priv_data)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12RegisterOutputCallback) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12RegisterOutputCallback)(callback, priv_data);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
#else
int DolbyMS12::DolbyMS12Output(
    void *DolbyMS12Pointer
    , const void *ms12_out_buffer //ms12 output buffer
    , size_t request_out_buffer_size //ms12 output buffer size
)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Output) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12Output)(DolbyMS12Pointer
                                 , ms12_out_buffer //ms12 output buffer
                                 , request_out_buffer_size //ms12 output buffer size
                                );
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
#endif

int DolbyMS12::MS12ContinuousRegisterCallback(void *dolbyMS12_pointer, int callback_type, void *callback, void *priv_data) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12ContinuousRegisterCallback)(dolbyMS12_pointer, callback_type, callback, priv_data);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::MS12ContinuousUnregisterCallback(void *dolbyMS12_pointer, int callback_type) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12ContinuousUnregisterCallback)(dolbyMS12_pointer, callback_type);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12UpdateRuntimeParams(void *DolbyMS12Pointer, int configNum, char **configParams)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12UpdateRuntimeParams) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12UpdateRuntimeParams)(DolbyMS12Pointer, configNum, configParams);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12UpdateRuntimeParamsNoLock(void *DolbyMS12Pointer, int configNum, char **configParams)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12UpdateRuntimeParamsNoLock) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12UpdateRuntimeParamsNoLock)(DolbyMS12Pointer, configNum, configParams);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12SchedulerRun(void *DolbyMS12Pointer)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12SchedulerRun) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return -1;
    }

    ret = (*FuncDolbyMS12SchedulerRun)(DolbyMS12Pointer);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

void DolbyMS12::DolbyMS12SetQuitFlag(int is_quit)
{
    int ret = 0;
    ALOGV("+%s() is_quit %d", __FUNCTION__, is_quit);
    if (!FuncDolbyMS12SetQuitFlag) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ;
    }

    (*FuncDolbyMS12SetQuitFlag)(is_quit);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ;
}

void DolbyMS12::DolbyMS12FlushAppInputBuffer(void)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12FlushAppInputBuffer) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ;
    }

    (*FuncDolbyMS12FlushAppInputBuffer)();
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ;
}

/*
idx = 0, primary
idx = 1, secondary
idx = 2, system
*/
int DolbyMS12::DolbyMS12GetGain(int idx)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetGain) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12GetGain)(idx);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

void DolbyMS12::DolbyMS12GetBitstreamOutputSize(unsigned long long *all_output_size, unsigned long long *ms12_generate_zero_size)
{
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetBitstreamOutputSize) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ;
    }

    (*FuncDolbyMS12GetBitstreamOutputSize)(all_output_size, ms12_generate_zero_size);
    ALOGV("-%s() *all_output_size %llu *ms12_generate_zero_size %llu", __FUNCTION__, *all_output_size,  *ms12_generate_zero_size);
    return ;
}

int DolbyMS12::DolbyMS12GetSystemBufferAvail(int * max_size)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetSystemBufferAvail) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12GetSystemBufferAvail)(max_size);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12GetDeepBufferAvailFrames(int * max_size)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetDeepBufferAvailFrames) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12GetDeepBufferAvailFrames)(max_size);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12SetMainVolume(float volume)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_MAIN_VOLUME, (ms12_config_t *)&volume);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12SetFadeInMaxDetectTimeMs(int time_ms)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_FADEIN_MAX_DETECT_TIME_MS, (ms12_config_t *)&time_ms);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}


int DolbyMS12::DolbyMS12SetMATStreamProfile(int stream_profile)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_MAT_STREAM_PROFILE, (ms12_config_t *)&stream_profile);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12SetAtmosDrop(int atmos_drop)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_ATMOS_DROP, (ms12_config_t *)&atmos_drop);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

void DolbyMS12::DumpDolbyMS12Info(int fd)
{
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDumpDolbyMS12Info) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
    }
    else {
        (*FuncDumpDolbyMS12Info)(fd);
    }

}

int DolbyMS12::DolbyMS12GetInputISDolbyAtmos()
{
    int ret = 0;
    struct aml_audio_info p_aml_audio_info;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetAudioInfo) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12GetAudioInfo)(&p_aml_audio_info);
    ALOGV("-%s() ret %d atmos Detected %d", __FUNCTION__, ret, p_aml_audio_info.is_dolby_atmos);
    return p_aml_audio_info.is_dolby_atmos;
}

unsigned int DolbyMS12::DolbyMS12GetAacProfile()
{
    int ret = 0;
    struct aml_audio_info p_aml_audio_info = {0,0,0,0};
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetAudioInfo) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12GetAudioInfo)(&p_aml_audio_info);
    ALOGV("-%s() ret %d aac profile = %d (0:AAC 1:HEAAC_v1 2:HEAAC_V2)", __FUNCTION__, ret, p_aml_audio_info.aac_profile);
    return p_aml_audio_info.aac_profile;
}

int DolbyMS12::DolbyMS12GetMATDecLatency()
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetMATDecLatency) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12GetMATDecLatency)();
    return ret;
}


int DolbyMS12::DolbyMS12EnableMixerMaxSize(int enable)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_MIXER_MAX_SIZE_ENABLED, (ms12_config_t *)&enable);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12SetCompressionFormat(int compression_format)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_COMPRESSION_FORMAT, (ms12_config_t *)&compression_format);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12SetSchedulerState(int sch_state)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_SCHEDULER_STATE, (ms12_config_t *)&sch_state);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

unsigned long long DolbyMS12::DolbyMS12GetContinuousNFramesPcmOutput(void *ms12_pointer, int index)
{
    uint64_t ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetContinuousNFramesPCMOutput) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12GetContinuousNFramesPCMOutput)(ms12_pointer, index);
    ALOGV("-%s() ret %" PRId64 "", __FUNCTION__, ret);
    return ret;
}


void DolbyMS12::DolbyMS12SetDebugLevel(int level)
{
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12SetDebugLevel) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
    }
    else
        (*FuncDolbyMS12SetDebugLevel)(level);
}

unsigned long long DolbyMS12::DolbyMS12GetNBytesConsumedSysSound(void)
{
    unsigned long long ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetNBytesConsumedSysSound) {
        return ret;
    }

    ret = (*FuncDolbyMS12GetNBytesConsumedSysSound)();
    ALOGV("-%s() ret %llu", __FUNCTION__, ret);
    return ret;
}

unsigned long long DolbyMS12::DolbyMS12GetFramesConsumedDeepBufferAudio(void)
{
    unsigned long long ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetFramesConsumedDeepBufferAudio) {
        return ret;
    }

    ret = (*FuncDolbyMS12GetFramesConsumedDeepBufferAudio)();
    ALOGV("-%s() ret %llu", __FUNCTION__, ret);
    return ret;
}


int DolbyMS12::DolbyMS12GetTotalNFramesDelay(void *ms12_pointer)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12GetTotalNFramesDelay) {
        return -1;
    }

    ret = (*FuncDolbyMS12GetTotalNFramesDelay)(ms12_pointer);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12GetLatencyForStereoOut(int *latency)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_STEREO_OUT_LATENCY, (ms12_config_t *)latency);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}


int DolbyMS12::DolbyMS12GetLatencyForMultiChannelOut(int *latency)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_MULTICHANNEL_OUT_LATENCY, (ms12_config_t *)latency);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12GetLatencyForDAPSpeakerOut(int *latency)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_DAP_SPEAKER_OUT_LATENCY, (ms12_config_t *)latency);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12GetLatencyForDAPHeadphoneOut(int *latency)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_DAP_HEADPHONE_OUT_LATENCY, (ms12_config_t *)latency);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12GetLatencyForDDPOut(int *latency)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_DDP_OUT_LATENCY, (ms12_config_t *)latency);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12GetLatencyForDDOut(int *latency)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_DD_OUT_LATENCY, (ms12_config_t *)latency);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12GetLatencyForMATOut(int *latency)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_MAT_OUT_LATENCY, (ms12_config_t *)latency);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}


/* MAT Encoder API Begin */
int DolbyMS12::DolbyMS12MATEncoderInit(int b_lfract_precision
    , int b_chmod_locking
    , unsigned int *p_matenc_maxoutbufsize
    , int b_iec_header
    , int dbg_enable
    , void **mat_enc_handle)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12MATEncoderInit) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12MATEncoderInit)(b_lfract_precision, b_chmod_locking, p_matenc_maxoutbufsize, b_iec_header, dbg_enable, mat_enc_handle);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

void DolbyMS12::DolbyMS12MatEncoderCleanup(void *mat_enc_handle)
{
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12MATEncoderCleanup) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
    }
    else {
        (*FuncDolbyMS12MATEncoderCleanup)(mat_enc_handle);
    }
}

int DolbyMS12::DolbyMS12MATEncoderProcess(void *mat_enc_handle
    , const unsigned char *in_buf
    , int n_bytes_in_buf
    , const unsigned char *out_buf
    , int *n_bytes_out_buf
    , int out_buf_max_size
    , int *nbytes_consumed
    )
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12MATEncoderProcess) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12MATEncoderProcess)(mat_enc_handle, in_buf, n_bytes_in_buf, out_buf, n_bytes_out_buf, out_buf_max_size, nbytes_consumed);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12MATEncoderConfig(void *mat_enc_handle
    , mat_enc_config_type_t config_type
    , mat_enc_config_t *config)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12MATEncoderConfig) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12MATEncoderConfig)(mat_enc_handle, config_type, config);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

/* MAT Encoder API End */

int DolbyMS12::DolbyMS12GetAC4ActivePresentation(int *presentation_group_index)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_AC4DEC_GET_ACTIVE_PRESENTATION, (ms12_config_t *)presentation_group_index);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12AC4DecCheckThePgiIsPresent(int presentation_group_index)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_AC4DEC_CHECK_THE_PGI_IS_PRESENT, (ms12_config_t *)&presentation_group_index);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12SetAlsaDelayFrame(int delay_frame)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_ALSA_DELAY_FRAME, (ms12_config_t *)&delay_frame);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12SetAlsaLimitFrame(int limit_frame)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_ALSA_LIMIT_FRAME, (ms12_config_t *)&limit_frame);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::DolbyMS12SetSchedulerSleep(int enable_sleep)
{
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncDolbyMS12Config)(MS12_CONFIG_SCHEDULER_SLEEP, (ms12_config_t *)&enable_sleep);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

int DolbyMS12::MS12DeocderOpen(void *dolbyMS12_pointer, int *ms12_decid, void *codec_info) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DeocderOpen)(dolbyMS12_pointer, ms12_decid, codec_info);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DeocderClose(void *dolbyMS12_pointer, int ms12_decid) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DeocderClose)(dolbyMS12_pointer, ms12_decid);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DecoderProcess(void *dolbyMS12_pointer, int ms12_decid) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DecoderProcess)(dolbyMS12_pointer, ms12_decid);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DeocderPause(void *dolbyMS12_pointer, int ms12_decid) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DeocderPause)(dolbyMS12_pointer, ms12_decid);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DeocderResume(void *dolbyMS12_pointer, int  ms12_decid) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DeocderResume)(dolbyMS12_pointer, ms12_decid);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DeocderFlush(void *dolbyMS12_pointer, int ms12_decid) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DeocderFlush)(dolbyMS12_pointer, ms12_decid);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DecoderMainWrite(void *dolbyMS12_pointer, int ms12_decid, void *buffer, int size, void *pcm_info) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DecoderMainWrite)(dolbyMS12_pointer, ms12_decid, buffer, size, pcm_info);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DecoderAssociateWrite(void *dolbyMS12_pointer, int ms12_decid, void *buffer, int size, void *pcm_info) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DecoderAssociateWrite)(dolbyMS12_pointer, ms12_decid, buffer, size, pcm_info);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DecoderSetparameter(void *dolbyMS12_pointer, int ms12_decid, int parameter_type, void *parameter, int size) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DecoderSetparameter)(dolbyMS12_pointer, ms12_decid, parameter_type, parameter, size);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DecoderGetparameter(void *dolbyMS12_pointer, int ms12_decid, int parameter_type, void *parameter, int size) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DecoderGetparameter)(dolbyMS12_pointer, ms12_decid, parameter_type, parameter, size);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DecoderRegisterCallback(void *dolbyMS12_pointer, int ms12_decid, int callback_type, void *callback, void *priv_data) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DecoderRegisterCallback)(dolbyMS12_pointer, ms12_decid, callback_type, callback, priv_data);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}
int DolbyMS12::MS12DecoderUnregisterCallback(void *dolbyMS12_pointer, int ms12_decid, int callback_type) {
    int ret = 0;
    ALOGV("+%s()", __FUNCTION__);
    if (!FuncDolbyMS12Config) {
        ALOGE("%s(), pls load lib first.\n", __FUNCTION__);
        return ret;
    }

    ret = (*FuncMS12DecoderUnregisterCallback)(dolbyMS12_pointer, ms12_decid, callback_type);
    ALOGV("-%s() ret %d", __FUNCTION__, ret);
    return ret;
}

/*--------------------------------------------------------------------------*/
}   // namespace android
