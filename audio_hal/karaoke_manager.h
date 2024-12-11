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

#ifndef _KARAOKE_MANAGER_H
#define _KARAOKE_MANAGER_H

#include <alsa_device_profile.h>
#include <alsa_device_proxy.h>
#include <pthread.h>
#include <aml_ringbuffer.h>
#include <aml_echo_reference.h>
#include <audio_data_process.h>
#include <hardware/audio.h>
#include "aml_audio_resample_manager.h"

struct audioCfg;
struct aml_ms12_dec_info;

struct voice_in {
    struct audioCfg cfg;
    bool debug;
    struct pcm *pcm_handle;
    struct pcm_config pcm_in_config;
    alsa_device_profile *in_profile;
    alsa_device_proxy proxy;
    void *conversion_buffer;
    size_t conversion_buffer_size;
};

typedef enum KARA_RECORD_TYPE {
    KARA_RECORD_TYPE_NONE             = -1,
    KARA_RECORD_TYPE_MIC_ORIGINAL     = 0,
    KARA_RECORD_TYPE_MIC_AFTER_VOLUME = 1,
    KARA_RECORD_TYPE_MIC_AFTER_REVERB = 2,
    KARA_RECORD_TYPE_MIC_AFTER_SW_MIX = 3,
    KARA_RECORD_TYPE_MAX              = 4,
} kara_record_type_t;

typedef enum KARA_INPUT_TYPE {
    KARA_INPUT_TYPE_NONE          = -1,
    KARA_INPUT_TYPE_USB_MIC       = 0,
    KARA_INPUT_TYPE_LINEIN_MIC    = 1,
    KARA_INPUT_TYPE_ALOOP         = 2,
    KARA_INPUT_TYPE_RING_BUFFER   = 3,
    KARA_INPUT_TYPE_3RD_PARTY     = 4,
    KARA_INPUT_TYPE_MAX           = 5,
} kara_input_type_t;

typedef enum KARA_OUTPUT_TYPE {
    KARA_OUTPUT_TYPE_NONE         = -1,
    KARA_OUTPUT_TYPE_SW_MIX       = 0,
    KARA_OUTPUT_TYPE_HW_MIX       = 1,
    KARA_OUTPUT_TYPE_RING_BUFFER  = 2,
    KARA_OUTPUT_TYPE_3RD_PARTY    = 3,
    KARA_OUTPUT_TYPE_MAX          = 4,
} kara_output_type_t;

struct kara_manager {
    pthread_mutex_t lock;
    kara_input_type_t kara_input_type;
    kara_output_type_t kara_output_type;
    bool karaoke_on;
    bool karaoke_enable;
    bool karaoke_start;
    bool kara_mic_record;
    bool kara_mic_mute;
    bool kara_mic_volume_enable;
    float kara_mic_gain;
    /* reverb for mic */
    void *reverb_handle;
    bool reverb_enable;
    int reverb_mode;
    struct voice_in in;
    void *buf;
    size_t buf_len;
    ring_buffer_t mic_buffer;
    struct audioCfg mixout_config;
    struct echo_reference_itfe *echo_reference;
    struct pcm *loopback_handle;
    int (*open)(struct kara_manager *in, struct audioCfg *cfg);
    int (*close)(struct kara_manager *in);
    /* mixer audio data to output */
    int (*mix)(struct kara_manager *in, void *buffer, size_t bytes);
    /* read data from ringbuffer */
    ssize_t (*read)(struct kara_manager *in, void *buffer, size_t bytes);
    audio_resample_config_t resample_config;
    aml_audio_resample_t *resample_handle;
    ring_buffer_t resample_buffer;
    int resample_wait_count;
};

void put_echo_reference(struct kara_manager *kara,
                          struct echo_reference_itfe *reference);

struct echo_reference_itfe *get_echo_reference(struct kara_manager *kara,
        audio_format_t format,
        uint32_t channel_count,
        uint32_t sampling_rate);

/* karaoke init*/
int karaoke_init(struct kara_manager *kara,
                      alsa_device_profile *profile,
                      kara_input_type_t kara_input_type,
                      kara_output_type_t kara_output_type);

/* karaoke check if should do capture and mix */
int karaoke_check_mix_output(struct kara_manager *karaoke, void *buffer, size_t bytes);
/* karaoke get audio config from ms12 info */
int karaoke_get_audioCfg_from_ms12_info(struct audioCfg *cfg, struct aml_ms12_dec_info *ms12_info);
/* karaoke get audio config from pcm_config */
int karaoke_get_audioCfg_from_pcm_config(struct audioCfg *cfg, struct pcm_config *pcm_cfg);
/* karaoke close */
int karaoke_close(struct kara_manager *kara);

/* karaoke key parameters get && set function*/
kara_input_type_t karaoke_get_input_type(struct kara_manager *kara);
int karaoke_set_input_type(struct kara_manager *kara, int value);
kara_output_type_t karaoke_get_output_type(struct kara_manager *kara);
int karaoke_set_output_type(struct kara_manager *kara, int value);
bool karaoke_get_on(struct kara_manager *kara);
int karaoke_set_on(struct kara_manager *kara, bool value);
bool karaoke_get_enable(struct kara_manager *kara);
int karaoke_set_enable(struct kara_manager *kara, bool value);
bool karaoke_get_start(struct kara_manager *kara);
int karaoke_set_start(struct kara_manager *kara, bool value);
bool karaoke_get_mic_record(struct kara_manager *kara);
int karaoke_set_mic_record(struct kara_manager *kara, bool value);
bool karaoke_get_mic_mute(struct kara_manager *kara);
int karaoke_set_mic_mute(struct kara_manager *kara, bool value);
bool karaoke_get_volume_enable(struct kara_manager *kara);
int karaoke_set_volume_enable(struct kara_manager *kara, bool value);
float karaoke_get_mic_gain(struct kara_manager *kara);
int karaoke_set_mic_gain(struct kara_manager *kara, float value);
bool karaoke_get_reverb_enable(struct kara_manager *kara);
int karaoke_set_reverb_enable(struct kara_manager *kara, bool value);
float karaoke_get_reverb_mode(struct kara_manager *kara);
int karaoke_set_reverb_mode(struct kara_manager *kara, int value);
bool karaoke_get_debug(struct kara_manager *kara);
int karaoke_set_debug(struct kara_manager *kara, bool value);

/* get audio config for recording such as adev_open_input_stream */
int karaoke_get_config_by_record_type(struct kara_manager *kara,
                                                    kara_record_type_t record_type,
                                                    struct audio_config *config);

/* --------------------related to project setting--------------------------- */
/* karaoke project config */
struct karaoke_config {
    bool feature_enable; /* karaoke enable config in hal, can not be modified by param */
    int default_input_type; /* default input type which defined by KARA_INPUT_TYPE */
    int default_output_type; /* default output type which defined by KARA_OUTPUT_TYPE */
    bool default_mic_volume_enable; /* default volume process enable config */
    float default_mic_volume_gain; /* default volume gain value */
    bool default_reverb_enable; /* default reverb process enable config */
    int default_reverb_mode; /* default reverb mode */
};
/* get config from json */
void karaoke_get_project_config(void *audio_config);
/* config set to usb&linein karaoke and do other init */
int karaoke_project_init(void *aml_adev);

/* set karaoke parameters on adev_set_parameters
   parameter format "hal_param_karaoke_set=[Name] [Command] [Value]"
   example: hal_param_karaoke_set=usb switch 1
*/
int karaoke_set_parameters(struct audio_hw_device *dev, char *param_value);

#endif
