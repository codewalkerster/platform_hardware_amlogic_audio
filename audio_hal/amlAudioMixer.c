/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define LOG_TAG "audio_hw_hal_audiomixer"
//#define LOG_NDEBUG 0
#define DEBUG_DUMP 0

#define __USE_GNU
#include <cutils/log.h>
#include <errno.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <stdlib.h>
#include <system/audio.h>
#include <aml_volume_utils.h>
#include <inttypes.h>
#include <audio_utils/primitives.h>

#ifdef ENABLE_AEC_APP
#include "audio_aec.h"
#endif

#include "amlAudioMixer.h"
#include "audio_hw_utils.h"
#include "audio_hwsync.h"
#include "audio_hwsync_wrap.h"
#include "audio_data_process.h"
#include "audio_virtual_buf.h"
#include "dolby_lib_api.h"

#include "audio_hw.h"
#include "audio_bt_hal.h"
#include "audio_bt_sco.h"
#include "audio_usb_hal.h"
#include "aml_audio_timer.h"
#include "aml_malloc_debug.h"
#include "aml_audio_spdifout.h"
#include "audio_hw_resource_mgr.h"
#include "aml_mmap_audio.h"
#include "aml_audio_output.h"
#include "aml_audio_ms12_sync.h"
#include "aml_stream_manager.h"
#ifdef ENABLE_DVB_PATCH
#include "dtv_patch_hal_avsync.h"
#include "dtv_patch_dtvsync.h"
#include "dtv_patch.h"
#endif


#ifdef ENABLE_AUTOMOTIVE_AUDIO_FUNCTION
#include "../automotive/aml_channel_index.h"
#endif

enum {
    INPORT_NORMAL,   // inport not underrun
    INPORT_UNDERRUN, //inport doesn't have data, underrun may happen later
    INPORT_FEED_SILENCE_DONE, //underrun will happen quickly, we feed some silence data to avoid noise
};
const char *submix_state_2_string[SUBMIX_SCHEDULER_MAX] = {
    "SUBMIX_SCHEDULER_RUNNING",
    "SUBMIX_SCHEDULER_STANDBY",
};
#define DUMP_AUDIOMIXER_INPORT_READ     0x0010
#define DUMP_AUDIOMIXER_INDUMP          0x0020
#define DUMP_AUDIOMIXER_OUTDUMP         0x0040

#define AML_MIXER_INPUT_BUF_PCM_NS          (48000000LL)


static int get_audiomixer_dump_enable(int dump_type) {
    int value = 0;
    value = get_debug_value(AML_DUMP_AUDIOHAL_SUBMIXING);
    return (value & dump_type);
}

int mixer_set_state(struct amlAudioMixer *audio_mixer, aml_mixer_state state)
{
    audio_mixer->state = state;
    return 0;
}

int mixer_set_continuous_output(struct amlAudioMixer *audio_mixer,
        bool continuous_output)
{
    audio_mixer->continuous_output = continuous_output;
    return 0;
}

bool mixer_is_continuous_enabled(struct amlAudioMixer *audio_mixer)
{
    return audio_mixer->continuous_output;
}

aml_mixer_state mixer_get_state(struct amlAudioMixer *audio_mixer)
{
    return audio_mixer->state;
}

/*
* Default, mixer output config is the same as output port config
* but, user can set different mixer output config from output port
*/
int mixer_get_default_config(struct audioCfg *cfg, bool is_tv)
{
    output_get_default_config(cfg, is_tv);
    AM_LOGD("format:0x%x channels:%d rate:%d frame_size:%d",
        cfg->format, cfg->channelCnt, cfg->sampleRate, cfg->frame_size);
    return 0;
}

int mixer_change_config_format(struct audioCfg *cfg, audio_format_t format)
{
    if (cfg->format != format) {
        cfg->format = format;
        cfg->frame_size = cfg->channelCnt * audio_bytes_per_sample(cfg->format);
    }
    return 0;
}



/**
 * Returns the first initialized port according to pMasks and resets
 * the corresponding bit. Can be called repeatedly to iterate over
 * initialized ports.
 */
static inline input_port *mixer_get_inport_by_mask_right_first(
        struct amlAudioMixer *audio_mixer, uint32_t *pMasks)
{
    uint8_t bit_position = get_bit_position_in_mask(NR_INPORTS - 1, pMasks);
    return audio_mixer->in_ports[bit_position];
}

/**
 * Returns the index of the first available supported port.
 */
static unsigned int mixer_get_available_inport_index(struct amlAudioMixer *audio_mixer)
{
    unsigned int index = 0;
    unsigned int mask = audio_mixer->inportsAvailMasks;

    AM_LOGD("+inportsAvailMasks: %#x", audio_mixer->inportsAvailMasks);
    index = __builtin_ctz(audio_mixer->inportsAvailMasks);
    audio_mixer->inportsAvailMasks &= ~(1 << index);
    AM_LOGD("-inportsAvailMasks:%#x, index %d", audio_mixer->inportsAvailMasks, index);
    return index;
}

int add_new_input_port_on_mixer(struct amlAudioMixer *audio_mixer, input_port *in_port)
{
    int port_index = mixer_get_available_inport_index(audio_mixer);
    if (port_index <= -1 || port_index >= NR_INPORTS) {
        AM_LOGE("Invalid input_port index:%d", port_index);
        return -1;
    }

    if (audio_mixer->in_ports[port_index] != NULL) {
        AM_LOGW("inport index:[%d]%s already exists! recreate", port_index, mixerInputType2Str(port_index));
        free_input_port(audio_mixer->in_ports[port_index]);
    }

    in_port->ID = port_index;
    audio_mixer->in_ports[port_index] = in_port;
    audio_mixer->inportsMasks |= 1 << port_index;
    return 0;
}

int init_mixer_input_port(struct amlAudioMixer *audio_mixer,
        struct audio_config *config,
        audio_output_flags_t flags,
        int (*on_notify_cbk)(void *data),
        void *notify_data,
        int (*on_input_avail_cbk)(void *data),
        void *input_avail_data,
        /*meta_data_cbk_t*/void *on_meta_data_cbk __unused,
        void *meta_data __unused,
        float volume)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");
    R_CHECK_POINTER_LEGAL(-EINVAL, config, "");
    R_CHECK_POINTER_LEGAL(-EINVAL, notify_data, "");

    input_port *in_port = NULL;
    uint8_t port_index = -1;
    struct aml_stream_out *aml_out = notify_data;
    bool direct_on = false;

    if (aml_out->inputPortID != -1) {
       AM_LOGW("stream input port id:%d exits delete it.", aml_out->inputPortID);
       delete_mixer_input_port(audio_mixer, aml_out->inputPortID);
    }
    /* if direct on, ie. the ALSA buffer is full, no need padding data anymore  */
    direct_on = (audio_mixer->in_ports[AML_MIXER_INPUT_PORT_PCM_DIRECT] != NULL);
    struct audioCfg portConfig;
    setPortConfig(&portConfig, config);
    in_port = new_input_port(MIXER_FRAME_COUNT, &portConfig, flags, volume, direct_on, false);
    if (in_port == NULL) {
        AM_LOGE("new_input_port is NULL");
        return -1;
    }
    port_index = mixer_get_available_inport_index(audio_mixer);
    /*coverity[leaked_storage]*/
    R_CHECK_PARAM_LEGAL(-1, port_index, 0, NR_INPORTS - 1, "");

    if (audio_mixer->in_ports[port_index] != NULL) {
        AM_LOGW("inport index:[%d]%s already exists! recreate", port_index, mixerInputType2Str(port_index));
        free_input_port(audio_mixer->in_ports[port_index]);
    }

    in_port->ID = port_index;
    AM_LOGI("input port:%s, size %d frames, frame_write_sum:%" PRId64 "",
        mixerInputType2Str(in_port->enInPortType), MIXER_FRAME_COUNT, aml_out->frame_write_sum);
    audio_mixer->in_ports[port_index] = in_port;
    audio_mixer->inportsMasks |= 1 << port_index;
    aml_out->inputPortID = port_index;

    set_port_notify_cbk(in_port, on_notify_cbk, notify_data);
    set_port_input_avail_cbk(in_port, on_input_avail_cbk, input_avail_data);

    if (aml_out->hw_sync_mode) {
        in_port->is_hwsync = true;
    }
    in_port->initial_frames = aml_out->frame_write_sum;
    return 0;
}

int delete_mixer_input_port(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    R_CHECK_PARAM_LEGAL(-EINVAL, port_index, 0, NR_INPORTS - 1, "");
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);

    AM_LOGI("input port ID:%d, type:%s, cur mask:%#x", port_index,
        mixerInputType2Str(in_port->enInPortType), audio_mixer->inportsMasks);
    pthread_mutex_lock(&audio_mixer->lock);
    pthread_mutex_lock(&audio_mixer->inport_lock);
    free_input_port(in_port);
    audio_mixer->in_ports[port_index] = NULL;
    audio_mixer->inportsMasks &= ~(1 << port_index);
    audio_mixer->inportsAvailMasks |= 1 << port_index;
    pthread_mutex_unlock(&audio_mixer->inport_lock);
    pthread_mutex_unlock(&audio_mixer->lock);
    return 0;
}

int init_mixer_multi_aaudio_input_port(struct amlAudioMixer *audio_mixer,
        struct audio_config *config)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");
    R_CHECK_POINTER_LEGAL(-EINVAL, config, "");

    input_port *in_port = NULL;
    uint8_t port_index = -1;

    struct audioCfg portConfig;
    setPortConfig(&portConfig, config);
    in_port = new_input_port(MIXER_FRAME_COUNT, &portConfig, 0, 1.0f, false, true);
    if (in_port == NULL) {
        AM_LOGE("new_input_port is NULL");
        return -1;
    }
    port_index = mixer_get_available_inport_index(audio_mixer);
    /*coverity[leaked_storage]*/
    R_CHECK_PARAM_LEGAL(-1, port_index, 0, NR_INPORTS - 1, "");

    if (audio_mixer->in_ports[port_index] != NULL) {
        AM_LOGW("inport index:[%d]%s already exists! recreate", port_index, mixerInputType2Str(port_index));
        free_input_port(audio_mixer->in_ports[port_index]);
    }

    in_port->ID = port_index;
    AM_LOGI("input port:%s, size %d frames",
        mixerInputType2Str(in_port->enInPortType), MIXER_FRAME_COUNT);
    audio_mixer->in_ports[port_index] = in_port;
    audio_mixer->inportsMasks |= 1 << port_index;
    audio_mixer->multi_aaudio_port_index = port_index;
    return 0;
}


int send_mixer_inport_message(struct amlAudioMixer *audio_mixer, uint8_t port_index, PORT_MSG msg)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return send_inport_message(in_port, msg);
}

int send_mixer_outport_message(struct amlAudioMixer *audio_mixer, uint8_t port_index,
        PORT_MSG msg, void *info, int info_len)
{
    output_port *out_port = audio_mixer->out_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, out_port, "port_index:%d", port_index);
    return send_outport_message(out_port, msg, info, info_len);
}

void set_mixer_hwsync_frame_size(struct amlAudioMixer *audio_mixer, uint32_t frame_size)
{
    AM_LOGI("framesize %d", frame_size);
    audio_mixer->hwsync_frame_size = frame_size;
}

uint32_t get_mixer_hwsync_frame_size(struct amlAudioMixer *audio_mixer)
{
    return audio_mixer->hwsync_frame_size;
}

uint32_t get_mixer_inport_consumed_frames(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return get_inport_consumed_size(in_port) / in_port->cfg.frame_size;
}

int set_mixer_inport_volume(struct amlAudioMixer *audio_mixer, uint8_t port_index, float vol)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    if (vol > 1.0 || vol < 0) {
        AM_LOGE("invalid vol %f", vol);
        return -EINVAL;
    }
    set_inport_volume(in_port, vol);
    return 0;
}

float get_mixer_inport_volume(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return get_inport_volume(in_port);
}

int mixer_write_inport(struct amlAudioMixer *audio_mixer, uint8_t port_index, const void *buffer, int bytes)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    int         written = 0;

    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    written = in_port->write(in_port, buffer, bytes);
    if (get_inport_state(in_port) != ACTIVE) {
        AM_LOGI("input port:%s is active now", mixerInputType2Str(in_port->enInPortType));
        set_inport_state(in_port, ACTIVE);
    }
    AM_LOGV("portIndex %d", port_index);
    return written;
}

int mixer_read_inport(struct amlAudioMixer *audio_mixer, uint8_t port_index, void *buffer, int bytes)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return in_port->read(in_port, buffer, bytes);
}

int mixer_set_inport_state(struct amlAudioMixer *audio_mixer, uint8_t port_index, port_state state)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return set_inport_state(in_port, state);
}

int mixer_set_inport_start_threshold(struct amlAudioMixer *audio_mixer, uint8_t port_index, int start_threshold)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return set_inport_start_threshold(in_port, start_threshold);
}

port_state mixer_get_inport_state(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return get_inport_state(in_port);
}
//TODO: handle message queue
static void mixer_procs_msg_queue(struct amlAudioMixer *audio_mixer __unused)
{
    AM_LOGV("start");
    return;
}

static inline MIXER_OUTPUT_PORT mixer_get_cur_outport(struct amlAudioMixer *audio_mixer, output_port **out_port)
{
    MIXER_OUTPUT_PORT port_index = audio_mixer->cur_output_port_type;
    *out_port = NULL;
    R_CHECK_PARAM_LEGAL(-1, port_index, MIXER_OUTPUT_PORT_STEREO_PCM, MIXER_OUTPUT_PORT_NUM - 1, "");
    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    *out_port = audio_mixer->out_ports[port_index];
    if (*out_port == NULL) {
        AM_LOGW("out_port is null");
        pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
        return MIXER_OUTPUT_PORT_INVAL;
    }
    /*coverity[missing_unlock]*/
    return port_index;
}

size_t get_outport_data_avail(output_port *outport)
{
    return outport->bytes_avail;
}

int set_outport_data_avail(output_port *outport, size_t avail)
{
    if (avail > outport->data_buf_len) {
        AM_LOGE("invalid avail %zu", avail);
        return -EINVAL;
    }
    outport->bytes_avail = avail;
    return 0;
}

int init_mixer_output_port(struct amlAudioMixer *audio_mixer,
        MIXER_OUTPUT_PORT output_type,
        struct audioCfg *src_config, /*mixer output format*/
        struct audioCfg *config, /*target config*/
        size_t buf_frames)
{
    R_CHECK_PARAM_LEGAL(-1, output_type, MIXER_OUTPUT_PORT_STEREO_PCM, MIXER_OUTPUT_PORT_NUM - 1, "");
    struct aml_audio_device     *adev = audio_mixer->adev;

    pthread_mutex_lock(&audio_mixer->outport_locks[output_type]);
    AM_LOGI("output port:%s", mixerOutputType2Str(output_type));
    output_port *out_port = new_output_port(output_type, src_config, config, buf_frames);
    if (out_port == NULL) {
        AM_LOGW("new_output_port fail");
        pthread_mutex_unlock(&audio_mixer->outport_locks[output_type]);
        return -1;
    }
    audio_mixer->cur_output_port_type = output_type;
    out_port->audio_mixer = audio_mixer;

    //set_port_notify_cbk(port, on_notify_cbk, notify_data);
    //set_port_input_avail_cbk(port, on_input_avail_cbk, input_avail_data);
    audio_mixer->out_ports[output_type] = out_port;
    aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_FORMAT, AML_STEREO_PCM);

#ifdef ENABLE_AEC_APP
    out_port->aec = audio_mixer->adev->aec;
    struct pcm_config alsa_config;
    memset(&alsa_config, 0, sizeof(struct pcm_config));
    output_get_alsa_config(out_port, &alsa_config);
    int aec_ret = init_aec_reference_config(out_port->aec, alsa_config);
    NO_R_CHECK_RET(aec_ret, "AEC: Speaker config init failed!");
#endif
    pthread_mutex_unlock(&audio_mixer->outport_locks[output_type]);
    return 0;
}

int delete_mixer_output_port(struct amlAudioMixer *audio_mixer, MIXER_OUTPUT_PORT port_index)
{
    R_CHECK_PARAM_LEGAL(-1, port_index, MIXER_OUTPUT_PORT_STEREO_PCM, MIXER_OUTPUT_PORT_NUM - 1, "");
    struct aml_audio_device     *adev = audio_mixer->adev;
#ifdef ENABLE_AEC_APP
    destroy_aec_reference_config(adev->aec);
#endif
    AM_LOGI("output port:%s", mixerOutputType2Str(port_index));
    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    audio_mixer->cur_output_port_type = MIXER_OUTPUT_PORT_INVAL;
    output_port *out_port = audio_mixer->out_ports[port_index];
    if (out_port == NULL) {
        AM_LOGW("out_port is null");
        pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
        return -1;
    }
    free_output_port(out_port);
    audio_mixer->out_ports[port_index] = NULL;
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_SPDIF_FORMAT, AML_STEREO_PCM);
    return 0;
}

static int mixer_output_startup(struct amlAudioMixer *audio_mixer)
{
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    AM_LOGI("output port:%s", mixerOutputType2Str(port_index));
    out_port->start(out_port);
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    audio_mixer->submix_standby = 0;

    return 0;
}

int mixer_output_standby(struct amlAudioMixer *audio_mixer)
{
    ALOGI("[%s:%d] request sleep thread", __func__, __LINE__);
    int timeoutMs = 200;
    (void)audio_mixer;
    //audio_mixer->run_state = AML_AUDIO_MIXER_RUN_STATE_REQ_SLEEP;
    return 0;
}

static int mixer_thread_sleep(struct amlAudioMixer *audio_mixer)
{
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    AM_LOGI("output port:%s", mixerOutputType2Str(port_index));
    if (false == audio_mixer->submix_standby) {
        ALOGI("[%s:%d] start going to standby", __func__, __LINE__);
        out_port->standby(out_port);
        audio_mixer->submix_standby = true;
    }
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    return 0;
}

int mixer_output_dummy(struct amlAudioMixer *audio_mixer, bool en)
{
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");

    AM_LOGI("output port:%s, en:%d", mixerOutputType2Str(port_index), en);
    outport_set_dummy(out_port, en);
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    return 0;
}

static int mixer_output_write(struct amlAudioMixer *audio_mixer)
{
    audio_config_base_t in_data_config = {48000, AUDIO_CHANNEL_OUT_STEREO, AUDIO_FORMAT_PCM_16_BIT};
    ssize_t ret = 0;
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    struct aml_audio_device *adev = audio_mixer->adev;
    uint32_t masks = 0;
    input_port *in_port = NULL;
    struct snd_pcm_status status;
    bool alsa_status = true;
    struct aml_stream_out *aml_out = NULL;
    output_port *mc_out_port = NULL;
    struct timespec alsa_delay_ts = {0};
    uint32_t alsa_delay_ms = 0;
    audio_format_t mixing_out_format = audio_mixer->cfg.format;
    uint32_t mixing_out_rate = audio_mixer->cfg.sampleRate;
    aml_pcm_mixing_st  *p_cur_mixer = NULL;

    if (audio_mixer->type == SUB_MIXER_NORMAL) {
        p_cur_mixer = &audio_mixer->stereo_mixer;
    } else if (audio_mixer->type == SUB_MIXER_CH_MUX) {
        p_cur_mixer =  &audio_mixer->ch_mux_mixer;
    }

    void *mixed_out_buffer = p_cur_mixer->mixed_buf;
    int mixed_out_bytes = p_cur_mixer->mixed_out_bytes;

    while (out_port->bytes_avail > 0) {
        // out_write_callbacks();
        if (is_include_sco_out_port(adev->cur_out_devices)) {
            if (out_port->cfg.channelCnt == 1) {
                in_data_config.channel_mask = AUDIO_CHANNEL_OUT_MONO;
            } else if (out_port->cfg.channelCnt == 2) {
                in_data_config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
            } else {
                AM_LOGW("not supported channel:%d", out_port->cfg.channelCnt);
                pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
                return out_port->bytes_avail;
            }
            in_data_config.sample_rate = mixing_out_rate;
            in_data_config.format = mixing_out_format;
            if (is_TV(adev))
                apply_volume(adev->sink_gain[OUTPORT_BT_SCO], mixed_out_buffer, sizeof(uint16_t),
                    mixed_out_bytes);
            ret = write_to_sco(adev, &in_data_config, mixed_out_buffer, mixed_out_bytes);
            if (ret < 0) {
                ALOGE("%s write_to_sco fail when insert", __func__);
                break;
           }
        } else {
            if (is_include_a2dp_out_port(adev->cur_out_devices) || is_include_usb_out_port(adev->cur_out_devices)) {
                void *proc_buf = NULL;
                size_t proc_bytes = 0;
                int sample_size = audio_bytes_per_sample(mixing_out_format);
                if (out_port->cfg.channelCnt == 1) {
                    in_data_config.channel_mask = AUDIO_CHANNEL_OUT_MONO;
                } else if (out_port->cfg.channelCnt == 2) {
                    in_data_config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                } else {
                    AM_LOGW("not supported channel:%d", out_port->cfg.channelCnt);
                    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
                    return out_port->bytes_avail;
                }
                in_data_config.sample_rate = mixing_out_rate;
                in_data_config.format = mixing_out_format;

                if (mixing_out_format == AUDIO_FORMAT_PCM_16_BIT) {
                    ret = aml_audio_check_and_realloc((void **)&adev->out_16_buf, &adev->out_16_buf_size, out_port->bytes_avail);
                    R_CHECK_RET((int)ret, "alloc out_16_buf size:%zu fail", out_port->bytes_avail);
                    proc_buf = adev->out_16_buf;
                    proc_bytes = out_port->bytes_avail;
                } else {
                    ret = aml_audio_check_and_realloc((void **)&adev->out_32_buf, &adev->out_32_buf_size, out_port->bytes_avail);
                    R_CHECK_RET((int)ret, "alloc out_16_buf size:%zu fail", out_port->bytes_avail);
                    proc_buf = adev->out_32_buf;
                    proc_bytes = out_port->bytes_avail;
                }
                memcpy(proc_buf, mixed_out_buffer, mixed_out_bytes);

                float volume = aml_audio_get_s_gain_by_src(adev, get_dev_patch_src(adev));
                if (is_TV(adev) || adev->enable_soundbar_mode) {
                    float sink_gain = adev->sink_gain[is_include_a2dp_out_port(adev->cur_out_devices) ? OUTPORT_A2DP : OUTPORT_USB_HEADSET];
                    volume *= sink_gain;
                }
                apply_volume(volume, proc_buf, sample_size, proc_bytes);
                if (is_include_a2dp_out_port(adev->cur_out_devices)) {
                    a2dp_out_write(adev, &in_data_config, proc_buf, proc_bytes);
                } else {
                    usb_check_write(adev, proc_buf, proc_bytes, &in_data_config);
                }
            }

            if (audio_mixer->submix_standby) {
                pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
                mixer_output_startup(audio_mixer);
                pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
            }
            if (out_port->pcm_handle == NULL) {
                alsa_status = false;
            } else {
                pcm_ioctl(out_port->pcm_handle, SNDRV_PCM_IOCTL_STATUS, &status);
                alsa_status = (status.state == PCM_STATE_RUNNING);
            }

            if (mixed_out_bytes > 0) {
                out_port->write(out_port, mixed_out_buffer, mixed_out_bytes);
            }
        }

        //clear mixing output data size
        p_cur_mixer->mixed_out_bytes = 0;
        set_outport_data_avail(out_port, 0);
    };
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    // output multi-ch pcm
    port_index = MIXER_OUTPUT_PORT_MULTI_PCM;
    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    mc_out_port = audio_mixer->out_ports[port_index];
    if (mc_out_port && mc_out_port->bytes_avail > 0) {
        mc_out_port->write(mc_out_port, mc_out_port->data_buf, mc_out_port->bytes_avail);
        mc_out_port->bytes_avail = 0;
        // multi-ch-pcm(its functionality like ddp51) has higher priority than stereo pcm.
        alsa_status = aml_audio_spdifout_get_status(mc_out_port->spdifout_handle);
        alsa_delay_ms = mc_out_port->alsa_delay_ms;
        alsa_delay_ts = mc_out_port->alsa_delay_ts;
    }
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    // update timestamp info
    pthread_mutex_lock(&audio_mixer->outport_delay_locks[port_index]);
    /*coverity[use]*/
    audio_mixer->outport_delay_ms[port_index] = alsa_delay_ms;
    /*coverity[use]*/
    audio_mixer->outport_delay_ts[port_index] = alsa_delay_ts;
    pthread_mutex_unlock(&audio_mixer->outport_delay_locks[port_index]);

    // update audio stream running status
    masks = audio_mixer->inportsMasks;
    while (masks) {
        in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (in_port == NULL) {
            continue;
        }
        aml_out = (struct aml_stream_out *)in_port->notify_cbk_data;
        if ((aml_out == NULL) || aml_out->standby || in_port->first_read) {
            continue;
        }
        if (alsa_status != aml_out->alsa_running_status) {
            ALOGI("%s alsa_running_status[%p] change from %d to %d", __func__, aml_out, aml_out->alsa_running_status, alsa_status);
            aml_out->alsa_running_status = alsa_status;
            aml_out->alsa_status_changed = true;
        }
    }

    return 0;
}

int init_ch_mux_mixer_buffer(struct amlAudioMixer *audio_mixer, struct audioCfg *p_mixer_cfg, int mixed_frames)
{
    R_CHECK_POINTER_LEGAL(-1, audio_mixer, "");
    R_CHECK_POINTER_LEGAL(-1, p_mixer_cfg, "");

    if (init_aml_pcm_mixer(&audio_mixer->ch_mux_mixer, p_mixer_cfg, mixed_frames) != 0) {
        return -1;
    }
    return 0;
}

void deinit_ch_mux_mixer_buffer(struct amlAudioMixer *audio_mixer)
{
    if (audio_mixer == NULL) {
        AM_LOGV("audio_mixer = NULL");
        return;
    }
    deinit_aml_pcm_mixer(&audio_mixer->ch_mux_mixer);
}

int init_stereo_mixer_buffer(struct amlAudioMixer *audio_mixer, struct audioCfg *p_mixer_cfg, int mixed_frames)
{
    R_CHECK_POINTER_LEGAL(-1, audio_mixer, "");
    R_CHECK_POINTER_LEGAL(-1, p_mixer_cfg, "");

    if (init_aml_pcm_mixer(&audio_mixer->stereo_mixer, p_mixer_cfg, mixed_frames) != 0) {
        return -1;
    }
    init_aml_pcm_downmix(&audio_mixer->pcm_downmix);
    return 0;
}

void deinit_stereo_mixer_buffer(struct amlAudioMixer *audio_mixer)
{
    if (audio_mixer == NULL) {
        AM_LOGV("audio_mixer = NULL");
        return;
    }
    deinit_aml_pcm_mixer(&audio_mixer->stereo_mixer);
    deinit_aml_pcm_downmix(&audio_mixer->pcm_downmix);
}

int init_multich_mixer_buffer(struct amlAudioMixer *audio_mixer, struct audioCfg *p_mixer_cfg, int mixed_frames)
{
    R_CHECK_POINTER_LEGAL(-1, audio_mixer, "");
    R_CHECK_POINTER_LEGAL(-1, p_mixer_cfg, "");

    if (init_aml_pcm_mixer(&audio_mixer->multich_mixer, p_mixer_cfg, mixed_frames) != 0) {
        return -1;
    }
    return 0;
}

void deinit_multich_mixer_buffer(struct amlAudioMixer *audio_mixer)
{
    if (audio_mixer == NULL) {
        AM_LOGV("audio_mixer = NULL");
        return;
    }
    deinit_aml_pcm_mixer(&audio_mixer->multich_mixer);
}

#define DEFAULT_KERNEL_FRAMES (DEFAULT_PLAYBACK_PERIOD_SIZE*DEFAULT_PLAYBACK_PERIOD_CNT)

static int mixer_update_tstamp(struct amlAudioMixer *audio_mixer)
{
    output_port *out_port = NULL;
    input_port *in_port = NULL;
    unsigned int avail = 0;
    uint32_t masks = audio_mixer->inportsMasks;

    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    if (out_port->pcm_handle == NULL) {
        AM_LOGV("pcm handle is null");
        pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
        return 0;
    }

    while (masks) {
        in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (in_port == NULL) {
            AM_LOGE("in_port:%p is null", in_port);
            continue;
        }

        struct aml_audio_device *adev = audio_mixer->adev;
        if (adev->cur_out_devices & AUDIO_DEVICE_OUT_ALL_A2DP) {
            uint64_t a2dp_latency_frames = a2dp_out_get_latency(adev) * in_port->cfg.sampleRate / MSEC_PER_SEC;
            if (in_port->mix_consumed_frames + in_port->initial_frames > a2dp_latency_frames) {
                in_port->presentation_frames = in_port->mix_consumed_frames + in_port->initial_frames - a2dp_latency_frames;
            } else {
                in_port->presentation_frames = 0;
            }
            clock_gettime(CLOCK_MONOTONIC, &in_port->timestamp);
            continue;
        }

        if (audio_mixer->mc_out_enable && audio_mixer->mc_out_status != STOPPED) {
            int64_t alsa_latency_frames = mixer_get_mc_outport_latency_frames(audio_mixer);
            int64_t signed_frames = in_port->mix_consumed_frames - alsa_latency_frames;
            if (signed_frames < 0) {
                in_port->s64_negative_frames = signed_frames;
                signed_frames = 0;
            } else {
                in_port->s64_negative_frames = 0;
            }
            in_port->presentation_frames = in_port->initial_frames + signed_frames;
            clock_gettime(CLOCK_MONOTONIC, &in_port->timestamp);

            if (adev->debug_flag && (audio_mixer->run_count % 4 == 0)) {
                AM_LOGI("type %d, present frames:%" PRId64 ", initial %" PRId64 ", consumed %" PRId64 ", sec:%ld, nanosec:%ld , alsa_latency_frames:%" PRId64 "",
                    in_port->enInPortType,
                    in_port->presentation_frames,
                    in_port->initial_frames,
                    in_port->mix_consumed_frames,
                    in_port->timestamp.tv_sec,
                    in_port->timestamp.tv_nsec,
                    alsa_latency_frames);
            }
            continue;
        }

        if (pcm_get_htimestamp(out_port->pcm_handle, &avail, &in_port->timestamp) == 0) {
            size_t kernel_buf_size = DEFAULT_KERNEL_FRAMES;
            int64_t signed_frames = in_port->mix_consumed_frames - kernel_buf_size + avail;
            if (signed_frames < 0) {
                in_port->s64_negative_frames = signed_frames;
                signed_frames = 0;
            } else {
                in_port->s64_negative_frames = 0;
            }
            in_port->presentation_frames = in_port->initial_frames + signed_frames;
            AM_LOGV("present frames:%" PRId64 ", initial %" PRId64 ", consumed %" PRId64 ", sec:%ld, nanosec:%ld",
                    in_port->presentation_frames,
                    in_port->initial_frames,
                    in_port->mix_consumed_frames,
                    in_port->timestamp.tv_sec,
                    in_port->timestamp.tv_nsec);
        }
    }

    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    return 0;
}

inline float get_fade_step_by_size(int fade_size, int frame_size)
{
    return 1.0/(fade_size/frame_size);
}

int init_fade(struct fade_out *fade_out, int fade_size,
        int sample_size, int channel_cnt)
{
    fade_out->vol = 1.0;
    fade_out->target_vol = 0;
    fade_out->fade_size = fade_size;
    fade_out->sample_size = sample_size;
    fade_out->channel_cnt = channel_cnt;
    fade_out->stride = get_fade_step_by_size(fade_size, sample_size * channel_cnt);
    AM_LOGI("size %d, stride %f", fade_size, fade_out->stride);
    return 0;
}

int process_fade_out(void *buf, int bytes, struct fade_out *fout)
{
    int i = 0;
    int frame_cnt = bytes / fout->sample_size / fout->channel_cnt;
    int16_t *sample = (int16_t *)buf;

    if (fout->channel_cnt != 2 || fout->sample_size != 2)
        AM_LOGE("not support yet");
    AM_LOGI("++++fade out vol %f, size %d", fout->vol, fout->fade_size);
    for (i = 0; i < frame_cnt; i++) {
        sample[i] = sample[i]*fout->vol;
        sample[i+1] = sample[i+1]*fout->vol;
        fout->vol -= fout->stride;
        if (fout->vol < 0)
            fout->vol = 0;
    }
    fout->fade_size -= bytes;
    AM_LOGI("----fade out vol %f, size %d", fout->vol, fout->fade_size);

    return 0;
}


static void set_inport_next_speed(input_port *in_port, float next_speed, int delay_frames)
{
    port_speed_info *p_speed = &in_port->speed;
    if (is_float_equal(next_speed, p_speed->curr_speed) || is_float_equal(next_speed, p_speed->next_speed)) {
        return;
    }
    p_speed->next_speed = next_speed;
    p_speed->next_speed_position = p_speed->input_frame_sum + delay_frames;
    p_speed->speed_changed = true;
}

static int update_inport_speed_info(input_port *in_port, size_t data_frame_cnt)
{
    port_speed_info *p_speed = &in_port->speed;
    int64_t part1_frames = 0;
    int64_t part2_frames = 0;
    uint64_t output_frames = data_frame_cnt;
    uint64_t no_speed_frames = 0;

    p_speed->input_frame_sum += data_frame_cnt;
    if (is_float_equal(p_speed->curr_speed, 0) || is_float_equal(p_speed->next_speed, 0)) {
        AM_LOGI("invalid speed : curr %f, next %f", p_speed->curr_speed, p_speed->next_speed);
        p_speed->output_frames = data_frame_cnt;
        return -1;
    }

    if (p_speed->speed_changed && p_speed->input_frame_sum >= p_speed->next_speed_position) {
        part1_frames = p_speed->input_frame_sum - p_speed->next_speed_position;
        part1_frames = part1_frames >= 0 ? part1_frames : 0;
        part2_frames = data_frame_cnt - part1_frames;
        part2_frames = part2_frames >= 0 ? part2_frames : 0;

        output_frames = part1_frames * p_speed->next_speed + part2_frames * p_speed->curr_speed;
        p_speed->curr_speed = p_speed->next_speed;
        p_speed->curr_speed_frame = part1_frames;
        p_speed->output_frame_sum = part1_frames * p_speed->next_speed;
        p_speed->speed_changed = false;
    } else {
        if (!is_float_equal(p_speed->curr_speed, 1.0f)) {
            p_speed->curr_speed_frame += data_frame_cnt;
            no_speed_frames = p_speed->curr_speed_frame * p_speed->curr_speed;
            output_frames = no_speed_frames - p_speed->output_frame_sum;
            p_speed->output_frame_sum += output_frames;
        }
    }
    p_speed->output_frames = output_frames;

    return 0;
}


static int update_inport_avail(input_port *in_port, size_t data_frame_cnt)
{
    size_t no_speed_frames = 0;

    // first throw away the padding frames
    if (in_port->padding_frames > 0) {
        if (in_port->padding_frames > data_frame_cnt) {
            in_port->padding_frames -= data_frame_cnt;
        } else {
            in_port->padding_frames = 0;
        }
        set_inport_pts_valid(in_port, false);
    } else {
        update_inport_speed_info(in_port, data_frame_cnt);
        no_speed_frames = in_port->speed.output_frames;

        in_port->mix_consumed_frames += no_speed_frames;
        set_inport_pts_valid(in_port, true);
    }
    in_port->data_valid = 1;
    return 0;
}

static void process_port_msg(input_port *in_port)
{
    port_message *msg = get_inport_message(in_port);
    if (msg) {
        AM_LOGI("msg: %s", port_msg_to_str(msg->msg_what));
        switch (msg->msg_what) {
        case MSG_PAUSE: {
            struct aml_stream_out *out = (struct aml_stream_out *)in_port->notify_cbk_data;
            audio_hwsync_t *hwsync = (out != NULL) ? (out->hwsync) : NULL;
            AM_LOGI("[%s:%d] hwsync:%p tsync pause", __func__, __LINE__, hwsync);
            if (hwsync != NULL) {
                aml_hwsync_wrap_set_pause(hwsync);
                // prepare for the next wait_video_drop function
                // aml_hwsync_wrap_wait_video_drop will return if it is vmaster mode.
                hwsync->wait_video_done = false;
                if (out->restore_vmaster) {
                    aml_hwsync_wrap_set_amaster(hwsync, false);
                    out->restore_vmaster = false;
                }

            }
            set_inport_state(in_port, PAUSING);
            break;
        }
        case MSG_FLUSH:
            set_inport_state(in_port, FLUSHING);
            break;
        case MSG_RESUME: {
            struct aml_stream_out *out = (struct aml_stream_out *)in_port->notify_cbk_data;
            audio_hwsync_t *hwsync = (out != NULL) ? (out->hwsync) : NULL;
            //AM_LOGI("[%s:%d] hwsync:%p tsync resume", hwsync);
            if ((hwsync != NULL) && (hwsync->use_mediasync)) {
                hwsync->hwsync_need_resume = true;
            }
            set_inport_state(in_port, RESUMING);
            break;
        }
        default:
            AM_LOGE("not support");
        }

        remove_inport_message(in_port, msg);
    }
}

int mixer_flush_inport(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return reset_input_port(in_port);
}

static int mixer_inports_read(struct amlAudioMixer *audio_mixer)
{
    unsigned int masks = audio_mixer->inportsMasks;
    AM_LOGV("+++");
    while (masks) {
        input_port *in_port = NULL;
        in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (NULL == in_port) {
            continue;
        }
        int ret = 0, fade_out = 0, fade_in = 0;
        aml_mixer_input_port_type_e type = in_port->enInPortType;
        process_port_msg(in_port);
        port_state state = get_inport_state(in_port);

        if (type == AML_MIXER_INPUT_PORT_PCM_DIRECT) {
            //if in pausing states, don't retrieve data
            if (state == PAUSING) {
                fade_out = 1;
            } else if (state == RESUMING) {
                struct aml_stream_out *out = (struct aml_stream_out *)in_port->notify_cbk_data;
                audio_hwsync_t *hwsync = (out != NULL) ? (out->hwsync) : NULL;
                fade_in = 1;
                AM_LOGI("input port:%s tsync resume", mixerInputType2Str(type));
                if (hwsync)
                    hwsync->hwsync_need_resume = true;
                set_inport_state(in_port, ACTIVE);
            } else if (state == STOPPED || state == PAUSED || state == FLUSHED) {
                AM_LOGV("input port:%s stopped, paused or flushed", mixerInputType2Str(type));
                continue;
            } else if (state == FLUSHING) {
                mixer_flush_inport(audio_mixer, in_port->ID);
                AM_LOGI("input port:%s flushing->flushed", mixerInputType2Str(type));
                set_inport_state(in_port, FLUSHED);
                continue;
            }
            if (get_inport_state(in_port) == ACTIVE && in_port->data_valid) {
                AM_LOGI("input port:%s data already valid", mixerInputType2Str(type));
                continue;
            }
        } else {
            if (in_port->data_valid) {
                AM_LOGI("input port ID:%d port:%s, data already valid", in_port->ID, mixerInputType2Str(type));
                continue;
            }
        }

        int consume_frame_cnt = in_port->data_buf_frame_cnt;
        int input_avail_size = in_port->rbuf_avail(in_port);
        AM_LOGV("input port:%s, portId:%d, avail:%d, masks:%#x, inportsMasks:%#x, data_len_bytes:%zu",
            mixerInputType2Str(type), in_port->ID, input_avail_size, masks, audio_mixer->inportsMasks, in_port->data_len_bytes);
        if (input_avail_size >= in_port->data_len_bytes) {
            if (in_port->first_read) {
                if (input_avail_size < in_port->inport_start_threshold) {
                    continue;
                } else {
                    AM_LOGI("input port:%s first start, portId:%d, avail:%d",
                        mixerInputType2Str(type), in_port->ID, input_avail_size);
                    in_port->first_read = false;
                }
            }
            ret = mixer_read_inport(audio_mixer, in_port->ID, in_port->data, in_port->data_len_bytes);
            if (ret == (int)in_port->data_len_bytes) {
                struct aml_stream_out *out = (struct aml_stream_out *)in_port->notify_cbk_data;
                if (fade_out) {
                    int drop_bytes = 0;
                    audio_hwsync_t *hwsync = (out != NULL) ? (out->hwsync) : NULL;
                    struct aml_audio_device *adev = (out != NULL) ? (out->dev) : NULL;
                    AM_LOGI("output port:%s fade out, pausing->pausing_1, tsync pause audio", mixerInputType2Str(type));
                    aml_hwsync_wrap_set_pause(hwsync);
                    if (out) {
                        if (in_port->cfg.format == AUDIO_FORMAT_PCM_32_BIT) {
                            audio_fade_func_32bit(in_port->data, ret, 0, in_port->cfg.channelCnt);
                        } else {
                            audio_fade_func_16bit(in_port->data, ret, 0, in_port->cfg.channelCnt);
                        }
                    }
                    set_inport_state(in_port, PAUSED);
                    /* Mute the last data to prevent gap. */
                    drop_bytes = get_buffer_read_space(in_port->r_buf);
                    ring_buffer_clear(in_port->r_buf);
                    if (adev && adev->is_netflix) {
                        // prepare for the next writing.
                        in_port->first_read = true;
                        aml_audio_data_handle_init((struct audio_stream_out *)out);
                    }
                    if (drop_bytes > 0 && in_port->cfg.frame_size > 0) {
                        // mix_consumed_frames should contain drop_frames
                        consume_frame_cnt += (drop_bytes/in_port->cfg.frame_size);
                        AM_LOGI("drop_frames %d", drop_bytes/in_port->cfg.frame_size);
                    }
                } else if (fade_in) {
                    AM_LOGI("input port:%s fade in", mixerInputType2Str(type));
                    if (out) {
                        if (in_port->cfg.format == AUDIO_FORMAT_PCM_32_BIT) {
                            audio_fade_func_32bit(in_port->data, ret, 1, in_port->cfg.channelCnt);
                        } else {
                            audio_fade_func_16bit(in_port->data, ret, 1, in_port->cfg.channelCnt);
                        }
                    }
                    set_inport_state(in_port, ACTIVE);
                }
                update_inport_avail(in_port, consume_frame_cnt);
                if (get_audiomixer_dump_enable(DUMP_AUDIOMIXER_INPORT_READ) &&
                        (in_port->enInPortType == AML_MIXER_INPUT_PORT_PCM_DIRECT)) {
                        aml_dump_audio_bitstreams("/data/vendor/audiohal/inportDirectFade.raw",
                                in_port->data, in_port->data_len_bytes);
                }
            } else {
                AM_LOGW("port:%s read fail, have read:%d Byte, need %zu Byte",
                    mixerInputType2Str(type), ret, in_port->data_len_bytes);
            }
        } else {
            struct aml_audio_device     *adev = audio_mixer->adev;
            // MULTI_AAUDIO always has data when it active
            if (adev->debug_flag && type != AML_MIXER_INPUT_PORT_MULTI_AAUDIO) {
                AM_LOGD("port:%d ring buffer data is not enough", in_port->ID);
            }
        }
    }
    audio_mixer->inports_read_time_us = aml_audio_get_systime();

    return 0;
}

int mixer_need_wait_forever(struct amlAudioMixer *audio_mixer)
{
    return mixer_get_state(audio_mixer) != MIXER_INPORTS_READY;
}

static uint32_t hwsync_align_to_frame(uint32_t consumed_size, uint32_t frame_size)
{
    return consumed_size - (consumed_size % frame_size);
}

static int mixer_do_mixing_32bit(struct amlAudioMixer *audio_mixer)
{
    input_port *in_port_sys = audio_mixer->in_ports[AML_MIXER_INPUT_PORT_PCM_SYSTEM];
    input_port *in_port_drct = audio_mixer->in_ports[AML_MIXER_INPUT_PORT_PCM_DIRECT];
    output_port *out_port = NULL;
    struct aml_audio_device *adev = audio_mixer->adev;
    int16_t *data_sys, *data_drct, *data_mixed;
    int mixing = 0, sys_only = 0, direct_only = 0;
    int dirct_okay = 0, sys_okay = 0;
    float dirct_vol = 1.0, sys_vol = 1.0;
    int mixed_32 = 0;
    size_t i = 0, mixing_len_bytes = 0;
    size_t frames = 0;
    size_t frames_written = 0;
    float gain_speaker = adev->sink_gain[OUTPORT_SPEAKER];
    aml_pcm_mixing_st *p_2ch_mixer = NULL;

    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    if (!in_port_sys && !in_port_drct) {
        AM_LOGE("sys or direct pcm must exist!!!");
        return 0;
    }

    if (in_port_sys && in_port_sys->data_valid) {
        sys_okay = 1;
    }
    if (in_port_drct && in_port_drct->data_valid) {
        dirct_okay = 1;
    }
    if (sys_okay && dirct_okay) {
        mixing = 1;
    } else if (dirct_okay) {
        AM_LOGV("only direct okay");
        direct_only = 1;
    } else if (sys_okay) {
        sys_only = 1;
    } else {
        AM_LOGV("sys direct both not ready!");
        return -EINVAL;
    }

    p_2ch_mixer = &audio_mixer->stereo_mixer;
    R_CHECK_POINTER_LEGAL(-1, p_2ch_mixer->mixed_buf, "");
    if (p_2ch_mixer->mixed_buf_size < MIXER_FRAME_COUNT * out_port->cfg.frame_size) {
        ALOGE("mixed_buf_size is too small(%zu < %d)", p_2ch_mixer->mixed_buf_size, MIXER_FRAME_COUNT * out_port->cfg.frame_size);
        return -1;
    }

    data_mixed = (int16_t *)out_port->data_buf;
    memset(p_2ch_mixer->mixed_buf, 0 , MIXER_FRAME_COUNT * out_port->cfg.frame_size);
    if (mixing) {
        AM_LOGV("mixing");
        data_sys = (int16_t *)in_port_sys->data;
        data_drct = (int16_t *)in_port_drct->data;
        mixing_len_bytes = in_port_drct->data_len_bytes;
        //TODO: check if the two stream's frames are equal
        if (DEBUG_DUMP) {
            aml_dump_audio_bitstreams("/data/vendor/audiohal/audiodrct.raw",
                    in_port_drct->data, in_port_drct->data_len_bytes);
            aml_dump_audio_bitstreams("/data/vendor/audiohal/audiosyst.raw",
                    in_port_sys->data, in_port_sys->data_len_bytes);
        }
        if (in_port_drct->is_hwsync && in_port_drct->bytes_to_insert < mixing_len_bytes) {
            //retrieve_hwsync_header(audio_mixer, in_port_drct, out_port);
        }

        // insert data for direct hwsync case, only send system sound
        if (in_port_drct->bytes_to_insert >= mixing_len_bytes) {
            frames = mixing_len_bytes / in_port_drct->cfg.frame_size;
            AM_LOGD("insert mixing data, need %zu, insert length %zu",
                    in_port_drct->bytes_to_insert, mixing_len_bytes);
            //memcpy(data_mixed, data_sys, mixing_len_bytes);
            //memcpy(p_mixer->mixed_buf, data_sys, mixing_len_bytes);
            if (DEBUG_DUMP) {
                aml_dump_audio_bitstreams("/data/vendor/audiohal/systbeforemix.raw",
                        data_sys, in_port_sys->data_len_bytes);
            }
            frames_written = do_mixing_2ch(p_2ch_mixer->mixed_buf, data_sys,
                frames, in_port_sys->cfg.format, out_port->cfg.format);
            if (DEBUG_DUMP) {
                aml_dump_audio_bitstreams("/data/vendor/audiohal/sysAftermix.raw",
                        p_2ch_mixer->mixed_buf, frames * FRAMESIZE_32BIT_STEREO);
            }
            if (is_TV(adev)) {
                apply_volume(gain_speaker, p_2ch_mixer->mixed_buf,
                    sizeof(uint32_t), frames * FRAMESIZE_32BIT_STEREO);
            }

            extend_channel_2_8(data_mixed, p_2ch_mixer->mixed_buf,
                    frames, 2, 8);

            if (DEBUG_DUMP) {
                aml_dump_audio_bitstreams("/data/vendor/audiohal/dataInsertMixed.raw",
                        data_mixed, frames * out_port->cfg.frame_size);
            }
            in_port_drct->bytes_to_insert -= mixing_len_bytes;
            in_port_sys->data_valid = 0;
            set_outport_data_avail(out_port, frames * out_port->cfg.frame_size);
        } else {
            frames = mixing_len_bytes / in_port_drct->cfg.frame_size;
            frames_written = do_mixing_2ch(p_2ch_mixer->mixed_buf, data_drct,
                frames, in_port_drct->cfg.format, out_port->cfg.format);
            if (DEBUG_DUMP)
                aml_dump_audio_bitstreams("/data/vendor/audiohal/tmpMixed0.raw",
                    p_2ch_mixer->mixed_buf, frames * p_2ch_mixer->mixed_frame_size);
            frames_written = do_mixing_2ch(p_2ch_mixer->mixed_buf, data_sys,
                frames, in_port_sys->cfg.format, out_port->cfg.format);
            if (DEBUG_DUMP)
                aml_dump_audio_bitstreams("/data/vendor/audiohal/tmpMixed1.raw",
                    p_2ch_mixer->mixed_buf, frames * p_2ch_mixer->mixed_frame_size);
            if (is_TV(adev)) {
                apply_volume(gain_speaker, p_2ch_mixer->mixed_buf,
                    sizeof(uint32_t), frames * FRAMESIZE_32BIT_STEREO);
            }

            extend_channel_2_8(data_mixed, p_2ch_mixer->mixed_buf,
                    frames, 2, 8);

            in_port_drct->data_valid = 0;
            in_port_sys->data_valid = 0;
            set_outport_data_avail(out_port, frames * out_port->cfg.frame_size);
        }
        if (DEBUG_DUMP) {
            aml_dump_audio_bitstreams("/data/vendor/audiohal/data_mixed.raw",
                out_port->data_buf, frames * out_port->cfg.frame_size);
        }
    }

    if (sys_only) {
        frames = in_port_sys->data_buf_frame_cnt;
        AM_LOGV("sys_only, frames %zu", frames);
        mixing_len_bytes = in_port_sys->data_len_bytes;
        data_sys = (int16_t *)in_port_sys->data;
        if (DEBUG_DUMP) {
            aml_dump_audio_bitstreams("/data/vendor/audiohal/audiosyst.raw",
                    in_port_sys->data, mixing_len_bytes);
        }
        // processing data and make conversion according to cfg
        // processing_and_convert(data_mixed, data_sys, frames, in_port_sys->cfg, out_port->cfg);
        frames_written = do_mixing_2ch(p_2ch_mixer->mixed_buf, data_sys,
                frames, in_port_sys->cfg.format, out_port->cfg.format);
        if (DEBUG_DUMP) {
            aml_dump_audio_bitstreams("/data/vendor/audiohal/sysTmp.raw",
                    p_2ch_mixer->mixed_buf, frames * FRAMESIZE_32BIT_STEREO);
        }
        if (is_TV(adev)) {
            apply_volume(gain_speaker, p_2ch_mixer->mixed_buf,
                sizeof(uint32_t), frames * FRAMESIZE_32BIT_STEREO);
        }
        if (DEBUG_DUMP) {
            aml_dump_audio_bitstreams("/data/vendor/audiohal/sysvol.raw",
                    p_2ch_mixer->mixed_buf, frames * FRAMESIZE_32BIT_STEREO);
        }

        extend_channel_2_8(data_mixed, p_2ch_mixer->mixed_buf, frames, 2, 8);

        if (DEBUG_DUMP) {
            aml_dump_audio_bitstreams("/data/vendor/audiohal/extendsys.raw",
                    data_mixed, frames * out_port->cfg.frame_size);
        }
        in_port_sys->data_valid = 0;
        set_outport_data_avail(out_port, frames * out_port->cfg.frame_size);
    }

    if (direct_only) {
        AM_LOGV("direct_only");
        //dirct_vol = get_inport_volume(in_port_drct);
        mixing_len_bytes = in_port_drct->data_len_bytes;
        data_drct = (int16_t *)in_port_drct->data;
        AM_LOGV("direct_only, inport consumed %zu",
                get_inport_consumed_size(in_port_drct));

        if (in_port_drct->is_hwsync && in_port_drct->bytes_to_insert < mixing_len_bytes) {
            //retrieve_hwsync_header(audio_mixer, in_port_drct, out_port);
        }

        if (DEBUG_DUMP) {
            aml_dump_audio_bitstreams("/data/vendor/audiohal/audiodrct.raw",
                    in_port_drct->data, mixing_len_bytes);
        }
        // insert 0 data to delay audio
        if (in_port_drct->bytes_to_insert >= mixing_len_bytes) {
            frames = mixing_len_bytes / in_port_drct->cfg.frame_size;
            AM_LOGD("inserting direct_only, need %zu, insert length %zu",
                    in_port_drct->bytes_to_insert, mixing_len_bytes);
            memset(data_mixed, 0, mixing_len_bytes);
            extend_channel_2_8(data_mixed, p_2ch_mixer->mixed_buf,
                    frames, 2, 8);
            in_port_drct->bytes_to_insert -= mixing_len_bytes;
            set_outport_data_avail(out_port, frames * out_port->cfg.frame_size);
        } else {
            AM_LOGV("direct_only, vol %f", dirct_vol);
            frames = mixing_len_bytes / in_port_drct->cfg.frame_size;
            //cpy_16bit_data_with_gain(data_mixed, data_drct,
            //        in_port_drct->data_len_bytes, dirct_vol);
            AM_LOGV("direct_only, frames %zu, bytes %zu", frames, mixing_len_bytes);

            frames_written = do_mixing_2ch(p_2ch_mixer->mixed_buf, data_drct,
                frames, in_port_drct->cfg.format, out_port->cfg.format);
            if (DEBUG_DUMP) {
                aml_dump_audio_bitstreams("/data/vendor/audiohal/dirctTmp.raw",
                        p_2ch_mixer->mixed_buf, frames * FRAMESIZE_32BIT_STEREO);
            }
            if (is_TV(adev)) {
                apply_volume(gain_speaker, p_2ch_mixer->mixed_buf,
                    sizeof(uint32_t), frames * FRAMESIZE_32BIT_STEREO);
            }

            extend_channel_2_8(data_mixed, p_2ch_mixer->mixed_buf,
                    frames, 2, 8);

            if (DEBUG_DUMP) {
                aml_dump_audio_bitstreams("/data/vendor/audiohal/exDrct.raw",
                        data_mixed, frames * out_port->cfg.frame_size);
            }
            in_port_drct->data_valid = 0;
            set_outport_data_avail(out_port, frames * out_port->cfg.frame_size);
        }
    }

    if (0) {
        aml_dump_audio_bitstreams("/data/vendor/audiohal/data_mixed.raw",
                out_port->data_buf, mixing_len_bytes);
    }
    return 0;
}

int do_ch_mux_data_mixing(aml_pcm_mixing_st *pMixer, input_port *in_port, size_t frames)
{
    uint32_t *out_ch_tab = pMixer->main_channel_table;
    uint32_t out_ch_count = pMixer->cfg.channelCnt;
    audio_format_t out_format = pMixer->cfg.format;
    uint32_t *in_ch_tab = in_port->mux_channel_table;
    uint32_t in_channels = in_port->cfg.channelCnt;
    uint32_t in_mux_ch = in_port->mux_channels;
    audio_format_t in_format = in_port->cfg.format;
    int ret_frames;

    ret_frames = do_mixing_by_ch_mux(pMixer->mixed_buf,
                                    out_ch_tab,
                                    out_ch_count,
                                    out_format,
                                    in_port->data,
                                    in_ch_tab,
                                    in_mux_ch,
                                    in_channels,
                                    in_format,
                                    frames);
    return ret_frames;
}

static int mixer_add_mixing_data(struct amlAudioMixer *audio_mixer, void *input, input_port *in_port, output_port *out_port)
{
    char *data_ptr = NULL;
    output_port *mc_out_port = NULL;
    aml_pcm_downmix_st *p_downmix = &audio_mixer->pcm_downmix;
    aml_pcm_mixing_st  *p_2ch_mixer = &audio_mixer->stereo_mixer;
    aml_pcm_mixing_st  *p_multich_mixer = &audio_mixer->multich_mixer;
    audio_format_t mixing_2ch_out_format = p_2ch_mixer->cfg.format;

    if (in_port->data_buf_frame_cnt < MIXER_FRAME_COUNT) {
        AM_LOGE("input port type:%s buf frames:%zu too small",
            mixerInputType2Str(in_port->enInPortType), in_port->data_buf_frame_cnt);
        return -EINVAL;
    }

    if (audio_mixer->type == SUB_MIXER_NORMAL) {
        AM_LOGV("%s  in_port->cfg.channelCnt:%d in_port->cfg.format:0x%x  mixing_2ch_out_format:0x%x", __func__, in_port->cfg.channelCnt, in_port->cfg.format, mixing_2ch_out_format);
        // 2ch pcm output is always exist.
        if (in_port->cfg.channelCnt != 2) {
            do_downmix_to_2ch(p_downmix, input, MIXER_FRAME_COUNT, &in_port->cfg);
            do_mixing_2ch(p_2ch_mixer->mixed_buf, p_downmix->output_buf, MIXER_FRAME_COUNT, in_port->cfg.format, mixing_2ch_out_format);
        } else {
            do_mixing_2ch(p_2ch_mixer->mixed_buf, input, MIXER_FRAME_COUNT, in_port->cfg.format, mixing_2ch_out_format);
        }
    } else if (audio_mixer->type == SUB_MIXER_CH_MUX) {
        aml_pcm_mixing_st *p_ch_mux_mixer = &audio_mixer->ch_mux_mixer;
        do_ch_mux_data_mixing(p_ch_mux_mixer, in_port, MIXER_FRAME_COUNT);
    } else {
        AM_LOGE("unknown mixer type:%d", audio_mixer->type);
    }

    // multich pcm
    if (audio_mixer->mc_out_enable && audio_mixer->mc_out_status != STOPPED) {
        do_mixing_multi_ch(p_multich_mixer, input, MIXER_FRAME_COUNT, &in_port->cfg);
    }

    in_port->data_valid = 0;
    AM_LOGV("input port ID:%d  channels:%d", in_port->ID, out_port->cfg.channelCnt);
    return 0;
}

void mixer_enable_multich_output(struct amlAudioMixer *audio_mixer, bool enable)
{
    output_port *mc_out_port = NULL;
    const MIXER_OUTPUT_PORT port_index = MIXER_OUTPUT_PORT_MULTI_PCM;
    if (audio_mixer->mc_out_enable == enable) {
        return;
    }
    AM_LOGI("mc_out_enable %d", enable);

    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    audio_mixer->mc_out_enable = enable;
    mc_out_port = audio_mixer->out_ports[port_index];
    if (!enable && mc_out_port) {
        // close multich alsa handle, then npcm can use it.
        free_mc_output_port(mc_out_port);
        audio_mixer->out_ports[port_index] = NULL;
    }
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
}

/*
 * 2ch pcm output always exist, multi-ch pcm output is additional and dependent with sink device.
*/
static int mixer_config_multich_output(struct amlAudioMixer *audio_mixer, struct audioCfg *out_port_cfg)
{
    int ret = 0;
    uint32_t masks = 0;
    input_port *in_port = NULL;
    int sink_max_channels = 2;
    uint32_t input_channels = 2;
    uint32_t mixer_max_channels = 2;
    audio_channel_mask_t mixer_max_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    struct aml_audio_device *adev = audio_mixer->adev;
    struct audioCfg *p_mixer_cfg = &audio_mixer->multich_mixer.cfg;
    struct audioCfg mixer_cfg;
    bool input_port_empty = true;
    const MIXER_OUTPUT_PORT port_index = MIXER_OUTPUT_PORT_MULTI_PCM;
    struct aml_arc_hdmi_desc* hdmi_descs = get_arc_hdmi_cap(adev);

    sink_max_channels = hdmi_descs->pcm_fmt.max_channels;
    if (is_TV(adev)) {
        if (is_earc_connected(adev)) {
            sink_max_channels = 8;
        } else {
            sink_max_channels = 2;
        }
    }

    if (is_bypass_submix_active(adev)) {
        AM_LOGV("is_bypass_submix_active");
        return 0;
    }
    if (adev->sink_format_updating) {
        AM_LOGI("sink_format_updating, skip it");
        return 0;
    }

    masks = audio_mixer->inportsMasks;
    while (masks) {
        in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (in_port == NULL) {
            continue;
        }
        if (in_port->enInPortType == AML_MIXER_INPUT_PORT_MULTI_AAUDIO && in_port->port_status == PAUSED) {
            // mutlti aaudio port always exist, no need to check when it pause
            continue;
        }

        input_port_empty = false;
        input_channels = in_port->cfg.channelCnt;
        if (input_channels >= mixer_max_channels && input_channels <= sink_max_channels) {
            mixer_max_channels = input_channels;
            mixer_max_ch_mask  = in_port->cfg.channelMask;
        }
    }
    if (input_port_empty) {
        mixer_max_channels = 2;
        mixer_max_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    }

    /* If not connected A2DP/Headphone, and HDMI RX/ARC supports multi-channel, so we have multi-channel output.
     * Otherwise the default 2 channel output.
    */
    if (adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP
        || adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET
        || adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE
        || !audio_mixer->mc_out_enable) {
        mixer_max_channels = 2;
        mixer_max_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    } else if (adev->is_netflix) {
        if (mixer_max_channels <= 6 && sink_max_channels >= 6) {
            mixer_max_channels = 6;
            mixer_max_ch_mask = AUDIO_CHANNEL_OUT_5POINT1;
        }
    }
    if (adev->debug_flag && (audio_mixer->run_count % 10 == 0)) {
        bool ad2p_connected = adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP;
        AM_LOGI("mc_out_enable %d, A2DP_connected %d, is_netflix %d, mixer_max_channels %d, is_earc %d",
                audio_mixer->mc_out_enable, ad2p_connected, adev->is_netflix, mixer_max_channels, is_earc_connected(adev));
    }

    if ((mixer_max_channels != p_mixer_cfg->channelCnt) || (mixer_max_ch_mask != p_mixer_cfg->channelMask)) {
        memcpy(&mixer_cfg, out_port_cfg, sizeof(*out_port_cfg));
        mixer_cfg.format = AUDIO_FORMAT_PCM_16_BIT;   // raw (IEC format) output always output 16bit pcm
        mixer_cfg.channelCnt  = mixer_max_channels;
        mixer_cfg.channelMask = mixer_max_ch_mask;
        mixer_cfg.frame_size  = mixer_cfg.channelCnt * audio_bytes_per_sample(mixer_cfg.format);
        deinit_multich_mixer_buffer(audio_mixer);
        init_multich_mixer_buffer(audio_mixer, &mixer_cfg, MIXER_FRAME_COUNT);
    }

    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    output_port *mc_out_port = audio_mixer->out_ports[port_index];
    if (audio_mixer->mc_out_enable && mixer_max_channels != 2) {
        if (mc_out_port == NULL || mc_out_port->cfg.channelCnt != mixer_max_channels) {
            AM_LOGI("mc output channel change to  %d", mixer_max_channels);
            if (mc_out_port != NULL) {
                free_mc_output_port(mc_out_port);
                audio_mixer->out_ports[port_index] = NULL;
            }

            mc_out_port = new_mc_output_port(p_mixer_cfg, MIXER_FRAME_COUNT);
            if (mc_out_port == NULL) {
                AM_LOGE("new_mc_output_port failed !");
                pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
                return -1;
            }
            audio_mixer->out_ports[port_index] = mc_out_port;
            mc_out_port->start(mc_out_port);
        } else {
            if (mc_out_port->port_status != ACTIVE) {
                mc_out_port->start(mc_out_port);
            }
        }
    }else if (mc_out_port) {
        if (mc_out_port->port_status != STOPPED) {
            mc_out_port->standby(mc_out_port);
        }
        mc_out_port->port_status = STOPPED;
    }
    if (mc_out_port) {
        audio_mixer->mc_out_status = mc_out_port->port_status;
    } else {
        audio_mixer->mc_out_status = STOPPED;
    }
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    return ret;
}


static int mixer_do_mixing_16bit(struct amlAudioMixer *audio_mixer)
{
    bool                        is_data_valid = false;
    input_port                  *in_port = NULL;
    output_port                 *out_port = NULL;
    struct aml_audio_device     *adev = audio_mixer->adev;
    char                        acFilePathStr[ENUM_TYPE_STR_MAX_LEN] = {0};
    uint32_t                    need_output_ch = 2;
    audio_channel_mask_t        need_output_ch_mask = AUDIO_CHANNEL_OUT_STEREO;
    uint32_t                    masks = 0;
    aml_pcm_mixing_st           *p_ch_mux_mixer =  &audio_mixer->ch_mux_mixer;
    aml_pcm_mixing_st           *p_2ch_mixer = &audio_mixer->stereo_mixer;
    aml_pcm_mixing_st           *p_multich_mixer = &audio_mixer->multich_mixer;
    void                        *mixed_data_ptr = NULL;
    int                         mixed_data_size = 0;
    output_port                 *mc_out_port = NULL;
    struct aml_arc_hdmi_desc * hdmi_descs = get_arc_hdmi_cap(adev);

    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    mixer_config_multich_output(audio_mixer, &out_port->cfg);

    if (p_ch_mux_mixer->mixed_buf) {
         memset(p_ch_mux_mixer->mixed_buf, 0, p_ch_mux_mixer->mixed_buf_size);
    }
    if (p_2ch_mixer->mixed_buf) {
        memset(p_2ch_mixer->mixed_buf, 0, p_2ch_mixer->mixed_buf_size);
    }
    if (p_multich_mixer->mixed_buf) {
        memset(p_multich_mixer->mixed_buf, 0, p_multich_mixer->mixed_buf_size);
    }

    masks = audio_mixer->inportsMasks;
    while (masks) {
        in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (NULL == in_port || 0 == in_port->data_valid) {
            continue;
        }
        is_data_valid = true;
        if (get_audiomixer_dump_enable(DUMP_AUDIOMIXER_INDUMP)) {
            char acFilePathStr[ENUM_TYPE_STR_MAX_LEN];
            sprintf(acFilePathStr, "/data/vendor/audiohal/%s_%d.pcm", mixerInputType2Str(in_port->enInPortType), in_port->ID);
            aml_dump_audio_bitstreams(acFilePathStr, in_port->data, in_port->data_len_bytes);
        }
        if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
            check_audio_level(mixerInputType2Str(in_port->enInPortType), in_port->data, in_port->data_len_bytes);
        }
        if (AML_MIXER_INPUT_PORT_PCM_DIRECT == in_port->enInPortType) {
            if (in_port->is_hwsync && in_port->bytes_to_insert < in_port->data_len_bytes) {
                pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
                //retrieve_hwsync_header(audio_mixer, in_port, out_port);
                pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
            }
            if (in_port->bytes_to_insert >= in_port->data_len_bytes) {
                in_port->bytes_to_insert -= in_port->data_len_bytes;
                AM_LOGD("PCM_DIRECT inport insert mute data, still need %zu, inserted length %zu",
                        in_port->bytes_to_insert, in_port->data_len_bytes);
                continue;
            }
        }

        pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
        mixer_add_mixing_data(audio_mixer, in_port->data, in_port, out_port);
        pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    }
    static uint32_t no_data_cnt = 0;
    if (!is_data_valid) {
        if (adev->cur_out_devices & AUDIO_DEVICE_OUT_ALL_A2DP) {
            if (adev->debug_flag) {
                AM_LOGI("inport no valid data");
            }
            /* If all input ports timeout for 1.6s and there is no data, we stop sending
             * data to the BT stack in order to save power. (200 * 8ms = 1.6s)
             */
            if (no_data_cnt >= 200) {
                return -1;
            }
            no_data_cnt++;
        } else if (is_TV(adev) && !adev->first_data){
            no_data_cnt = 0;
            return -1;
        } else {
            no_data_cnt = 0;
        }
    } else {
        no_data_cnt = 0;
        if (!adev->first_data) {
            adev->first_data = true;
        }
    }

    pthread_mutex_lock(&audio_mixer->outport_locks[MIXER_OUTPUT_PORT_MULTI_PCM]);
    mc_out_port = audio_mixer->out_ports[MIXER_OUTPUT_PORT_MULTI_PCM];
    mixed_data_ptr  = p_multich_mixer->mixed_buf;
    mixed_data_size = p_multich_mixer->mixed_buf_size;
    if (mixed_data_ptr && mc_out_port && mc_out_port->port_status != STOPPED) {
        if (mixed_data_size > mc_out_port->data_buf_len) {
            AM_LOGE("mixed_data_size too large(%d > %zu), truncate", mixed_data_size, mc_out_port->data_buf_len);
            mixed_data_size = mc_out_port->data_buf_len;
        }
        memcpy(mc_out_port->data_buf, mixed_data_ptr, mixed_data_size);
        mc_out_port->bytes_avail = mixed_data_size;
    }
    pthread_mutex_unlock(&audio_mixer->outport_locks[MIXER_OUTPUT_PORT_MULTI_PCM]);

    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    if (p_2ch_mixer->mixed_buf) {
        mixed_data_ptr  = p_2ch_mixer->mixed_buf;
        mixed_data_size = p_2ch_mixer->mixed_buf_size;
        if (mixed_data_size > out_port->data_buf_len) {
            AM_LOGE("mixed_data_size too large(%d > %zu), truncate", mixed_data_size, out_port->data_buf_len);
            mixed_data_size = out_port->data_buf_len;
        }
        if (mixed_data_ptr == NULL || mixed_data_size <= 0) {
            AM_LOGE("p_2ch_mixer : invalid data_ptr(%p) or data_size(0x%x)", mixed_data_ptr, mixed_data_size);
        } else {
            //write mixer out to audio port, Not copy again
            //memcpy(out_port->data_buf, mixed_data_ptr, mixed_data_size);
            p_2ch_mixer->mixed_out_bytes = mixed_data_size;
        }
    } else if (p_ch_mux_mixer->mixed_buf) {
        need_output_ch = p_ch_mux_mixer->cfg.channelCnt;
        mixed_data_ptr  = p_ch_mux_mixer->mixed_buf;
        mixed_data_size = p_ch_mux_mixer->mixed_buf_size;
        if (mixed_data_size > out_port->data_buf_len) {
            AM_LOGE("mixed_data_size too large(%d > %zu), truncate", mixed_data_size, out_port->data_buf_len);
            mixed_data_size = out_port->data_buf_len;
        }
        if (mixed_data_ptr == NULL || mixed_data_size <= 0) {
            AM_LOGE("p_2ch_mixer : invalid data_ptr(%p) or data_size(0x%x)", mixed_data_ptr, mixed_data_size);
        } else {
            //write mixer out to audio port, Not copy again
            //memcpy(out_port->data_buf, mixed_data_ptr, mixed_data_size);
            p_ch_mux_mixer->mixed_out_bytes = mixed_data_size;
        }
    } else {
        mixed_data_size = 0;
        AM_LOGE("No available running mixer, mixer_data_size = 0!");
    }

    if (get_audiomixer_dump_enable(DUMP_AUDIOMIXER_OUTDUMP)) {
        sprintf(acFilePathStr, "/data/vendor/audiohal/audio_mixed_%dch.pcm", need_output_ch);
        aml_dump_audio_bitstreams(acFilePathStr, out_port->data_buf, mixed_data_size);
    }
    if (get_debug_value(AML_DEBUG_AUDIOHAL_LEVEL_DETECT)) {
        check_audio_level("audio_mixed", out_port->data_buf, mixed_data_size);
    }

    //set x_mixer->mixed_out_bytes in it's mixing process
    set_outport_data_avail(out_port, mixed_data_size);
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    return 0;
}

int notify_mixer_input_avail(struct amlAudioMixer *audio_mixer)
{
    for (uint8_t port_index = 0; port_index < NR_INPORTS; port_index++) {
        input_port *in_port = audio_mixer->in_ports[port_index];
        if (in_port && in_port->on_input_avail_cbk)
            in_port->on_input_avail_cbk(in_port->input_avail_cbk_data);
    }

    return 0;
}

int notify_mixer_exit(struct amlAudioMixer *audio_mixer)
{
    for (uint8_t port_index = 0; port_index < NR_INPORTS; port_index++) {
        input_port *in_port = audio_mixer->in_ports[port_index];
        if (in_port && in_port->on_notify_cbk)
            in_port->on_notify_cbk(in_port->notify_cbk_data);
    }

    return 0;
}

#define THROTTLE_TIME_US 3000
static void *mixer_32b_threadloop(void *data)
{
    struct amlAudioMixer *audio_mixer = data;
    int ret = 0;
    struct aml_audio_device *adev = (struct aml_audio_device *)adev_get_handle();

    AM_LOGI("++start");

    audio_mixer->exit_thread = 0;
    prctl(PR_SET_NAME, "amlAudioMixer32");
    aml_audio_set_cpu_affinity(true);
    while (!audio_mixer->exit_thread) {
        //pthread_mutex_lock(&audio_mixer->lock);
        //mixer_procs_msg_queue(audio_mixer);
        // processing throttle
        struct timespec tval_new;
        clock_gettime(CLOCK_MONOTONIC, &tval_new);
        const uint32_t delta_us = tspec_diff_to_us(audio_mixer->tval_last_write, tval_new);
        ret = mixer_inports_read(audio_mixer);
        if (ret < 0) {
            //usleep(5000);
            AM_LOGV("data not enough, next turn");
            notify_mixer_input_avail(audio_mixer);
            continue;
            //notify_mixer_input_avail(audio_mixer);
            //continue;
        }
        notify_mixer_input_avail(audio_mixer);
        AM_LOGV("do mixing");
        mixer_do_mixing_32bit(audio_mixer);
        uint64_t tpast_us = 0;
        clock_gettime(CLOCK_MONOTONIC, &tval_new);
        tpast_us = tspec_diff_to_us(audio_mixer->tval_last_write, tval_new);
        // audio patching should not in this write
        // TODO: fix me, make compatible with source output
        if (!is_dev_patch_running(audio_mixer->adev)) {
            mixer_output_write(audio_mixer);
            mixer_update_tstamp(audio_mixer);
        }
    }

    AM_LOGI("--");
    return NULL;
}

uint32_t get_mixer_inport_count(struct amlAudioMixer *audio_mixer)
{
    return __builtin_popcount(audio_mixer->inportsMasks);
}

static bool is_submix_disable(struct amlAudioMixer *audio_mixer) {
    struct aml_audio_device *adev = audio_mixer->adev;

    if (is_bypass_submix_active(adev)) {
        return true;
    }
    return false;
}

static void mixer_config_low_latency_mode(struct amlAudioMixer *audio_mixer,
                    bool low_latency, struct audio_virtual_buf **ppstVirtualBuffer)
{
    output_port *mc_out_port = NULL;
    const MIXER_OUTPUT_PORT port_index = MIXER_OUTPUT_PORT_MULTI_PCM;
    if (audio_mixer->aaudio_low_latency == low_latency) {
        return;
    }

    AM_LOGI("change aaudio_low_latency to %d", low_latency);
    audio_mixer->aaudio_low_latency = low_latency;

    // Restart alsa output port to reduce buffer level
    mixer_outport_pcm_restart(audio_mixer);

    // Standby mc_out_port, latter it will restart by mixer_config_multich_output
    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    mc_out_port = audio_mixer->out_ports[port_index];
    if (audio_mixer->mc_out_enable && mc_out_port) {
        mc_out_port->standby(mc_out_port);
    }
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    if (ppstVirtualBuffer && *ppstVirtualBuffer) {
        audio_virtual_buf_close((void **)ppstVirtualBuffer);
        *ppstVirtualBuffer = NULL;
    }
}


static void *mixer_16b_threadloop(void *data)
{
    struct amlAudioMixer        *audio_mixer = data;
    struct audio_virtual_buf    *pstVirtualBuffer = NULL;
    struct aml_audio_device *adev = (struct aml_audio_device *)adev_get_handle();
    uint64_t buffer_frame_ns = 0;

    AM_LOGI("begin create thread");
    if (audio_mixer->mixing_enable == 0) {
        pthread_exit(0);
        AM_LOGI("mixing_enable is 0 exit thread");
        return NULL;
    }
    audio_mixer->exit_thread = 0;
    audio_mixer->run_count = 0;
    audio_mixer->reset_virtual_buf = false;
    prctl(PR_SET_NAME, "amlAudioMixer16");
    aml_audio_set_cpu_affinity(true);
    aml_set_thread_sched_priority("amlAudioMixer16", audio_mixer->out_mixer_tid, AUDIO_FIFO_THREAD_DEFAULT_PRIORITY);
    while (!audio_mixer->exit_thread) {
        mixer_config_low_latency_mode(audio_mixer, adev->aaudio_low_latency, &pstVirtualBuffer);

        if (audio_mixer->reset_virtual_buf) {
            audio_virtual_buf_close((void **)&pstVirtualBuffer);
            audio_mixer->reset_virtual_buf = false;
            pstVirtualBuffer = NULL;
        }

        if (audio_mixer->submix_scheduler_state == SUBMIX_SCHEDULER_STANDBY) {
            ALOGD("%s  submix continuous start standby wait ....\n", __FUNCTION__);
            if (sem_wait(&audio_mixer->submix_standby_sem)) {
                ALOGE("%s wait submix semaphore failed\n", __FUNCTION__);
            } else {
                ALOGD("%s wait submix semaphore successful, currently wakedup.\n", __FUNCTION__);
            }
        }

        if (pstVirtualBuffer == NULL) {
            if (audio_mixer->aaudio_low_latency) {
                buffer_frame_ns = MIXER_WRITE_PERIOD_TIME_NANO * 3;
            } else {
                buffer_frame_ns = MIXER_WRITE_PERIOD_TIME_NANO * 4;
            }
            audio_virtual_buf_open((void **)&pstVirtualBuffer, "mixer_16bit_thread",
                    buffer_frame_ns, buffer_frame_ns, 0, 0);
            audio_virtual_buf_process((void *)pstVirtualBuffer, buffer_frame_ns);
        }

        pthread_mutex_lock(&audio_mixer->lock);
        mixer_inports_read(audio_mixer);
        pthread_mutex_unlock(&audio_mixer->lock);

        audio_virtual_buf_process((void *)pstVirtualBuffer, MIXER_WRITE_PERIOD_TIME_NANO);
        pthread_mutex_lock(&audio_mixer->lock);
        notify_mixer_input_avail(audio_mixer);
        mixer_do_mixing_16bit(audio_mixer);
        pthread_mutex_unlock(&audio_mixer->lock);

        if (!is_submix_disable(audio_mixer)) {
            pthread_mutex_lock(&audio_mixer->lock);
            mixer_output_write(audio_mixer);
            mixer_update_tstamp(audio_mixer);
            pthread_mutex_unlock(&audio_mixer->lock);
        }
        audio_mixer->run_count++;
        adev->debug_flag = aml_audio_get_debug_flag();
    }
    if (pstVirtualBuffer != NULL) {
        audio_virtual_buf_close((void **)&pstVirtualBuffer);
    }

    AM_LOGI("exit thread");
    return NULL;
}

uint32_t mixer_get_inport_latency_frames(struct amlAudioMixer *audio_mixer, uint8_t port_index)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return in_port->get_latency_frames(in_port);
}

/*
 * 1. mc output port may be released at any time, use outport_locks to protect.
 * 2. output stream(writer) or mixer thread(alsa writing) may call this function at the same time;
 *    but alsa writing may block a while, cause output stream also block a while.
 * So split it from output port function.
*/
int mixer_get_mc_outport_latency_frames(struct amlAudioMixer *audio_mixer)
{
    const MIXER_OUTPUT_PORT port_index = MIXER_OUTPUT_PORT_MULTI_PCM;
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");
    struct aml_audio_device *adev = audio_mixer->adev;
    R_CHECK_POINTER_LEGAL(-EINVAL, adev, "");

    struct timespec ts_start;
    struct timespec ts_end;
    int delay_ms = 0;
    int latency_frames = 0;
    int64_t system_time_diff_ms = 0;

    pthread_mutex_lock(&audio_mixer->outport_delay_locks[port_index]);
    delay_ms = audio_mixer->outport_delay_ms[port_index];
    ts_start = audio_mixer->outport_delay_ts[port_index];
    pthread_mutex_unlock(&audio_mixer->outport_delay_locks[port_index]);

    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    system_time_diff_ms = calc_time_interval_us(&ts_start, &ts_end) / MSEC_PER_SEC;

    delay_ms -= system_time_diff_ms;
    if (delay_ms < 0) {
        delay_ms = 0;
    }
    latency_frames = delay_ms * audio_mixer->multich_mixer.cfg.sampleRate / 1000;

    if (adev->debug_flag && (audio_mixer->run_count % 8 == 0)) {
        ALOGI("%s delay_ms : %d, delay_ts sec = %ld, nanosec = %ld, curr_ts sec = %ld, nanosec = %ld\n", __func__,
            audio_mixer->outport_delay_ms[port_index], ts_start.tv_sec, ts_start.tv_nsec, ts_end.tv_sec, ts_end.tv_nsec);
        ALOGI("%s diff_ms %" PRId64 ", final delay_ms %d, latency_frames %d",
            __func__, system_time_diff_ms, delay_ms, latency_frames);
    }
    return latency_frames;
}


uint32_t mixer_get_outport_latency_frames(struct amlAudioMixer *audio_mixer)
{
    MIXER_OUTPUT_PORT port_index = audio_mixer->cur_output_port_type;
    output_port *out_port = NULL;
    int latency_frames = 0;
    R_CHECK_PARAM_LEGAL(-1, port_index, MIXER_OUTPUT_PORT_STEREO_PCM, MIXER_OUTPUT_PORT_NUM - 1, "");

    // currently stereo pcm output always exist, multi pcm is optional and has higher priority.
    if (audio_mixer->mc_out_enable && audio_mixer->mc_out_status != STOPPED) {
        return mixer_get_mc_outport_latency_frames(audio_mixer);
    }
    out_port = audio_mixer->out_ports[port_index];
    if (out_port == NULL) {
        AM_LOGW("out_port is null");
        return 0;
    }
    latency_frames = outport_get_latency_frames(out_port);
    if (latency_frames <= 0) {
        latency_frames = out_port->alsa_buffer_frames/2;
    }
    return latency_frames;
}

int aml_send_submix_standby_state_2_submix(void)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    struct amlAudioMixer *audio_mixer = adev->mixerData;
    int sch_state = SUBMIX_SCHEDULER_STANDBY;
    pthread_mutex_lock(&audio_mixer->lock);
    if (audio_mixer->last_scheduler_state == SUBMIX_SCHEDULER_RUNNING || aml_get_is_exist_active_stream()) {
        ALOGI("[%s:%d] submix is running.", __FUNCTION__, __LINE__);
        pthread_mutex_unlock(&audio_mixer->lock);
        return 0;
    }
    audio_mixer->submix_scheduler_state = sch_state;
    set_submix_continuous_state(audio_mixer, audio_mixer->submix_scheduler_state);
    ALOGD("%s adev:%p, sch_state:%d(%s) ", __func__, adev, sch_state, submix_state_2_string[sch_state]);
    pthread_mutex_unlock(&audio_mixer->lock);

    return 0;
}

void submix_timer_callback_handler(union sigval sigv)
{
    ALOGD("func:%s sigv:%d ~~~~~~~~~~", __func__, sigv.sival_int);
    aml_send_submix_standby_state_2_submix();
    return ;

}

int pcm_mixer_thread_run(struct amlAudioMixer *audio_mixer)
{
    int ret = 0;
    AM_LOGI("++");
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");

    audio_format_t format = out_port->cfg.format;
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    if (audio_mixer->out_mixer_tid > 0) {
        AM_LOGE("out mixer thread already running");
        return -EINVAL;
    }
    audio_mixer->mixing_enable = 1;
    audio_mixer->submix_scheduler_state = SUBMIX_SCHEDULER_RUNNING;
    if (is_TV(audio_mixer->adev)) {
        audio_mixer->submix_scheduler_state = SUBMIX_SCHEDULER_STANDBY;
    }
    audio_mixer->last_scheduler_state = audio_mixer->submix_scheduler_state;

    {
        int ret = aml_audio_timer_create(submix_timer_callback_handler);
        if (ret < 0) {
            ALOGE("func:%s  timer_id:%d error and exit", __func__, audio_mixer->submix_timer_id);
        } else {
            audio_mixer->submix_timer_id = ret;
            ALOGI("func:%s  timer_id:%d", __func__, audio_mixer->submix_timer_id);
        }
    }

    if (sem_init(&audio_mixer->submix_standby_sem, 0, 0)) {
        ALOGE("%s init submix standby semaphore failed\n", __FUNCTION__);
    } else {
        ALOGD("%s init submix standby semaphore successful\n", __FUNCTION__);
    }
    switch (format) {
    /*coverity[unterminated_case]*/
    case AUDIO_FORMAT_PCM_32_BIT:
        //ret = pthread_create(&audio_mixer->out_mixer_tid, NULL, mixer_32b_threadloop, audio_mixer);
        //break;
        ALOGI("%s(), whatever 32bit output, mixing 16bit for 32 is for TV alsa output", __func__);
    case AUDIO_FORMAT_PCM_16_BIT:
        ret = pthread_create(&audio_mixer->out_mixer_tid, NULL, mixer_16b_threadloop, audio_mixer);
        break;
    default:
        AM_LOGE("format not supported");
        break;
    }
    if (ret < 0) {
        AM_LOGE("thread run failed.");
    }
    AM_LOGI("++mixing_enable:%d, format:%#x", audio_mixer->mixing_enable, format);

    return ret;
}

int pcm_mixer_thread_exit(struct amlAudioMixer *audio_mixer)
{
    unsigned int remaining_time = 0;
    audio_mixer->mixing_enable = 0;
    AM_LOGI("++ audio_mixer->mixing_enable %d", audio_mixer->mixing_enable);
    // block exit
    if (audio_mixer->submix_scheduler_state == SUBMIX_SCHEDULER_STANDBY) {
        sem_post(&audio_mixer->submix_standby_sem);
    }

    /* check timers is running or not,
    ** timer should be stopped if running.
    **/
    remaining_time = audio_timer_remaining_time(audio_mixer->submix_timer_id);
    if (remaining_time > 0) {
        audio_timer_stop(audio_mixer->submix_timer_id);
    }
    int ret = aml_audio_timer_delete(audio_mixer->submix_timer_id);
    ALOGD("func:%s timer_id:%d  ret:%d",__func__, audio_mixer->submix_timer_id, ret);

    audio_mixer->exit_thread = 1;
    pthread_join(audio_mixer->out_mixer_tid, NULL);
    audio_mixer->out_mixer_tid = 0;
    if (sem_destroy(&audio_mixer->submix_standby_sem)) {
        ALOGE("%s release submix standby semaphore failed\n", __FUNCTION__);
    } else {
        ALOGD("%s release submix standby semaphore successful\n", __FUNCTION__);
    }
    notify_mixer_exit(audio_mixer);
    return 0;
}

struct pcm *pcm_mixer_get_pcm_handle(struct amlAudioMixer *audio_mixer)
{
    struct pcm *pcm_handle = NULL;
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return NULL;
    }
    R_CHECK_POINTER_LEGAL(NULL, out_port, "");
    pcm_handle = out_port->pcm_handle;
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    return pcm_handle;
}

struct amlAudioMixer *newAmlAudioMixer(struct aml_audio_device *adev, struct audioCfg mixer_cfg, struct audioCfg out_cfg, int mixer_type)
{
    struct amlAudioMixer *audio_mixer = NULL;
    int ret = 0;
    struct audio_config aaudio_config;
    AM_LOGD("");

    audio_mixer = aml_audio_calloc(1, sizeof(*audio_mixer));
    R_CHECK_POINTER_LEGAL(NULL, audio_mixer, "allocate amlAudioMixer:%zu no memory", sizeof(struct amlAudioMixer));
    audio_mixer->adev = adev;
    audio_mixer->submix_standby = 1;
    mixer_set_state(audio_mixer, MIXER_IDLE);
    audio_mixer->mc_out_enable = true;
    audio_mixer->type = mixer_type;
    uint32_t main_channel_mask = 0;

    memcpy(&audio_mixer->cfg, &mixer_cfg, sizeof(struct audioCfg));

    for (int i = 0; i < MIXER_OUTPUT_PORT_NUM; i++) {
        pthread_mutex_init(&audio_mixer->outport_locks[i], NULL);
        pthread_mutex_init(&audio_mixer->outport_delay_locks[i], NULL);
        memset(&audio_mixer->outport_delay_ts[i], 0, sizeof(audio_mixer->outport_delay_ts[i]));
        audio_mixer->outport_delay_ms[i] = 0;
    }

    ret = init_mixer_output_port(audio_mixer, MIXER_OUTPUT_PORT_STEREO_PCM, &audio_mixer->cfg, &out_cfg, MIXER_FRAME_COUNT);
    if (ret < 0) {
        AM_LOGE("init mixer out port failed");
        goto err_tmp;
    }

    if (audio_mixer->type == SUB_MIXER_NORMAL) {
        ret = init_stereo_mixer_buffer(audio_mixer, &audio_mixer->cfg, MIXER_FRAME_COUNT);
        if (ret != 0) {
            AM_LOGE("init_mixer_process_buffer failed");
            goto err_state;
        }
    }
#ifdef ENABLE_AUTOMOTIVE_AUDIO_FUNCTION
    else if (audio_mixer->type == SUB_MIXER_CH_MUX) {
        ret = init_ch_mux_mixer_buffer(audio_mixer, &audio_mixer->cfg, MIXER_FRAME_COUNT);
        if (ret != 0) {
            AM_LOGE("init_mixer_process_buffer failed");
            goto err_state;
        }

        //set main channel mask table for bus output
        aml_pcm_mixing_st *pch_mux_mixer = &audio_mixer->ch_mux_mixer;
        ret = set_bus_out_main_channel_mask(pch_mux_mixer->main_channel_table, mixer_cfg.channelCnt);
        if (ret != 0) {
            AM_LOGE("set main_channel_mask failed");
            goto err_state;
        }
    }
#endif
    else {
        AM_LOGE("Invalid mixer type:%d", audio_mixer->type);
        goto err_state;
    }

    audio_mixer->inportsMasks = 0;
    audio_mixer->inportsAvailMasks = (1 << NR_INPORTS) - 1;
    audio_mixer->aaudio_low_latency = false;
    pthread_mutex_init(&audio_mixer->lock, NULL);
    pthread_mutex_init(&audio_mixer->inport_lock, NULL);

    memset(&aaudio_config, 0, sizeof(aaudio_config));
    aaudio_config.sample_rate = 48000;
    aaudio_config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    aaudio_config.format = AUDIO_FORMAT_PCM_16_BIT;
    init_mixer_multi_aaudio_input_port(audio_mixer, &aaudio_config);

    AM_LOGI("mixer_type:%d chNum:%d format:0x%x main_channel_mask:0x%x",
        audio_mixer->type, mixer_cfg.channelCnt, mixer_cfg.format, main_channel_mask);
    return audio_mixer;

err_state:
    delete_mixer_output_port(audio_mixer, MIXER_OUTPUT_PORT_STEREO_PCM);
err_tmp:
    aml_audio_free(audio_mixer);
    return NULL;
}

void freeAmlAudioMixer(struct amlAudioMixer *audio_mixer)
{
    MIXER_OUTPUT_PORT port_index = MIXER_OUTPUT_PORT_STEREO_PCM;
    R_CHECK_POINTER_LEGAL((void)0, audio_mixer, "");

    if (audio_mixer->cur_output_port_type == MIXER_OUTPUT_PORT_STEREO_PCM ||
        audio_mixer->cur_output_port_type == MIXER_OUTPUT_PORT_MULTI_PCM) {
        delete_mixer_output_port(audio_mixer, audio_mixer->cur_output_port_type);
    }

    if (audio_mixer->type == SUB_MIXER_NORMAL) {
        deinit_stereo_mixer_buffer(audio_mixer);
    }
#ifdef ENABLE_AUTOMOTIVE_AUDIO_FUNCTION
    else if (audio_mixer->type == SUB_MIXER_CH_MUX) {
        deinit_ch_mux_mixer_buffer(audio_mixer);
    }
#endif
    else {
        AM_LOGE("unknown mixer type:%d", audio_mixer->type);
    }

    port_index = MIXER_OUTPUT_PORT_MULTI_PCM;
    pthread_mutex_lock(&audio_mixer->outport_locks[port_index]);
    if (audio_mixer->out_ports[port_index]) {
        free_mc_output_port(audio_mixer->out_ports[port_index]);
        audio_mixer->out_ports[port_index] = NULL;
    }
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    deinit_multich_mixer_buffer(audio_mixer);

    if (audio_mixer->multi_aaudio_port_index >= 0) {
        delete_mixer_input_port(audio_mixer, audio_mixer->multi_aaudio_port_index);
        audio_mixer->multi_aaudio_port_index = -1;
    }

    for (int i = 0; i < MIXER_OUTPUT_PORT_NUM; i++) {
        pthread_mutex_destroy(&audio_mixer->outport_locks[i]);
        pthread_mutex_destroy(&audio_mixer->outport_delay_locks[i]);
    }
    pthread_mutex_destroy(&audio_mixer->lock);
    pthread_mutex_destroy(&audio_mixer->inport_lock);
    aml_audio_free(audio_mixer);
}

int mixer_get_presentation_position(
        struct amlAudioMixer *audio_mixer,
        uint8_t port_index,
        uint64_t *frames,
        int64_t *s64_negative_frames,
        struct timespec *timestamp)
{
    int ret = 0;
    R_CHECK_PARAM_LEGAL(-1, port_index, 0, NR_INPORTS - 1, "");
    pthread_mutex_lock(&audio_mixer->inport_lock);
    input_port *in_port = audio_mixer->in_ports[port_index];
    if (in_port == NULL) {
        AM_LOGE("in_port is null pointer, port_index:%d", port_index);
        pthread_mutex_unlock(&audio_mixer->inport_lock);
        return -EINVAL;
    }
    *frames = in_port->presentation_frames;
    *s64_negative_frames = in_port->s64_negative_frames;
    *timestamp = in_port->timestamp;
    if (!is_inport_pts_valid(in_port)) {
        AM_LOGW("not valid now");
        ret = -ENODATA;
    }
    pthread_mutex_unlock(&audio_mixer->inport_lock);
    return ret;
}

int mixer_set_padding_size(
        struct amlAudioMixer *audio_mixer,
        uint8_t port_index,
        int padding_bytes)
{
    input_port *in_port = audio_mixer->in_ports[port_index];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", port_index);
    return set_inport_padding_size(in_port, padding_bytes);
}

int mixer_outport_pcm_restart(struct amlAudioMixer *audio_mixer)
{
    output_port *out_port = NULL;
    output_port *mc_out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    /*
    * tdm and spdif have same source for HDMI.
    * Here need to restart pcm/tdm device when select audio source tdm to HDMITx.
    * Or the sink device will no sound when it just only support pcm.
    **/
    outport_pcm_restart(out_port);
    mc_out_port = audio_mixer->out_ports[MIXER_OUTPUT_PORT_MULTI_PCM];
    if (mc_out_port) {
        outport_pcm_restart(mc_out_port);
    }
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);
    return 0;
}

bool has_hwsync_stream_running(void *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct amlAudioMixer *audio_mixer = adev->mixerData;
    if (audio_mixer == NULL)
        return false;

    unsigned int masks = audio_mixer->inportsMasks;

    while (masks) {
        input_port *in_port = mixer_get_inport_by_mask_right_first(audio_mixer, &masks);
        if (NULL != in_port && in_port->enInPortType == AML_MIXER_INPUT_PORT_PCM_DIRECT
            && in_port->notify_cbk_data) {
            struct aml_stream_out *out = (struct aml_stream_out *)in_port->notify_cbk_data;
            if ((out != aml_out) && out->hw_sync_mode && !out->standby)
                return true;
        }
    }
    return false;
}

void mixer_dump(int s32Fd, const struct aml_audio_device *pstAmlDev)
{
    if (NULL == pstAmlDev || NULL == pstAmlDev->mixerData) {
        dprintf(s32Fd, "[AML_HAL] [%s:%d] device or sub mixing is NULL !\n", __func__, __LINE__);
        return;
    }
    struct amlAudioMixer *pstAudioMixer = (struct amlAudioMixer *)pstAmlDev->mixerData;
    if (NULL == pstAudioMixer) {
        dprintf(s32Fd, "[AML_HAL] [%s:%d] struct amlAudioMixer is NULL !\n", __func__, __LINE__);
        return;
    }

    dprintf(s32Fd, "\n-------------[AML_HAL] AudioMixer -----------------------------------\n");
    dprintf(s32Fd, "[AML_HAL]---------------input port description cnt: [%d](masks:%#x)---------\n",
        get_mixer_inport_count(pstAudioMixer), pstAudioMixer->inportsMasks);
    for (uint8_t index=0; index < NR_INPORTS; index++) {
        input_port *pstInputPort = pstAudioMixer->in_ports[index];
        if (pstInputPort) {
            dprintf(s32Fd, "[AML_HAL]  input port type: %s(ID:%d)\n", mixerInputType2Str(pstInputPort->enInPortType), pstInputPort->ID);
            dprintf(s32Fd, "[AML_HAL]      Channel       : %10d     | Format            : %#10x\n",
                pstInputPort->cfg.channelCnt, pstInputPort->cfg.format);
            dprintf(s32Fd, "[AML_HAL]      FrameCnt      : %zu     | data size         : %zu Byte\n",
                pstInputPort->data_buf_frame_cnt, pstInputPort->data_len_bytes);
            if (pstInputPort->r_buf) {
                dprintf(s32Fd, "[AML_HAL]      rbuf size     : %10d Byte| Avail size        : %10d Byte\n",
                    pstInputPort->r_buf->size, get_buffer_read_space(pstInputPort->r_buf));
            }
            dprintf(s32Fd, "[AML_HAL]      is_hwsync     : %10d     | start_threshold   : %10d Byte\n",
                pstInputPort->is_hwsync, pstInputPort->inport_start_threshold);
        }
    }
    dprintf(s32Fd, "[AML_HAL]---------------------output port description----------------------\n");
    output_port *pstOutPort = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(pstAudioMixer, &pstOutPort);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return ;
    }
    if (pstOutPort) {
        dprintf(s32Fd, "[AML_HAL]  output port type: %s\n", mixerOutputType2Str(pstOutPort->enOutPortType));
        dprintf(s32Fd, "[AML_HAL]      Channel       : %10d     | Format            : %#10x\n", pstOutPort->cfg.channelCnt, pstOutPort->cfg.format);
        dprintf(s32Fd, "[AML_HAL]      FrameCnt      : %10zu     | data size         : %zu Byte\n",
            pstOutPort->data_buf_frame_cnt, pstOutPort->data_buf_len);
        pthread_mutex_unlock(&pstAudioMixer->outport_locks[port_index]);
    } else {
        dprintf(s32Fd, "[AML_HAL] not find output port description!!!\n");
    }
}

void mixer_using_alsa_device_dump(int s32Fd, const struct aml_audio_device *pstAmlDev)
{
    if (NULL == pstAmlDev || NULL == pstAmlDev->mixerData) {
        dprintf(s32Fd, "\t[AML_HAL] device or audioMixer is NULL !\n");
        return;
    }
    struct amlAudioMixer *pstAudioMixer = (struct amlAudioMixer *)pstAmlDev->mixerData;
    if (NULL == pstAudioMixer) {
        dprintf(s32Fd, "\t[AML_HAL] amlAudioMixer is NULL !\n");
        return;
    }

    output_port *pstOutPort = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(pstAudioMixer, &pstOutPort);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return ;
    }

    if (pstOutPort) {
        struct pcm *pcm = pstOutPort->pcm_handle;
        if (!pcm) {
            pthread_mutex_unlock(&pstAudioMixer->outport_locks[port_index]);
            return;
        }

        aml_alsa_pcm_info_dump(pcm, s32Fd);
        pthread_mutex_unlock(&pstAudioMixer->outport_locks[port_index]);
    }
}

#ifdef SUPPORT_KARAOKE
int mixer_set_karaoke(struct amlAudioMixer *audio_mixer, struct kara_manager *kara)
{
    output_port *out_port = NULL;
    MIXER_OUTPUT_PORT port_index = mixer_get_cur_outport(audio_mixer, &out_port);
    if (port_index == MIXER_OUTPUT_PORT_INVAL) {
        AM_LOGE("%s :mixer_get_cur_outport is fail", __func__);
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, out_port, "");
    ALOGI("++%s(), set karaoke = %p", __func__, kara);
    outport_set_karaoke(out_port, kara);
    pthread_mutex_unlock(&audio_mixer->outport_locks[port_index]);

    return 0;
}
#endif

int mixer_reset_virtual_buf(void *audio_mixer, bool reset)
{
    struct amlAudioMixer *mixer = NULL;
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");
    mixer = audio_mixer;

    mixer->reset_virtual_buf = reset;
    return 0;
}

int mixer_get_inport_start_threshold(void *aml_out, struct amlAudioMixer *audio_mixer)
{
    struct aml_stream_out *out = (struct aml_stream_out *)aml_out;
    input_port *in_port = NULL;

    R_CHECK_POINTER_LEGAL(0, out, "");
    R_CHECK_POINTER_LEGAL(0, audio_mixer, "");
    R_CHECK_PARAM_LEGAL(0, out->inputPortID, 0, NR_INPORTS - 1, "");
    in_port = audio_mixer->in_ports[out->inputPortID];
    R_CHECK_POINTER_LEGAL(0, in_port, "");

    return in_port->inport_start_threshold;
}

input_port *mixer_get_inport(
        struct amlAudioMixer *audio_mixer, uint32_t *pMasks) {
    return mixer_get_inport_by_mask_right_first(audio_mixer, pMasks);
}

void set_submix_continuous_state(struct amlAudioMixer *audio_mixer, int state) {
    audio_mixer->submix_scheduler_state = state;
    if (state == SUBMIX_SCHEDULER_RUNNING) {
        if (sem_post(&audio_mixer->submix_standby_sem)) {
            ALOGE("%s post submix unstandby semaphore failed", __FUNCTION__);
        } else {
            ALOGD("%s  post submix unstandby semaphore successful", __FUNCTION__);
        }
    } else {
        // do nothing
    }
}

submix_scheduler_state_t aml_get_submix_scheduler_state(struct amlAudioMixer *audio_mixer)
{
    return audio_mixer->submix_scheduler_state;
}

int aml_set_submix_scheduler_state(struct amlAudioMixer *audio_mixer, int sch_state)
{
    struct aml_audio_device *adev = aml_adev_get_handle();
    bool is_arc_connecting = is_HDMI_connected(adev);/*(adev->active_outport == OUTPORT_HDMI_ARC);*/
    bool is_netflix = adev->is_netflix;
    unsigned int remaining_time = 0;
    bool is_karaoke_on = false; /* do not standby when karaoke on*/

    if (sch_state == SUBMIX_SCHEDULER_STANDBY) {
        /*If there are other streams present, the submix status to running*/
        //aml_get_is_exist_active_stream() to replace usecase_masks
        if (aml_get_is_exist_active_stream() || !is_TV(adev) || mmap_audio_has_active_client(adev->mmap_audio_manager)) {
            sch_state = SUBMIX_SCHEDULER_RUNNING;
        }
    }
    if (sch_state <= SUBMIX_SCHEDULER_NONE ||  sch_state >= SUBMIX_SCHEDULER_MAX) {
          ALOGE("%s  sch_state:%d is an invalid scheduler state.", __func__, sch_state);
          return -1;
    } else if (audio_mixer->last_scheduler_state == sch_state) {
       ALOGV("%s  sch_state:%d %s, submix scheduler state not changed.", __func__, sch_state, submix_state_2_string[sch_state]);
       return 0;
    }

#ifdef SUPPORT_KARAOKE
    if (karaoke_get_on(&adev->usb_audio.karaoke)
        || karaoke_get_on(&adev->linein_karaoke)) {
        is_karaoke_on = true;
    }
#endif

    if (!is_arc_connecting && !is_netflix && !is_karaoke_on) {
        remaining_time = audio_timer_remaining_time(audio_mixer->submix_timer_id);
        if (remaining_time > 0) {
            audio_timer_stop(audio_mixer->submix_timer_id);
        }

        if (sch_state == SUBMIX_SCHEDULER_STANDBY) {
            audio_one_shot_timer_start(audio_mixer->submix_timer_id, AML_TIMER_DELAY);
        } else {
            set_submix_continuous_state(audio_mixer, sch_state);
        }
        ALOGI("%s sch_state:%d %s is sent to submix", __func__, sch_state, submix_state_2_string[sch_state]);
    } else {
        remaining_time = audio_timer_remaining_time(audio_mixer->submix_timer_id);
        if (remaining_time > 0) {
            audio_timer_stop(audio_mixer->submix_timer_id);
        }

        sch_state = SUBMIX_SCHEDULER_RUNNING;
        set_submix_continuous_state(audio_mixer, sch_state);
        ALOGI("%s  is_arc_connecting:%d, is_netflix:%d, sch_state:%d %s is sent to submix", __func__,
            is_arc_connecting, is_netflix, sch_state, submix_state_2_string[sch_state]);
    }

    audio_mixer->last_scheduler_state = sch_state;
    return 0;
}

int aml_audiohal_sch_state_2_submix(struct amlAudioMixer *audio_mixer, int sch_state) {
    if (audio_mixer && audio_mixer->mixing_enable) {
        pthread_mutex_lock(&audio_mixer->lock);
        aml_set_submix_scheduler_state(audio_mixer, sch_state);
        pthread_mutex_unlock(&audio_mixer->lock);
    }
    return 0;
}


/*
**these code were moved from sub_mixing_factory.c
**it was for unifying write interface and removing sub_mixing_factory.c
*/
int on_notify_cbk(void *data)
{
    struct aml_stream_out *out = data;
    pthread_cond_broadcast(&out->cond);
    return 0;
}

int on_input_avail_cbk(void *data)
{
    struct aml_stream_out *out = data;
    pthread_cond_broadcast(&out->cond);
    return 0;
}


int aml_do_hwsync_action(void *stream, void *abuffer)
{
    aml_audio_buffer_t *audioBuffer = (aml_audio_buffer_t *)abuffer;
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct amlAudioMixer *audio_mixer = adev->mixerData;
    audio_hwsync_t *hw_sync = aml_out->hwsync;
    uint64_t  cur_pts = ULLONG_MAX;
    bool amaster_mode = true;
    uint64_t apts64 = 0, apts = 0;
    int debug_enable = get_debug_value(AML_DEBUG_AUDIOHAL_HW_SYNC);
    int inport_latency_ms = 0, alsa_latency_ms = 0;
    int tuning_latency_ms = 0;
    int video_delay_ms = 0;
    int speed_latency_ms = 0;
    int total_latency_pts = 0;
    bool is_valid_pts = true;
    bool alsa_running_status = true;
    bool is_dts = is_dts_stream(aml_out);
    uint64_t cur_real_pts = 0; // just for speed avsync
    struct timespec current_timestamp;
    aml_stream_speed_info_t *speed_info = &aml_out->speed_info;
    int post_delay_ms = 0;

    if (audioBuffer) {
        cur_pts = audioBuffer->apts;
    }

    //inport + alsa + tuning + video  latency
    if (eDolbyMS12Lib == adev->dolby_lib_type_last) {
        alsa_latency_ms = out_get_alsa_latency_frames(stream)* 1000 / aml_out->config.rate;
    } else {
        inport_latency_ms = mixer_get_inport_latency_frames(audio_mixer, aml_out->inputPortID) * 1000 / aml_out->config.rate;
        alsa_latency_ms = mixer_get_outport_latency_frames(audio_mixer) * 1000 / aml_out->config.rate;
    }
    tuning_latency_ms = aml_audio_get_nonms12_tunnel_latency(stream, adev->sink_format)/48;
    video_delay_ms = get_media_video_delay(&adev->alsa_mixer);
    post_delay_ms = inport_latency_ms + alsa_latency_ms;

    if (speed_info->speed_handle) {
        int post_delay_frame = post_delay_ms * 48;
        speed_latency_ms = speed_info->last_latency_frame / 48;
        if (speed_latency_ms < 0) {
            speed_latency_ms = 0;
        }
        aml_audio_speed_update_post_delay(&speed_info->post_delay, speed_info->speed, post_delay_frame);
        post_delay_frame = aml_audio_speed_calculate_post_delay(&speed_info->post_delay, post_delay_frame);
        post_delay_ms = post_delay_frame / 48;
    }
    total_latency_pts = (speed_latency_ms + post_delay_ms + tuning_latency_ms - video_delay_ms) * 90;
    if (cur_pts >= abs(total_latency_pts)) {
        apts = cur_pts - total_latency_pts;
        is_valid_pts = true;
    } else {
        apts = 0;
        is_valid_pts = false;
    }
    apts64 = apts & ULLONG_MAX;
    /*if the pts is zero, to avoid video pcr not set issue, we just set it as 1ms*/
    if (apts64 == 0) {
        apts64 = 1 * 90;
    }
    if (debug_enable) {
        AM_LOGI("total latency:%d ms  inport_latency_ms:%d alsa_latency_ms:%d post_delay_ms %d video delay:%d speed_latency_ms:%d tuning latency_ms:%d; input apts 0x%"
            PRIx64 "(%" PRIu64 "ms); adjusted apts64 0x%" PRIx64 "(%" PRIu64 "ms)\n",
            total_latency_pts / 90, inport_latency_ms, alsa_latency_ms, post_delay_ms, video_delay_ms, speed_latency_ms, tuning_latency_ms, cur_pts, cur_pts/90, apts64, apts64/90);
    }

    aml_audio_hwsync_update_threshold(hw_sync);
    if (hw_sync->wait_video_done == false) {
        aml_out->is_waiting_video = true;
        aml_hwsync_wait_video_start(hw_sync);
        aml_hwsync_wait_video_drop(hw_sync, apts64);
        aml_out->is_waiting_video = false;
        hw_sync->wait_video_done = true;
        aml_out->trace_last_write_time_ms = 0;
    } else {
        aml_hwsync_wrap_is_amaster(hw_sync, &amaster_mode);
        if (!amaster_mode) {
            aml_out->restore_vmaster = true;
            aml_hwsync_wrap_set_amaster(hw_sync, true);
        }
    }

    if (hw_sync->first_apts_flag == false) {
        if (audio_is_linear_pcm(adev->optical_format)) {
            alsa_running_status = aml_out->alsa_running_status;
        } else if (aml_out->spdifout_handle){
            alsa_running_status = aml_audio_spdifout_get_status(aml_out->spdifout_handle);
        }
        if (alsa_running_status == false) {
            AM_LOGI("alsa is not stable, skip this packet !");
            return 0;
        }
    }

    if (is_valid_pts) {
        uint64_t pcr = 0;
        int pcr_pts_gap = 0;
        if (hw_sync->first_apts_flag == false) {
            aml_audio_hwsync_set_first_pts(aml_out->hwsync, apts64);
            aml_hwsync_wrap_reset_pcrscr_speed(aml_out->hwsync, apts64, speed_info->speed, true);
        }

        aml_hwsync_wrap_get_pts(aml_out->hwsync, &pcr);
        pcr_pts_gap = ((int)(apts64 - pcr)) / 90;
        if (speed_info->speed_handle) {
            aml_audio_speed_add_apts_gap(&speed_info->sync_apts_gap, pcr_pts_gap);
        }

        if (aml_out->streamType == STREAM_PCM_HWSYNC &&
            abs(pcr_pts_gap) > (APTS_DISCONTINUE_THRESHOLD_MIN_70MS) &&
            abs(pcr_pts_gap) < APTS_DISCONTINUE_THRESHOLD_MIN_3S &&
            apts64 > pcr &&
            pcr != 0) {
            // this code is for CTS cases about pcm tunnel mode stream.
            aml_out->is_insert_zero_data = true;
            aml_out->insert_zero_data_ms = pcr_pts_gap;
        } else {
            float speed_select = speed_info->speed;
            bool force_update = false;
            aml_audio_speed_post_delay_t *p_post_delay = &speed_info->post_delay;

            if (p_post_delay->transitioning || !is_float_equal(p_post_delay->next_speed, speed_info->speed)) {
                speed_select = p_post_delay->last_speed;
            } else if (speed_info->hwsync_force_update) {
                if (hw_sync->last_output_pts && hw_sync->last_output_pts != ULLONG_MAX) {
                    // Make sure : different speed has different apts value.
                    uint64_t mini_apts64 = hw_sync->last_output_pts + 90;
                    if (apts64 < mini_apts64) {
                        AM_LOGI("apts64 change %" PRIu64 " to %" PRIu64 "", apts64, mini_apts64);
                        apts64 = mini_apts64;
                    }
                }
                force_update = true;
                speed_info->hwsync_force_update = false;
            }
            aml_hwsync_wrap_reset_pcrscr_speed(aml_out->hwsync, apts64, speed_select, force_update);
            aml_out->is_insert_zero_data = false;
        }
        clock_gettime(CLOCK_MONOTONIC, &current_timestamp);
        hw_sync->last_output_pts = apts64;
        hw_sync->last_timestamp  = current_timestamp;

        {
            int64_t time_diff = calc_time_interval_us(&aml_out->last_avsync_timestamp, &current_timestamp);
            if (debug_enable || time_diff >= (TIME_DIFF_THRESHOLD * USEC_PER_SEC)) {
                AM_LOGI("[stream_id:%p, %s]tunnel status:%d hwsync:%p mediasync:%p hwsync_id:%d 0x%x"
                    " start_pts[%"PRIu64"]ms, current_pts[%"PRIu64"]ms current_pcr[%"PRIu64"]ms diff[%d]ms",
                    aml_out,
                    aml_out->nickname,
                    aml_out->stream_status,
                    aml_out->hwsync,
                    aml_out->hwsync->mediasync,
                    aml_out->hwsync->hwsync_id,aml_out->hwsync->hwsync_id,
                    aml_out->hwsync->first_apts / 90,
                    apts64 / 90,
                    pcr / 90,
                    pcr_pts_gap);
                aml_out->last_avsync_timestamp = current_timestamp;
            }
        }
    } else {
        AM_LOGI("%s aml_out:%p  write_count:%d,  drop this pts (is_valid_pts:%d), input apts 0x%" PRIx64 "(%" PRIu64 "ms) ", __func__,
            aml_out, aml_out->write_count, is_valid_pts, cur_pts, cur_pts/90);
    }

    return 0;
}


void audio_mixer_post_sleep(void *out)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)out;
    uint64_t curr_time_us = 0;
    struct aml_audio_device *adev = (aml_out != NULL ? aml_out->dev : NULL);

    if (aml_out == NULL || adev == NULL || !adev->useAudioMixer) {
        return;
    }
    if (aml_out->audiomixer_sleep_start_us == 0 || aml_out->audiomixer_sleep_time_us <= 0) {
        return;
    }

    curr_time_us = aml_audio_get_systime();
    if (curr_time_us > aml_out->audiomixer_sleep_start_us) {
        uint64_t past_time_us = curr_time_us - aml_out->audiomixer_sleep_start_us;
        if (aml_out->audiomixer_sleep_time_us > past_time_us) {
            uint64_t slee_time_us = aml_out->audiomixer_sleep_time_us - past_time_us;
            usleep(slee_time_us);
            if (adev->debug_flag) {
                AM_LOGI("sleep_time_us %"PRId64", actual sleep %" PRId64 " us",
                    slee_time_us, aml_audio_get_systime() - curr_time_us);
            }
        }
    }
}

input_port *get_mixer_stream_inport(struct aml_stream_out *aml_out)
{
    struct aml_audio_device *adev = NULL;
    struct amlAudioMixer *audio_mixer = NULL;
    input_port *in_port = NULL;

    R_CHECK_POINTER_LEGAL(NULL, aml_out, "");
    adev = aml_out->dev;
    R_CHECK_POINTER_LEGAL(NULL, adev, "");
    audio_mixer = adev->mixerData;
    R_CHECK_POINTER_LEGAL(NULL, audio_mixer, "");
    R_CHECK_PARAM_LEGAL(NULL, aml_out->inputPortID, 0, NR_INPORTS - 1, "");
    in_port = audio_mixer->in_ports[aml_out->inputPortID];
    return in_port;
}

static ssize_t aml_out_write_to_mixer(struct audio_stream_out *stream, const void* buffer,
                                    size_t bytes, void *abuffer)
{
    aml_audio_buffer_t *audioBuffer = (aml_audio_buffer_t *)abuffer;
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct amlAudioMixer *audio_mixer = adev->mixerData;
    const char *data = (char *)buffer;
    size_t written_total = 0, frame_size = 4;
    uint32_t latency_frames = 0;
    struct timespec ts;
    input_port *in_port = NULL;

    if (audioBuffer) {
        buffer = audioBuffer->pData;
        bytes = audioBuffer->size;
        uint64_t apts = audioBuffer->apts;

        //AM_LOGI("pData:%p size:%zu apts:%"PRIu64" ms", buffer, bytes, apts/90);
        if (out->hw_sync_mode && out->hwsync && out->hwsync->mediasync)
            aml_do_hwsync_action(stream, abuffer);

            AM_AOUT_OutputMode_t cur_sound_track_mode = adev->sound_track_mode;
#ifdef ENABLE_DVB_PATCH
            if (is_dtv_stream_out(stream)) {
                cur_sound_track_mode = get_dtv_sound_channel_mode(stream);
            }
#endif

            if (audioBuffer->bufFormat.channelMask == AUDIO_CHANNEL_OUT_STEREO && cur_sound_track_mode > AM_AOUT_OUTPUT_STEREO) {
                aml_audio_switch_output_mode(audioBuffer->pData, audioBuffer->size, audioBuffer->bufFormat.format, cur_sound_track_mode);
            }

#ifdef ENABLE_DVB_PATCH
        if (is_dtv_stream_out(stream)) {
            dtvsync_process_res ret = dtv_audio_sync_non_ms12_process(stream, abuffer);
            if (ret == DTVSYNC_AUDIO_DROP) {
               return bytes;
            }
        }
#endif
    }

    in_port = audio_mixer->in_ports[out->inputPortID];
    R_CHECK_POINTER_LEGAL(-EINVAL, in_port, "port_index:%d", out->inputPortID);
    if (out->hw_sync_mode && out->is_insert_zero_data) {
        /*this for dynamically get bytes with 0 inserted*/
        in_port->bytes_to_insert = out->insert_zero_data_ms * 48 * (audioBuffer->bufFormat.channelCount * audio_bytes_per_sample(audioBuffer->bufFormat.format));
    }

    if (adev->is_netflix && (STREAM_PCM_NORMAL == out->streamType
        || STREAM_PCM_HWSYNC == out->streamType
        || (STREAM_RAW_HWSYNC == out->streamType && adev->dolby_decode_enable)
        || (STREAM_RAW_DIRECT == out->streamType && adev->dolby_decode_enable))) {
        out->data_handle_info.max_detect_time_ms = NETFLIX_FADEIN_MAX_DETECT_TIME_MS;
        aml_audio_data_handle(stream, buffer, bytes);
    }

    do {
        ssize_t written = 0;
        AM_LOGV("stream streamType: %s, written_total %zu, bytes %zu",
            streamType2Str(out->streamType), written_total, bytes);

        written = mixer_write_inport(audio_mixer,
                out->inputPortID, data, bytes - written_total);
        if (written < 0) {
            AM_LOGE("write failed, errno = %zu", written);
            return written;
        }

        if (written > 0) {
            written_total += written;
            data += written;
        }
        AM_LOGV("port index(%d) written(%zu), written_total(%zu), bytes(%zu)",
            out->inputPortID, written, written_total, bytes);

        if (written_total >= bytes) {
            AM_LOGV("exit");
            break;
        }

        ts_wait_time_us(&ts, 5000);
        AM_LOGV("-wait....");
        pthread_mutex_lock(&out->cond_lock);
        pthread_cond_timedwait(&out->cond, &out->cond_lock, &ts);
        AM_LOGV("--wait wakeup");
        pthread_mutex_unlock(&out->cond_lock);
    } while (1);

    return written_total;
}

ssize_t out_write_pcm_to_AudioMixer(void *stream, const void *buffer, size_t bytes, void *abuffer)
{
    aml_audio_buffer_t *audioBuffer = (aml_audio_buffer_t *)abuffer;
    struct aml_stream_out *out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = out->dev;
    struct amlAudioMixer *audio_mixer = adev->mixerData;
    ssize_t written = 0;
    size_t remain = 0;
    int frame_size = audio_bytes_per_sample(out->audioCfg.format) * audio_channel_count_from_out_mask(out->audioCfg.channel_mask);
    int channels = 2;
    int sample_size = 2;
    float volume[8];
    float last_volume[8];
    input_port *inport = get_mixer_stream_inport(out);
    bool start_active = false;
    int sample_rate = out->audioCfg.sample_rate;
    void * out_buf = (void*)buffer;
    int out_size = bytes;
    aml_stream_speed_info_t *speed_info = &out->speed_info;

    volume[0]   = out->volume_l;
    volume[1]   = out->volume_r;
    last_volume[0]   = out->last_volume_l;
    last_volume[1]   = out->last_volume_r;

    if (audioBuffer) {
        buffer = audioBuffer->pData;
        bytes = audioBuffer->size;
    }
    if (out->audiomixer_standby) {
        start_active = true;
        out->audiomixer_standby = false;
        aml_audio_data_handle_init((struct audio_stream_out *)out);
    }
    if (sample_rate <= 0) {
        sample_rate = 48000;  // default value
    }

    /*
     * if out->aml_dec isn't NULL, lpcm5.1/7.1 will be downmix to 2ch.
     * then output_stream information is incorrect.
    */
    if (audio_is_linear_pcm(out->hal_format) && out->aml_dec == NULL) {
        frame_size = out->hal_frame_size;
        channels = audio_channel_count_from_out_mask(out->hal_channel_mask);
        sample_size = audio_bytes_per_sample(out->hal_format);
    }

    //clock_gettime(CLOCK_MONOTONIC, &tval);
    if (out->aml_dec == NULL) {
        /*
        android only support max stereo stream volume configuration,we have to reuse left volume as
        C/LFE/Ls/Rs/Lrs/Rrs volume
        */
        if (channels > 2) {
            for (int ch = 2; ch < channels; ch ++) {
                last_volume[ch] = last_volume[0];
                volume[ch] = volume[0];
            }
        }
        if (adev->debug_flag) {
            AM_LOGI("last_volume=%f volume=%f channels=%d bytes=%zu", last_volume[0], volume[0], channels, bytes);
        }
        apply_volume_fade(last_volume, volume, (void *)buffer, sample_size, channels, bytes);
        out->last_volume_l = out->volume_l;
        out->last_volume_r = out->volume_r;
    }

    if (speed_info->last_speed != speed_info->speed) {
        int delay_frames = mixer_get_inport_latency_frames(audio_mixer, out->inputPortID);
        set_inport_next_speed(inport, speed_info->speed, delay_frames);
        speed_info->last_speed = speed_info->speed;
        aml_audio_speed_reset_apts_gap(&speed_info->sync_apts_gap, AML_AUDIO_SPEED_DETECT_GAP_TIME_MS);
    }

    // base on speed 1.0f
    out->frame_write_sum += bytes / frame_size;

    written = aml_out_write_to_mixer(stream, buffer, out_size, abuffer);
    if (written >= 0) {
        remain = out_size - written;
        if (remain > 0) {
            AM_LOGE("INVALID partial written");
        }

        if (inport) {
            int target_buffer_us = 0;
            int sleep_time_us = 0;
            int64_t curr_time_us = aml_audio_get_systime();
            int64_t diff_time_us = curr_time_us - audio_mixer->inports_read_time_us;
            int64_t avail_buffer_us = (int64_t)inport->rbuf_avail(inport)/frame_size * 1000000 / sample_rate;
            int written_data_us = (int64_t)written/frame_size * 1000000 / sample_rate;
            int calc_buffer_us = avail_buffer_us - diff_time_us;
            int64_t start_threshold_us = inport->start_threshold_ns/1000;;

            if (start_threshold_us > 0) {
                target_buffer_us = start_threshold_us;
            }
            // At lease, buffer should have 16ms data to tolerate system/audiomixer jitter.
            if (target_buffer_us < 16*1000) {
                target_buffer_us = 16*1000;
            }
            if (inport->enInPortType == AML_MIXER_INPUT_PORT_PCM_SYSTEM) {
                target_buffer_us = 32*1000;
            }
            if (target_buffer_us > inport->buffer_len_ns/1000) {
                target_buffer_us = inport->buffer_len_ns/1000;
            }

            if (calc_buffer_us > target_buffer_us) {
                sleep_time_us = calc_buffer_us - target_buffer_us;
                // Don't control system audio, its input port buffer is small.
                if (!out->is_normal_pcm && start_active) {
                    if (start_threshold_us >= written_data_us && sleep_time_us < written_data_us/2) {
                        sleep_time_us = written_data_us/2;
                    }
                }
            }
            if (adev->debug_flag) {
                AM_LOGI("%s format 0x%x, sleep_time_us %d, calc_buffer_us %d, target_buffer_us %d, avail_buffer_us %" PRId64 " (bytes : %d)",
                   out->nickname, out->hal_format, sleep_time_us, calc_buffer_us, target_buffer_us, avail_buffer_us, inport->rbuf_avail(inport));
            }

            if (!audio_is_linear_pcm(out->hal_format)) {
                /*
                 * Currently npcm and pcm output is serial,
                 * If sleep after pcm output, npcm will start late. then their pipeline latency is different.
                 *
                 * Solution :
                 * don't sleep at here, try to let pcm and npcm output simultaneously
                */
                out->audiomixer_sleep_start_us = curr_time_us;
                out->audiomixer_sleep_time_us = sleep_time_us;
            } else {
                if (sleep_time_us > 0) {
                    usleep(sleep_time_us);
                }
                out->audiomixer_sleep_start_us = 0;
                out->audiomixer_sleep_time_us = 0;
            }
        }
    } else {
        AM_LOGE("write fail, err = %zd", written);
    }

exit:
    // update new timestamp
    pthread_mutex_lock(&out->apts_update_lock);
    clock_gettime(CLOCK_MONOTONIC, &out->timestamp);
    out->lasttimestamp.tv_sec = out->timestamp.tv_sec;
    out->lasttimestamp.tv_nsec = out->timestamp.tv_nsec;
    if (written >= 0) {
        uint32_t latency_frames = mixer_get_inport_latency_frames(audio_mixer, out->inputPortID);
                + mixer_get_outport_latency_frames(audio_mixer);
        if (out->frame_write_sum > latency_frames)
            out->last_frames_position = out->frame_write_sum - latency_frames;
        else
            out->last_frames_position = out->frame_write_sum;

        if (0) {
            AM_LOGI("last position %" PRId64 ", latency_frames %d", out->last_frames_position, latency_frames);
        }
    }
    pthread_mutex_unlock(&out->apts_update_lock);

    return written;
}

static int startMixingThread(struct amlAudioMixer *amixer)
{
    return pcm_mixer_thread_run(amixer);
}

static int exitMixingThread(struct amlAudioMixer *amixer)
{
    return pcm_mixer_thread_exit(amixer);
}

static int initSubMixingOutput(
        enum MIXER_TYPE type,
        struct aml_audio_device *adev)
{
    if (type == MIXER_LPCM) {
        struct audioCfg mixer_cfg;
        struct audioCfg outport_cfg;
        int mixer_type = SUB_MIXER_NORMAL;
#ifdef ENABLE_AUTOMOTIVE_AUDIO_FUNCTION
        mixer_type = SUB_MIXER_CH_MUX;
        output_get_default_bus_config(&outport_cfg);
        memcpy(&mixer_cfg, &outport_cfg, sizeof(struct audioCfg));
#else
        mixer_type = SUB_MIXER_NORMAL;
        output_get_default_config(&outport_cfg, is_TV(adev));
        mixer_get_default_config(&mixer_cfg, is_TV(adev));
#endif
        audio_format_t primaryOutFormat = get_primary_out_format(adev);
        switch (primaryOutFormat)
        {
        case AUDIO_FORMAT_PCM_16_BIT:
        case AUDIO_FORMAT_PCM_32_BIT:
            output_change_config_format(&outport_cfg, primaryOutFormat);
            mixer_change_config_format(&mixer_cfg, primaryOutFormat);
            break;
        default:
            ALOGW("%s() Invalid primaryOutFormat:0x%x using default mixerOutFormat:0x%x",__func__,
                primaryOutFormat, mixer_cfg.format);
            break;
        }

        struct amlAudioMixer *amixer = newAmlAudioMixer(adev, mixer_cfg, outport_cfg, mixer_type);
        R_CHECK_POINTER_LEGAL(-ENOMEM, amixer, "newAmlAudioMixer failed");
        adev->mixerData = amixer;
        /* TV product has EQ DRC and sink gain */
        if (adev->eq_drc_inited) {
            ALOGI("%s(), eq data addr %p", __func__, &adev->eq_data);
            subMixingSetEQData(adev, &adev->eq_data);
        }
        if (is_TV(adev)) {
            ALOGI("%s(), sink gain addr %p", __func__, adev->sink_gain);
            subMixingSetSinkGain(adev, adev->sink_gain);
        }
        startMixingThread(adev->mixerData);
    } else if (type == MIXER_MS12) {
        //TODO
        AM_LOGW("not support yet, in TODO list");
    } else {
        AM_LOGE("not support");
        return -EINVAL;
    }
    return 0;
};

static int releaseSubMixingOutput(struct amlAudioMixer *amixer)
{
    R_CHECK_POINTER_LEGAL(-EINVAL, amixer, "");
    AM_LOGI("++");
    exitMixingThread(amixer);
    freeAmlAudioMixer(amixer);
    amixer = NULL;

    return 0;
}


int initHalSubMixing(enum MIXER_TYPE type,
        struct aml_audio_device *adev,
        bool isTV)
{
    int ret = 0;

    ALOGI("type %d, isTV %d", type, isTV);
    ret = initSubMixingOutput(type, adev);
    if (ret < 0) {
        AM_LOGE("fail to init mixer");
        goto err1;
    }
    return 0;
err1:
    return ret;
}

int deleteHalSubMixing(struct aml_audio_device *adev)
{
    releaseSubMixingOutput(adev->mixerData);
    adev->mixerData = NULL;
    return 0;
}


struct pcm *getSubMixingPCMdev(struct amlAudioMixer *amixer)
{
    return pcm_mixer_get_pcm_handle(amixer);
}

int subMixingOutputRestart(struct aml_audio_device *adev)
{
    struct amlAudioMixer *audio_mixer = NULL;

    R_CHECK_POINTER_LEGAL(-EINVAL, adev, "");
    audio_mixer = adev->mixerData;
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");

    return mixer_outport_pcm_restart(audio_mixer);
}

static int subMixingOutMsg(struct aml_audio_device *adev, PORT_MSG msg, void *info, int info_len)
{
    struct amlAudioMixer *audio_mixer = NULL;
    int ret = 0;
    R_CHECK_POINTER_LEGAL(-EINVAL, adev, "");
    audio_mixer = adev->mixerData;
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");

    send_mixer_outport_message(audio_mixer, MIXER_OUTPUT_PORT_STEREO_PCM, msg, info, info_len);
    return 0;
}

int subMixingSetSinkGain(struct aml_audio_device *adev, void *sink_gain)
{
    return subMixingOutMsg(adev, MSG_SINK_GAIN, &sink_gain, sizeof(sink_gain));
}

int subMixingSetEQData(struct aml_audio_device *adev, void *eq_data)
{
    return subMixingOutMsg(adev, MSG_EQ_DATA, &eq_data, sizeof(eq_data));
}

int subMixingSetSrcGain(struct aml_audio_device *adev, float gain)
{
    return subMixingOutMsg(adev, MSG_SRC_GAIN, &gain, sizeof(gain));
}

int subMixingSetAudioPostprocess(struct aml_audio_device *adev, void **postprocess)
{
    return subMixingOutMsg(adev, MSG_EFFECT, postprocess, sizeof(void *));
}

int subMixingEnableMultiChOutput(struct aml_audio_device *adev, bool enable)
{
    int ret = 0;
    struct amlAudioMixer *audio_mixer = NULL;

    R_CHECK_POINTER_LEGAL(-EINVAL, adev, "");
    audio_mixer = adev->mixerData;
    R_CHECK_POINTER_LEGAL(-EINVAL, audio_mixer, "");

    mixer_enable_multich_output(audio_mixer, enable);
    return ret;
}

