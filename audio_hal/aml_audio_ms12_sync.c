/*
 * Copyright (C) 2021 The Android Open Source Project
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
#define LOG_TAG "audio_hw_hal_ms12sync"
//#define LOG_NDEBUG 0
#include <cutils/log.h>
#include <cutils/properties.h>
#include <inttypes.h>

#include "audio_hw.h"
#include "audio_hw_utils.h"
#include "audio_bt_hal.h"
#include "audio_hw_ms12.h"
#ifndef MS12_V24_ENABLE
#include "audio_avsync_table_aml_ms12_v1.h"
#else
#include "audio_avsync_table_aml_ms12_v2.h"
#endif
#include "aml_audio_spdifout.h"
#include "aml_audio_ms12_sync.h"
#include "audio_hw_resource_mgr.h"
#include "dolby_lib_api.h"

#define MS12_OUTPUT_5_1_DDP "vendor.media.audio.ms12.output.5_1_ddp"

static int get_nonms12_dv_tunnel_input_latency(audio_format_t input_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        /*for non tunnel ddp2h/heaac case:netflix AL1 case */
        prop_name = AVSYNC_NONMS12_DV_TUNNEL_PCM_LATENCY_PROPERTY;
        latency_ms = AVSYNC_NONMS12_DV_TUNNEL_PCM_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3: {
        /*for non tunnel dolby ddp5.1 case:netflix AL1 case*/
        prop_name = AVSYNC_NONMS12_DV_TUNNEL_DDP_LATENCY_PROPERTY;
        latency_ms = AVSYNC_NONMS12_DV_TUNNEL_DDP_LATENCY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}

static int get_nonms12_dv_tunnel_output_latency(audio_format_t output_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;

    switch (output_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        prop_name = AVSYNC_NONMS12_DV_TUNNEL_PCMOUT_LATENCY_PROPERTY;
        latency_ms = AVSYNC_NONMS12_DV_TUNNEL_PCMOUT_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3: {
        prop_name = AVSYNC_NONMS12_DV_TUNNEL_DDPOUT_LATENCY_PROPERTY;
        latency_ms = AVSYNC_NONMS12_DV_TUNNEL_DDPOUT_LATENCY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}


static int get_ms12_dv_tunnel_input_latency(audio_format_t input_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        /*for non tunnel ddp2h/heaac case:netflix AL1 case */
        prop_name = AVSYNC_MS12_DV_TUNNEL_PCM_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DV_TUNNEL_PCM_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3: {
        /*for non tunnel dolby ddp5.1 case:netflix AL1 case*/
        prop_name = AVSYNC_MS12_DV_TUNNEL_DDP_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DV_TUNNEL_DDP_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC4: {
        prop_name = AVSYNC_MS12_DV_TUNNEL_AC4_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DV_TUNNEL_AC4_LATENCY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}

static int get_ms12_dv_tunnel_output_latency(audio_format_t output_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;

    switch (output_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        prop_name = AVSYNC_MS12_DV_TUNNEL_PCMOUT_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DV_TUNNEL_PCMOUT_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3: {
        prop_name = AVSYNC_MS12_DV_TUNNEL_DDPOUT_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DV_TUNNEL_DDPOUT_LATENCY;
        break;
    }
    case AUDIO_FORMAT_MAT: {
        prop_name = AVSYNC_MS12_DV_TUNNEL_MATOUT_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DV_TUNNEL_MATOUT_LATENCY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}


int get_sink_dv_latency_offset(bool tunnel, bool is_netflix)
{
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    struct aml_audio_device *adev = adev_get_handle();

    if (is_netflix) {
        if (is_SBR_active(adev)) {
            if (tunnel) {
                prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_DV_TUNNEL_LATENCY_PROPERTY;
                latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_DV_TUNNEL_LATENCY;
            } else {
                prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_DV_NONTUNNEL_LATENCY_PROPERTY;
                latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_DV_NONTUNNEL_LATENCY;
            }
        } else {
            if (tunnel) {
                prop_name = AVSYNC_DV_NETFLIX_TUNNEL_LATENCY_PROPERTY;
                latency_ms = AVSYNC_DV_NETFLIX_TUNNEL_LATENCY;
            } else {
                prop_name = AVSYNC_DV_NETFLIX_NONTUNNEL_LATENCY_PROPERTY;
                latency_ms = AVSYNC_DV_NETFLIX_NONTUNNEL_LATENCY;
            }
        }
    } else {
        if (tunnel) {
            prop_name = AVSYNC_DV_TUNNEL_LATENCY_PROPERTY;
            latency_ms = AVSYNC_DV_TUNNEL_LATENCY;
        } else {
            prop_name = AVSYNC_DV_NONTUNNEL_LATENCY_PROPERTY;
            latency_ms = AVSYNC_DV_NONTUNNEL_LATENCY;
        }
    }
    ret = property_get(prop_name, buf, NULL);
    if (ret > 0) {
        latency_ms = atoi(buf);
    }
    return latency_ms;
}


static int get_ms12_nontunnel_input_latency(audio_format_t input_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        prop_name = AVSYNC_MS12_NONTUNNEL_PCM_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_NONTUNNEL_PCM_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3: {
        prop_name = AVSYNC_MS12_NONTUNNEL_DDP_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_NONTUNNEL_DDP_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC4: {
        prop_name = AVSYNC_MS12_NONTUNNEL_AC4_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_NONTUNNEL_AC4_LATENCY;
        break;
    }
    default:
        break;

    }
    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}


static int get_ms12_tunnel_input_latency(audio_format_t input_format, enum OUT_PORT port) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        /* CVBS output DDP target is [-45, +125]*/
        if ((port == OUTPORT_SPEAKER) || (port == OUTPORT_AUX_LINE)) {
            prop_name = AVSYNC_MS12_TUNNEL_PCM_CVBS_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TUNNEL_PCM_CVBS_LATENCY;
        } else {
            /*for non tunnel ddp2h/heaac case:netflix AL1 case */
            prop_name = AVSYNC_MS12_TUNNEL_PCM_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TUNNEL_PCM_LATENCY;
        }
        break;
    }
    case AUDIO_FORMAT_AC3:
    #if 0
    {
       prop_name = AVSYNC_MS12_TUNNEL_DD_LATENCY_PROPERTY;
       latency_ms = AVSYNC_MS12_TUNNEL_DD_LATENCY;
       break;
    }
    #endif
    case AUDIO_FORMAT_E_AC3: {
        /* CVBS output DDP target is [-45, +125]*/
        if ((port == OUTPORT_SPEAKER) || (port == OUTPORT_AUX_LINE)) {
            prop_name = AVSYNC_MS12_TUNNEL_DDP_CVBS_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TUNNEL_DDP_CVBS_LATENCY;
        }
        /* HDMI or other output, DDP HDMI target is [-45, 0] */
        else {
            prop_name = AVSYNC_MS12_TUNNEL_DDP_HDMI_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TUNNEL_DDP_HDMI_LATENCY;
        }
        break;
    }
    case AUDIO_FORMAT_AC4: {
        /* CVBS/Speaker output, Dolby MS12 AVSync target is [-45, +125] */
        if ((port == OUTPORT_SPEAKER) || (port == OUTPORT_AUX_LINE)) {
            prop_name = AVSYNC_MS12_TUNNEL_AC4_CVBS_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TUNNEL_AC4_CVBS_LATENCY;
        }
        /* HDMI output, Dolby MS12 AVSync target is [-45, 0] */
        else {
            prop_name = AVSYNC_MS12_TUNNEL_AC4_HDMI_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TUNNEL_AC4_HDMI_LATENCY;
        }

        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}


static int get_ms12_netflix_nontunnel_input_latency(audio_format_t input_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    struct aml_audio_device *adev = adev_get_handle();

    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        if (is_TV(adev)) {
            prop_name = AVSYNC_MS12_TV_NETFLIX_NONTUNNEL_PCM_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TV_NETFLIX_NONTUNNEL_PCM_LATENCY;
        } else if (is_SBR_active(adev)) {
            prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_NONTUNNEL_PCM_LATENCY_PROPERTY;
            latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_NONTUNNEL_PCM_LATENCY;
        } else {
            prop_name = AVSYNC_MS12_NETFLIX_NONTUNNEL_PCM_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_NETFLIX_NONTUNNEL_PCM_LATENCY;
        }
        break;
    }
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3: {
        if (is_TV(adev)) {
            prop_name = AVSYNC_MS12_TV_NETFLIX_NONTUNNEL_DDP_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TV_NETFLIX_NONTUNNEL_DDP_LATENCY;
        } else if (is_SBR_active(adev)) {
            prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_NONTUNNEL_DDP_LATENCY_PROPERTY;
            latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_NONTUNNEL_DDP_LATENCY;
        } else {
            prop_name = AVSYNC_MS12_NETFLIX_NONTUNNEL_DDP_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_NETFLIX_NONTUNNEL_DDP_LATENCY;
        }
        break;
    }
    default:
        break;

    }
    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}


static int get_ms12_netflix_tunnel_input_latency(audio_format_t input_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    struct aml_audio_device *adev = adev_get_handle();
    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        /*for non tunnel ddp2h/heaac case:netflix AL1 case */
        if (is_TV(adev)) {
            prop_name = AVSYNC_MS12_TV_NETFLIX_TUNNEL_PCM_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TV_NETFLIX_TUNNEL_PCM_LATENCY;
        } else if (is_SBR_active(adev)) {
            prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_TUNNEL_PCM_LATENCY_PROPERTY;
            latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_TUNNEL_PCM_LATENCY;
        } else {
            prop_name = AVSYNC_MS12_NETFLIX_TUNNEL_PCM_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_NETFLIX_TUNNEL_PCM_LATENCY;
        }
        break;
    }
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3: {
        /*for non tunnel dolby ddp5.1 case:netflix AV1/HDR10/HEVC case*/
        if (is_TV(adev)) {
            prop_name = AVSYNC_MS12_TV_NETFLIX_TUNNEL_DDP_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TV_NETFLIX_TUNNEL_DDP_LATENCY;
        } else if (is_SBR_active(adev)) {
            prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_TUNNEL_DDP_LATENCY_PROPERTY;
            latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_TUNNEL_DDP_LATENCY;
        } else {
            prop_name = AVSYNC_MS12_NETFLIX_TUNNEL_DDP_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_NETFLIX_TUNNEL_DDP_LATENCY;
        }
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}


static int get_ms12_output_latency(audio_format_t output_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (output_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        latency_ms = AVSYNC_MS12_PCM_OUT_LATENCY;
        prop_name = AVSYNC_MS12_PCM_OUT_LATENCY_PROPERTY;
        break;
    }
    case AUDIO_FORMAT_AC3: {
        latency_ms = AVSYNC_MS12_DD_OUT_LATENCY;
        prop_name = AVSYNC_MS12_DD_OUT_LATENCY_PROPERTY;
        break;
    }
    case AUDIO_FORMAT_E_AC3: {
        latency_ms = AVSYNC_MS12_DDP_OUT_LATENCY;
        prop_name = AVSYNC_MS12_DDP_OUT_LATENCY_PROPERTY;
        break;
    }
    case AUDIO_FORMAT_MAT: {
        latency_ms = AVSYNC_MS12_MAT_OUT_LATENCY;
        prop_name = AVSYNC_MS12_MAT_OUT_LATENCY_PROPERTY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    ALOGV("%s output format =0x%x latency ms =%d", __func__, output_format, latency_ms);
    return latency_ms;

}

static int get_ms12_netflix_output_latency(audio_format_t output_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    struct aml_audio_device *adev = adev_get_handle();

    switch (output_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        if (is_SBR_active(adev)) {
             // Default is pcm, no need tuning parameters.
        } else {
            latency_ms = AVSYNC_MS12_NETFLIX_PCM_OUT_LATENCY;
            prop_name = AVSYNC_MS12_NETFLIX_PCM_OUT_LATENCY_PROPERTY;
        }
        break;
    }
    case AUDIO_FORMAT_AC3: {
        if (is_SBR_active(adev)) {
            latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_OUTPUT_DD_LATENCY;
            prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_OUTPUT_DD_LATENCY_PROPERTY;
        } else {
            latency_ms = AVSYNC_MS12_NETFLIX_DD_OUT_LATENCY;
            prop_name = AVSYNC_MS12_NETFLIX_DD_OUT_LATENCY_PROPERTY;
        }
        break;
    }
    case AUDIO_FORMAT_E_AC3: {
        if (is_SBR_active(adev)) {
            latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_OUTPUT_DDP_LATENCY;
            prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_OUTPUT_DDP_LATENCY_PROPERTY;
        } else {
            latency_ms = AVSYNC_MS12_NETFLIX_DDP_OUT_LATENCY;
            prop_name = AVSYNC_MS12_NETFLIX_DDP_OUT_LATENCY_PROPERTY;
        }
        break;
    }
    case AUDIO_FORMAT_MAT: {
        if (is_SBR_active(adev)) {
            latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_OUTPUT_MAT_LATENCY;
            prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_OUTPUT_MAT_LATENCY_PROPERTY;
        } else {
            latency_ms = AVSYNC_MS12_NETFLIX_MAT_OUT_LATENCY;
            prop_name = AVSYNC_MS12_NETFLIX_MAT_OUT_LATENCY_PROPERTY;
        }
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    ALOGV("%s output format =0x%x latency ms =%d", __func__, output_format, latency_ms);
    return latency_ms;

}

static int get_ms12_netflix_soundeffect_latency(bool dap_enable, bool virtual_bass_enable)
{
    char buf[PROPERTY_VALUE_MAX];
    int ret = -1;
    int latency_ms = 0;
    int dap_latency_ms = 0;
    int dap_tuning_ms = 0;
    int cus_latency_ms = 0;
    char *prop_name = NULL;
    struct aml_audio_device *adev = adev_get_handle();

    if (dap_enable) {
        if (virtual_bass_enable) {
            dap_latency_ms = 1536/48;
        } else {
            dap_latency_ms = 512/48;
        }

        if (is_SBR_active(adev)) {
            dap_tuning_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_DAP_TUNING_LATENCY;
            prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_DAP_TUNING_LATENCY_PROPERTY;
        }
        if (prop_name) {
            memset(buf, 0, sizeof(buf));
            ret = property_get(prop_name, buf, NULL);
            if (ret > 0) {
                dap_tuning_ms = atoi(buf);
            }
        }
    }

    // customer soundeffect process
    if (is_SBR_active(adev)) {
        cus_latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_CUS_PROCESS_LATENCY;
        prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_CUS_PROCESS_LATENCY_PROPERTY;

        if (prop_name) {
            memset(buf, 0, sizeof(buf));
            ret = property_get(prop_name, buf, NULL);
            if (ret > 0) {
                cus_latency_ms = atoi(buf);
            }
        }
    }

    latency_ms = dap_latency_ms + dap_tuning_ms + cus_latency_ms;
    ALOGV("%s dap_enable=%d, virtual_bass_enable=%d, dap_latency_ms=%d, dap_tuning_ms=%d cus_latency_ms=%d, latency_ms=%d",
        __func__, dap_enable, virtual_bass_enable, dap_latency_ms, dap_tuning_ms, cus_latency_ms, latency_ms);
    return latency_ms;
}


int get_ms12_netflix_port_latency( enum OUT_PORT port, audio_format_t output_format)
{
    int latency_ms = 0;
    int ret = 0;
    char *prop_name = NULL;
    char buf[PROPERTY_VALUE_MAX] = {'\0'};

    switch (port)  {
        case OUTPORT_HDMI_ARC:
            if (output_format == AUDIO_FORMAT_AC3)
                latency_ms = AVSYNC_MS12_NETFLIX_HDMI_ARC_OUT_DD_LATENCY;
            else if (output_format == AUDIO_FORMAT_E_AC3)
                latency_ms = AVSYNC_MS12_NETFLIX_HDMI_ARC_OUT_DDP_LATENCY;
            else if (output_format == AUDIO_FORMAT_MAT)
                latency_ms = AVSYNC_MS12_NETFLIX_HDMI_ARC_OUT_MAT_LATENCY;
            else
                latency_ms = AVSYNC_MS12_NETFLIX_HDMI_ARC_OUT_PCM_LATENCY;

            prop_name = AVSYNC_MS12_NETFLIX_HDMI_ARC_OUT_LATENCY_PROPERTY;
            break;
        case OUTPORT_HDMI:
            latency_ms = AVSYNC_MS12_NETFLIX_HDMI_OUT_LATENCY; //default value as 0
            prop_name = AVSYNC_MS12_NETFLIX_HDMI_LATENCY_PROPERTY;
            break;
        case OUTPORT_SPEAKER:
        case OUTPORT_AUX_LINE:
        case OUTPORT_HEADPHONE:
            latency_ms = AVSYNC_MS12_NETFLIX_SPEAKER_LATENCY;
            prop_name = AVSYNC_MS12_NETFLIX_SPEAKER_LATENCY_PROPERTY;
            break;
        default :
            break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    ALOGV("%s output format =0x%x latency ms =%d", __func__, output_format, latency_ms);

    return latency_ms;
}

int get_ms12_port_latency(enum OUT_PORT port, audio_format_t output_format, bool is_eARC, bool is_tunnel)
{
    int attend_type, earc_latency;
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    bool is_speaker_mute = aml_get_speaker_mute_status();

    switch (port)  {

        case OUTPORT_HDMI_ARC:
        {
            if (is_eARC) {
                if (output_format == AUDIO_FORMAT_AC3) {
                    latency_ms = AVSYNC_MS12_HDMI_EARC_OUT_DD_LATENCY;
                    prop_name = AVSYNC_MS12_HDMI_EARC_OUT_DD_LATENCY_PROPERTY;
                }
                else if (output_format == AUDIO_FORMAT_E_AC3) {
                    latency_ms = AVSYNC_MS12_HDMI_EARC_OUT_DDP_LATENCY;
                    prop_name = AVSYNC_MS12_HDMI_EARC_OUT_DDP_LATENCY_PROPERTY;
                }
                else if (output_format == AUDIO_FORMAT_MAT) {
                    latency_ms = AVSYNC_MS12_HDMI_EARC_OUT_MAT_LATENCY;
                    prop_name = AVSYNC_MS12_HDMI_EARC_OUT_MAT_LATENCY_PROPERTY;
                }
                else {
                    latency_ms = AVSYNC_MS12_HDMI_EARC_OUT_PCM_LATENCY;
                    prop_name = AVSYNC_MS12_HDMI_EARC_OUT_PCM_LATENCY_PROPERTY;
                }
            }
            else {
                if (is_tunnel) {
                    if (output_format == AUDIO_FORMAT_AC3) {
                        latency_ms = AVSYNC_MS12_HDMI_ARC_OUT_DD_LATENCY;
                        prop_name = AVSYNC_MS12_HDMI_ARC_OUT_DD_LATENCY_PROPERTY;
                    }
                    else if (output_format == AUDIO_FORMAT_E_AC3) {
                        latency_ms = AVSYNC_MS12_HDMI_ARC_OUT_DDP_LATENCY;
                        prop_name = AVSYNC_MS12_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY;
                    }
                    else {
                        latency_ms = AVSYNC_MS12_HDMI_ARC_OUT_PCM_LATENCY;
                        prop_name = AVSYNC_MS12_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY;
                    }
                }
                else {
                    if (output_format == AUDIO_FORMAT_AC3) {
                        latency_ms = AVSYNC_MS12_NONTUNNEL_HDMI_ARC_OUT_DD_LATENCY;
                        prop_name = AVSYNC_MS12_NONTUNNEL_HDMI_ARC_OUT_DD_LATENCY_PROPERTY;
                    }
                    else if (output_format == AUDIO_FORMAT_E_AC3) {
                        latency_ms = AVSYNC_MS12_NONTUNNEL_HDMI_ARC_OUT_DDP_LATENCY;
                        prop_name = AVSYNC_MS12_NONTUNNEL_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY;
                    }
                    else {
                        latency_ms = AVSYNC_MS12_NONTUNNEL_HDMI_ARC_OUT_PCM_LATENCY;
                        prop_name = AVSYNC_MS12_NONTUNNEL_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY;
                    }
                }
            }
            break;
        }
        case OUTPORT_HDMI:
            latency_ms = AVSYNC_MS12_HDMI_OUT_LATENCY; //default value as 0
            prop_name = AVSYNC_MS12_HDMI_LATENCY_PROPERTY;
            break;
        case OUTPORT_SPEAKER:
        case OUTPORT_AUX_LINE:
            if (is_speaker_mute) {
                latency_ms = AVSYNC_MS12_SPDIF_OUT_LATENCY;
                prop_name = AVSYNC_MS12_SPDIF_OUT_LATENCY_PROPERTY;
            } else {
                latency_ms = AVSYNC_MS12_SPEAKER_LATENCY;
                prop_name = AVSYNC_MS12_SPEAKER_LATENCY_PROPERTY;
            }
        default :
            break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    ALOGV("%s output format =0x%x latency ms =%d", __func__, output_format, latency_ms);

    return latency_ms;
}

static int get_ms12_nontunnel_latency_offset(enum OUT_PORT port
    , audio_format_t input_format
    , audio_format_t output_format
    , bool is_netflix
    , device_type_t platform_type __unused
    , bool is_eARC)
{
    int latency_ms = 0;
    int input_latency_ms = 0;
    int output_latency_ms = 0;
    int port_latency_ms = 0;
    bool is_tunnel = false;
    int sound_effect_latency_ms = 0;
    struct aml_audio_device *adev = adev_get_handle();
    bool dap_enable = is_audio_postprocessing_add_dolbyms12_dap(adev);
    bool virtual_bass_enable = get_ms12_dap_virtual_bass_enable();

    if (is_netflix) {
        input_latency_ms  = get_ms12_netflix_nontunnel_input_latency(input_format);
        output_latency_ms = get_ms12_netflix_output_latency(output_format);
        if (adev->bDVEnable && !is_TV(adev)) {
            output_latency_ms += get_sink_dv_latency_offset(false, true);
        }
        if (port == OUTPORT_SPEAKER) {
            sound_effect_latency_ms = get_ms12_netflix_soundeffect_latency(dap_enable, virtual_bass_enable);
        }
        port_latency_ms = get_ms12_netflix_port_latency(port, output_format);
    } else {
        input_latency_ms  = get_ms12_nontunnel_input_latency(input_format);
        output_latency_ms = get_ms12_output_latency(output_format);
        if (adev->compensate_video_enable) {
            output_latency_ms = 0;
        }
        port_latency_ms   = get_ms12_port_latency(port, output_format, is_eARC, is_tunnel);
    }
    latency_ms = input_latency_ms + output_latency_ms + port_latency_ms + sound_effect_latency_ms;
    ALOGV("%s total latency =%d ms in=%d ms out=%d ms port=%d ms sound_effect=%d ms", __func__,
        latency_ms, input_latency_ms, output_latency_ms, port_latency_ms, sound_effect_latency_ms);
    return latency_ms;
}

static int get_ms12_nontunel_deepbuffer_latency_offset(bool is_netflix)
{
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;

    /*non tunnel pcm case*/
    if (is_netflix) {
        prop_name = AVSYNC_MS12_NETFLIX_NONTUNNEL_DEEPBUFFER_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_NETFLIX_NONTUNNEL_DEEPBUFFER_LATENCY;
    } else {
        prop_name = AVSYNC_MS12_NONTUNNEL_DEEPBUFFER_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_NONTUNNEL_DEEPBUFFER_LATENCY;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    return latency_ms;
}


static int get_ms12_tunnel_latency_offset(enum OUT_PORT port
    , audio_format_t input_format
    , audio_format_t output_format
    , bool is_netflix
    , bool is_output_ddp_atmos
    , device_type_t platform_type __unused
    , bool is_eARC)
{
    int latency_ms = 0;
    int input_latency_ms = 0;
    int output_latency_ms = 0;
    int port_latency_ms = 0;
    int sound_effect_latency_ms = 0;
    int is_dv = getprop_bool(MS12_OUTPUT_5_1_DDP); /* suppose that Dolby Vision is under test */
    bool is_tunnel = true;
    struct aml_audio_device *adev = adev_get_handle();
    bool dap_enable = is_audio_postprocessing_add_dolbyms12_dap(adev);
    bool virtual_bass_enable = get_ms12_dap_virtual_bass_enable();


    //ALOGD("%s  prot:%d, is_netflix:%d, input_format:0x%x, output_format:0x%x", __func__,
    //            port, is_netflix, input_format, output_format);
    if (is_netflix) {
        input_latency_ms  = get_ms12_netflix_tunnel_input_latency(input_format);
        output_latency_ms = get_ms12_netflix_output_latency(output_format);
        if ((output_format == AUDIO_FORMAT_E_AC3) || (output_format == AUDIO_FORMAT_AC3)) {
            output_latency_ms += AVSYNC_MS12_NETFLIX_DDP_OUT_TUNNEL_TUNING;
        }
        port_latency_ms   = get_ms12_netflix_port_latency(port, output_format);
        if (port == OUTPORT_SPEAKER) {
            sound_effect_latency_ms = get_ms12_netflix_soundeffect_latency(dap_enable, virtual_bass_enable);
         }
    } else {
        /*
         * TODO:
         * found the different between DDP_JOC and DDP,
         * DDP_JOC will has more 32m(in MS12 pipeline) and 20ms(get_ms12_atmos_latency_offset)
         * and one 32ms in SPDIF Encoder delay for DDP_JOC, but test result is 16ms.
         * so, better to dig into this hard coding part.
         */
        input_latency_ms  = get_ms12_tunnel_input_latency(input_format, port)
                            + is_output_ddp_atmos * AVSYNC_MS12_TUNNEL_DIFF_DDP_JOC_VS_DDP_LATENCY;
        if (is_dv) {
            input_latency_ms += get_ms12_dv_tunnel_input_latency(input_format);
        }
        output_latency_ms = get_ms12_output_latency(output_format);
        if (is_dv) {
            output_latency_ms += get_ms12_dv_tunnel_output_latency(output_format);
        }
        port_latency_ms   = get_ms12_port_latency(port, output_format, is_eARC, is_tunnel);
    }
    latency_ms = input_latency_ms + output_latency_ms + port_latency_ms + sound_effect_latency_ms;
    ALOGV("%s total latency =%d ms in=%d ms out=%d ms(is output ddp_atmos %d) port=%d ms sound_effect=%d ms", __func__,
        latency_ms, input_latency_ms, output_latency_ms, is_output_ddp_atmos, port_latency_ms, sound_effect_latency_ms);
    return latency_ms;
}


int get_ms12_atmos_latency_offset(bool tunnel, bool is_netflix)
{
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    struct aml_audio_device *adev = adev_get_handle();

    if (is_netflix) {
        if (is_SBR_active(adev)) {
            if (tunnel) {
                /*tunnel atmos case*/
                prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_TUNNEL_ATMOS_LATENCY_PROPERTY;
                latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_TUNNEL_ATMOS_LATENCY;
            } else {
                /*non tunnel atmos case*/
                prop_name = AVSYNC_SOUNDBAR_MS12_NETFLIX_NONTUNNEL_ATMOS_LATENCY_PROPERTY;
                latency_ms = AVSYNC_SOUNDBAR_MS12_NETFLIX_NONTUNNEL_ATMOS_LATENCY;
            }
        } else {
            if (tunnel) {
                /*tunnel atmos case*/
                prop_name = AVSYNC_MS12_NETFLIX_TUNNEL_ATMOS_LATENCY_PROPERTY;
                latency_ms = AVSYNC_MS12_NETFLIX_TUNNEL_ATMOS_LATENCY;
            } else {
               /*non tunnel atmos case*/
                prop_name = AVSYNC_MS12_NETFLIX_NONTUNNEL_ATMOS_LATENCY_PROPERTY;
                latency_ms = AVSYNC_MS12_NETFLIX_NONTUNNEL_ATMOS_LATENCY;
           }
       }
    } else {
        if (tunnel) {
            /*tunnel atmos case*/
            prop_name = AVSYNC_MS12_TUNNEL_ATMOS_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TUNNEL_ATMOS_LATENCY;
        } else {
            /*non tunnel atmos case*/
            prop_name = AVSYNC_MS12_NONTUNNEL_ATMOS_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_NONTUNNEL_ATMOS_LATENCY;
        }
    }
    ret = property_get(prop_name, buf, NULL);
    if (ret > 0) {
        latency_ms = atoi(buf);
    }
    return latency_ms;
}


int get_ms12_bypass_latency_offset(bool tunnel, bool is_netflix)
{
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;

    if (is_netflix) {
        if (tunnel) {
            /*tunnel case*/
            prop_name = AVSYNC_MS12_NETFLIX_TUNNEL_BYPASS_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_NETFLIX_TUNNEL_BYPASS_LATENCY;
        } else {
            /*non tunnel case*/
            prop_name = AVSYNC_MS12_NETFLIX_NONTUNNEL_BYPASS_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_NETFLIX_NONTUNNEL_BYPASS_LATENCY;
        }
    } else {
        if (tunnel) {
            /*tunnel atmos case*/
            prop_name = AVSYNC_MS12_TUNNEL_BYPASS_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_TUNNEL_BYPASS_LATENCY;
        } else {
            /*non tunnel atmos case*/
            prop_name = AVSYNC_MS12_NONTUNNEL_BYPASS_LATENCY_PROPERTY;
            latency_ms = AVSYNC_MS12_NONTUNNEL_BYPASS_LATENCY;
        }
    }
    ret = property_get(prop_name, buf, NULL);
    if (ret > 0) {
        latency_ms = atoi(buf);
    }
    return latency_ms;
}


uint32_t out_get_ms12_latency_frames(struct audio_stream_out *stream)
{
    struct aml_stream_out *hal_out = (struct aml_stream_out *)stream;

    if (hal_out == NULL)  {
        ALOGI("hal_out null return 0");
        return 0;
    }

    snd_pcm_sframes_t frames = 0;
    struct snd_pcm_status status;
    uint32_t whole_latency_frames;
    int ret = 0;
    struct aml_audio_device *adev = hal_out->dev;
    struct aml_stream_out *ms12_out = NULL;
    struct pcm_config *config = &adev->ms12_config;
    int mul = 1;

    if (continuous_mode(adev) && adev->ms12.dolby_ms12_enable) {
        ms12_out = adev->ms12_out;
    } else {
        ms12_out = hal_out;
    }

    if (ms12_out == NULL) {
        return 0;
    }

    if (adev->out_device & AUDIO_DEVICE_OUT_ALL_A2DP) {
        return a2dp_out_get_latency(adev) * MM_FULL_POWER_SAMPLING_RATE / 1000;
    }

    whole_latency_frames = config->start_threshold;
    if (!ms12_out->pcm || !pcm_is_ready(ms12_out->pcm)) {
        return whole_latency_frames / mul;
    }

    ret = pcm_ioctl(ms12_out->pcm, SNDRV_PCM_IOCTL_STATUS, &status);
    if (ret < 0) {
        return whole_latency_frames / mul;
    }
    if (status.state != PCM_STATE_RUNNING && status.state != PCM_STATE_DRAINING) {
        return whole_latency_frames / mul;
    }

    ret = pcm_ioctl(ms12_out->pcm, SNDRV_PCM_IOCTL_DELAY, &frames);
    if (ret < 0) {
        return whole_latency_frames / mul;
    }
    ALOGV("%s frames =%ld mul=%d", __func__, frames, mul);
    return frames / mul;
}

uint32_t out_get_ms12_bitstream_latency_ms(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int bitstream_delay_ms = 0;
    struct bitstream_out_desc *bitstream_out = &ms12->bitstream_out[BITSTREAM_OUTPUT_A];
    int ret = 0;

    bitstream_delay_ms = aml_audio_spdifout_get_delay(bitstream_out->spdifout_handle);

    return bitstream_delay_ms;
}



static int aml_audio_output_ddp_atmos(struct audio_stream_out *stream)
{
    int ret = 0;
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct aml_arc_hdmi_desc* hdmi_descs = get_arc_hdmi_cap(adev);

    bool is_atmos_supported = is_platform_supported_ddp_atmos(
                            hdmi_descs->ddp_fmt.atmos_supported
                            , adev->cur_out_devices
                            , is_TV(adev));

    bool is_ddp_atmos_format = (out->hal_format == AUDIO_FORMAT_E_AC3_JOC);

    return (is_atmos_supported && is_ddp_atmos_format);
}

static int get_ms12_tunnel_video_delay(struct audio_stream_out *stream) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;

    // temporary only netflix use get_media_video_delay, avoid affecting
    // current Dolby-AVsync or XTS certification result.
    if (adev->is_netflix) {
        latency_ms = get_media_video_delay(&adev->alsa_mixer);
    } else {
        prop_name= AVSYNC_MS12_TUNNEL_VIDEO_DELAY_PROPERTY;
        latency_ms = AVSYNC_MS12_TUNNEL_VIDEO_DELAY;
        if (prop_name) {
            ret = property_get(prop_name, buf, NULL);
            if (ret > 0) {
                latency_ms = atoi(buf);
            }
        }
    }
    ALOGV("%s latency ms =%d, is_netflix=%d", __func__, latency_ms, adev->is_netflix);
    return latency_ms;

}


int aml_audio_get_ms12_tunnel_latency(struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct dolby_ms12_dec_desc *ms12_dec = out->ms12_dec_handle;
    int32_t latency_frames = 0;
    int32_t tuning_delay = 0;
    int32_t atmos_tuning_delay = 0;
    int32_t bypass_delay = 0;
    int32_t video_delay = 0;
    int32_t dv_delay = 0;
    bool is_output_ddp_atmos = aml_audio_output_ddp_atmos(stream);
    device_type_t platform_type = STB;
    bool is_earc = is_earc_connected(adev);

    int hdmi_delay = 0;

    /* if the hdmi delay is negative, then we should render video later.
     * this latency calculation need a positive value to delay video,
     * so we change the hdmi delay to positive one
     */
    //adev->active_outport == OUTPORT_HDMI
    //active_outport has been removed, replace it with cur_out_devices
    if (get_output_by_devices(adev->cur_out_devices) == OUTPORT_HDMI &&
        adev->avsync_compensate_delay_ms < 0) {
        hdmi_delay = abs(adev->avsync_compensate_delay_ms) * 48;
    }

    /*compensate the avr delay for raw data*/
    if (adev->b_ott_tv_arc_connected &&
        get_output_by_devices(adev->cur_out_devices) == OUTPORT_HDMI &&
        adev->sink_format != AUDIO_FORMAT_PCM_16_BIT) {
        hdmi_delay += abs(adev->arc_delay_ms) * 48;
    }


    if (is_STB(adev)) {
        platform_type = STB;
    }
    else if (is_TV(adev)) {
        platform_type = TV;
    }
    else if (is_SBR(adev)) {
        if (is_SBR_active(adev)) {
            platform_type = SBR;
        } else {
            platform_type = STB;
        }
    }

    //ALOGI("latency_frames =%d", latency_frames);
    tuning_delay = get_ms12_tunnel_latency_offset(get_output_by_devices(adev->cur_out_devices),
                                                      out->hal_internal_format,
                                                      adev->sink_format,
                                                      adev->is_netflix,
                                                      is_output_ddp_atmos,
                                                      platform_type,
                                                      is_earc) * 48;

    if (ms12_dec->is_dolby_atmos || adev->atoms_lock_flag) {
        /*
         * In DV AV sync, the ATMOS(DDP_JOC) item, it will add atmos_tuning_delay into the latency_frames.
         * If other case choose an diff value, here separate by is_netflix.
         */
        atmos_tuning_delay = get_ms12_atmos_latency_offset(true, adev->is_netflix) * 48;
    }

    if (ms12_dec->is_bypass_ms12) {
        bypass_delay = get_ms12_bypass_latency_offset(true, adev->is_netflix) * 48;
    }

    if (is_TV(adev)) {
        video_delay = get_ms12_tunnel_video_delay(stream) * 48;
    } else if (adev->bDVEnable) {
        dv_delay = get_sink_dv_latency_offset(true, adev->is_netflix) * 48;
    }

    latency_frames = tuning_delay + atmos_tuning_delay + bypass_delay - video_delay + dv_delay + hdmi_delay;

    ALOGV("latency frames =%d tuning delay=%d ms atmos =%d ms video delay %d ms dv_delay %d ms",
        latency_frames, tuning_delay / 48, atmos_tuning_delay / 48, video_delay / 48, dv_delay / 48);
    return latency_frames;
}

int get_nonms12_port_latency(enum OUT_PORT port, audio_format_t output_format, bool is_eARC)
{
    int attend_type, earc_latency;
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (port) {
        case OUTPORT_HDMI_ARC:
        {
            if (is_eARC) {
                if (output_format == AUDIO_FORMAT_AC3) {
                    latency_ms = AVSYNC_NONMS12_HDMI_EARC_OUT_DD_LATENCY;
                    prop_name = AVSYNC_NONMS12_HDMI_EARC_OUT_DD_LATENCY_PROPERTY;
                }
                else if (output_format == AUDIO_FORMAT_E_AC3) {
                    latency_ms = AVSYNC_NONMS12_HDMI_EARC_OUT_DDP_LATENCY;
                    prop_name = AVSYNC_NONMS12_HDMI_EARC_OUT_DDP_LATENCY_PROPERTY;
                }
                else if (output_format == AUDIO_FORMAT_MAT) {
                    latency_ms = AVSYNC_NONMS12_HDMI_EARC_OUT_MAT_LATENCY;
                    prop_name = AVSYNC_NONMS12_HDMI_EARC_OUT_MAT_LATENCY_PROPERTY;
                }
                else {
                    latency_ms = AVSYNC_NONMS12_HDMI_EARC_OUT_PCM_LATENCY;
                    prop_name = AVSYNC_NONMS12_HDMI_EARC_OUT_PCM_LATENCY_PROPERTY;
                }
            }
            else {
                if (output_format == AUDIO_FORMAT_AC3) {
                    latency_ms = AVSYNC_NONMS12_HDMI_ARC_OUT_DD_LATENCY;
                    prop_name = AVSYNC_NONMS12_HDMI_ARC_OUT_DD_LATENCY_PROPERTY;
                }
                else if (output_format == AUDIO_FORMAT_E_AC3) {
                    latency_ms = AVSYNC_NONMS12_HDMI_ARC_OUT_DDP_LATENCY;
                    prop_name = AVSYNC_NONMS12_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY;
                }
                else {
                    latency_ms = AVSYNC_NONMS12_HDMI_ARC_OUT_PCM_LATENCY;
                    prop_name = AVSYNC_NONMS12_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY;
                }
            }
            break;
        }
        case OUTPORT_HDMI:
        {
            if (output_format == AUDIO_FORMAT_AC3) {
                latency_ms = AVSYNC_NONMS12_HDMI_OUT_DD_LATENCY;
                prop_name = AVSYNC_NONMS12_HDMI_OUT_DD_LATENCY_PROPERTY;
            }
            else if (output_format == AUDIO_FORMAT_E_AC3) {
                latency_ms = AVSYNC_NONMS12_HDMI_OUT_DDP_LATENCY;
                prop_name = AVSYNC_NONMS12_HDMI_OUT_DDP_LATENCY_PROPERTY;
            }
            else {
                latency_ms = AVSYNC_NONMS12_HDMI_OUT_PCM_LATENCY;
                prop_name = AVSYNC_NONMS12_HDMI_OUT_PCM_LATENCY_PROPERTY;
            }
            break;
        }
        case OUTPORT_SPEAKER:
        case OUTPORT_AUX_LINE:
        {
            latency_ms = AVSYNC_NONMS12_SPEAKER_LATENCY;
            prop_name = AVSYNC_NONMS12_SPEAKER_LATENCY_PROPERTY;
            break;
        }
        default :
            break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}


static int get_nonms12_netflix_tunnel_input_latency(audio_format_t input_format, unsigned int input_channel_count, device_type_t platform_type) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        /*for tunnel ddp2h/heaac case:netflix AL1 case */
        if (input_channel_count > 2) {
            if (platform_type == TV) {
                latency_ms = AVSYNC_NONMS12_TV_NETFLIX_TUNNEL_MULTICH_PCM_LATENCY;
            } else {
                latency_ms = AVSYNC_NONMS12_NETFLIX_TUNNEL_MULTICH_PCM_LATENCY;
            }
            prop_name = AVSYNC_NONMS12_NETFLIX_TUNNEL_MULTICH_PCM_LATENCY_PROPERTY;
        } else {
            if (platform_type == TV) {
                latency_ms = AVSYNC_NONMS12_TV_NETFLIX_TUNNEL_PCM_LATENCY;
            } else {
                latency_ms = AVSYNC_NONMS12_NETFLIX_TUNNEL_PCM_LATENCY;
            }
            prop_name = AVSYNC_NONMS12_NETFLIX_TUNNEL_PCM_LATENCY_PROPERTY;
        }
        break;
    }
    case AUDIO_FORMAT_AC3:
    case AUDIO_FORMAT_E_AC3: {
        /*for tunnel dolby ddp5.1 case:netflix AV1/HDR10/HEVC case*/
        if (platform_type == TV) {
            latency_ms = AVSYNC_NONMS12_TV_NETFLIX_TUNNEL_DDP_LATENCY;
        } else {
            latency_ms = AVSYNC_NONMS12_NETFLIX_TUNNEL_DDP_LATENCY;
        }
        prop_name = AVSYNC_NONMS12_NETFLIX_TUNNEL_DDP_LATENCY_PROPERTY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}

static int get_nonms12_tunnel_input_latency(audio_format_t input_format, device_type_t platform_type, enum OUT_PORT port) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    if (platform_type == STB) {
        switch (input_format) {
        case AUDIO_FORMAT_PCM_16_BIT: {
            prop_name = AVSYNC_NONMS12_TUNNEL_STB_PCM_LATENCY_PROPERTY;
            latency_ms = AVSYNC_NONMS12_TUNNEL_STB_PCM_LATENCY;
            break;
        }
        case AUDIO_FORMAT_AC3:
        case AUDIO_FORMAT_E_AC3:
        /* CVBS output DDP target is [-30, +100]*/
        if ((port == OUTPORT_SPEAKER) || (port == OUTPORT_AUX_LINE)) {
            prop_name = AVSYNC_NONMS12_TUNNEL_STB_DDP_CVBS_LATENCY_PROPERTY;
            latency_ms = AVSYNC_NONMS12_TUNNEL_STB_DDP_CVBS_LATENCY;
        }
        /* HDMI or other output, DDP HDMI target is [-45, 0] */
        else {
            prop_name = AVSYNC_NONMS12_TUNNEL_STB_DDP_HDMI_LATENCY_PROPERTY;
            latency_ms = AVSYNC_NONMS12_TUNNEL_STB_DDP_HDMI_LATENCY;
        }

        default:
            break;
        }
    }
    else if (platform_type == TV) {
        switch (input_format) {
        case AUDIO_FORMAT_PCM_16_BIT: {
            prop_name = AVSYNC_NONMS12_TUNNEL_TV_PCM_LATENCY_PROPERTY;
            latency_ms = AVSYNC_NONMS12_TUNNEL_TV_PCM_LATENCY;
            break;
        }
        case AUDIO_FORMAT_AC3:
        case AUDIO_FORMAT_E_AC3: {
            prop_name = AVSYNC_NONMS12_TUNNEL_TV_DDP_LATENCY_PROPERTY;
            latency_ms = AVSYNC_NONMS12_TUNNEL_TV_DDP_LATENCY;
            break;
        }
        default:
            break;
        }
    }
    else if (platform_type == SBR) {
        ;/* TODO */
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}

static int get_nonms12_output_latency(audio_format_t output_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (output_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        latency_ms = AVSYNC_NONMS12_STB_PCMOUT_LATENCY;
        prop_name = AVSYNC_NONMS12_STB_PCMOUT_LATENCY_PROPERTY;
        break;
    }
    case AUDIO_FORMAT_AC3: {
        latency_ms = AVSYNC_NONMS12_STB_DDOUT_LATENCY;
        prop_name = AVSYNC_NONMS12_STB_DDOUT_LATENCY_PROPERTY;
        break;
    }
    case AUDIO_FORMAT_E_AC3: {
        latency_ms = AVSYNC_NONMS12_STB_DDPOUT_LATENCY;
        prop_name = AVSYNC_NONMS12_STB_DDPOUT_LATENCY_PROPERTY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    ALOGV("%s output format =0x%x latency ms =%d", __func__, output_format, latency_ms);
    return latency_ms;
}


static int get_nonms12_tunnel_latency_offset(enum OUT_PORT port
    , audio_format_t input_format
    , unsigned int input_channel_count
    , audio_format_t output_format
    , bool is_netflix
    , bool is_output_ddp_atmos
    , device_type_t platform_type
    , bool is_eARC)
{
    int latency_ms = 0;
    int input_latency_ms = 0;
    int output_latency_ms = 0;
    int port_latency_ms = 0;
    int is_dv = getprop_bool(MS12_OUTPUT_5_1_DDP); /* suppose that Dolby Vision is under test */
    struct aml_audio_device *adev = adev_get_handle();

    if (is_netflix) {
        input_latency_ms  = get_nonms12_netflix_tunnel_input_latency(input_format, input_channel_count, platform_type);
        if (is_TV(adev)) {
            output_latency_ms = aml_audio_get_netflix_port_latency(port, output_format);
        }
    } else {
        input_latency_ms  = get_nonms12_tunnel_input_latency(input_format, platform_type, port);
        port_latency_ms   = get_nonms12_port_latency(port, output_format, is_eARC);
        if (platform_type == STB) {
            output_latency_ms = get_nonms12_output_latency(output_format);
        }
        if (is_dv) {
            input_latency_ms += get_nonms12_dv_tunnel_input_latency(input_format);
        }

        if (is_dv) {
            output_latency_ms += get_nonms12_dv_tunnel_output_latency(output_format);
        }
    }

    latency_ms = input_latency_ms + output_latency_ms + port_latency_ms;
    ALOGV("%s total latency =%d, ms in=%d ms out=%d ms(is output ddp_atmos %d) port=%d ms", __func__,
       latency_ms, input_latency_ms, output_latency_ms, is_output_ddp_atmos, port_latency_ms);

    return latency_ms;
}

int aml_audio_get_nonms12_tunnel_latency(struct audio_stream_out * stream, audio_format_t output_format)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int32_t tuning_delay = 0;
    int32_t alsa_delay = 0;
    int latency_frames = 0;
    bool is_output_ddp_atmos = aml_audio_output_ddp_atmos(stream);
    device_type_t platform_type = STB;
    bool is_earc = is_earc_connected(adev);

    if (is_STB(adev)) {
        platform_type = STB;
    }
    else if (is_TV(adev)) {
        platform_type = TV;
    }
    else if (is_SBR_active(adev)) {
        platform_type = SBR;
    }

    //alsa_delay = (int32_t)out_get_latency(stream);
    //ALOGI("latency_frames =%d", latency_frames);
    tuning_delay = get_nonms12_tunnel_latency_offset(get_output_by_devices(adev->cur_out_devices),
                                                      out->hal_internal_format,
                                                      out->hal_ch,
                                                      output_format,
                                                      adev->is_netflix,
                                                      is_output_ddp_atmos,
                                                      platform_type,
                                                      is_earc) * 48;

    latency_frames = alsa_delay + tuning_delay;

    ALOGV("latency frames =%d, alsa delay=%d ms  tuning delay=%d ms",
        latency_frames, alsa_delay / 48, tuning_delay / 48);

    return latency_frames;
}

static int get_ms12_tunnel_xts_latency(void) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int latency_ms = 0;
    char *prop_name = NULL;
    int ret = 0;

    prop_name = "vendor.media.audio.hal.ms12.xts.tunnel.pcm";
    latency_ms = 0;


    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    //ALOGI("%s %d  latency_ms:%d", __func__, __LINE__, latency_ms);
    return latency_ms;
}

int aml_audio_get_ms12_nontunel_tune_latency(const struct audio_stream_out * stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    bool is_output_ddp_atmos = aml_audio_output_ddp_atmos((struct audio_stream_out *)stream);
    bool is_earc = is_earc_connected(adev);
    bool b_deepbuffer = (out->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER);
    int frame_latency = 0;
    device_type_t platform_type = STB;
    bool is_netflix = adev->is_netflix;

    if (eDolbyMS12Lib != adev->dolby_lib_type) {
        return 0;
    }

    if (is_STB(adev)) {
        platform_type = STB;
    } else if (is_TV(adev)) {
        platform_type = TV;
    } else if (is_SBR_active(adev)) {
        platform_type = SBR;
    }

    if (out->ms12_dec_handle->is_bypass_ms12) {
        frame_latency = get_ms12_bypass_latency_offset(false, is_netflix) * 48;
        if (adev->bDVEnable && !is_TV(adev)) {
            frame_latency += get_sink_dv_latency_offset(false, is_netflix) * 48;
        }
    } else {
        frame_latency = get_ms12_nontunnel_latency_offset(get_output_by_devices(adev->cur_out_devices),
                                                           out->hal_internal_format,
                                                           adev->sink_format,
                                                           is_netflix,
                                                           platform_type,
                                                           is_earc) * 48;
        if (out->is_normal_pcm && b_deepbuffer) {
            frame_latency += get_ms12_nontunel_deepbuffer_latency_offset(is_netflix) * 48;
        }
        if (out->ms12_dec_handle->is_dolby_atmos || adev->atoms_lock_flag) {
            if (is_netflix && platform_type == TV && adev->sink_format == AUDIO_FORMAT_E_AC3) {
                // Do not add atmos latency, reduce apk atmos bitstream underrun probability
            } else {
                frame_latency += get_ms12_atmos_latency_offset(false, is_netflix) * 48;
            }
        }
    }

    return frame_latency;
}

int aml_audio_get_ms12_presentation_position(const struct audio_stream_out *stream, uint64_t *frames, struct timespec *timestamp)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int frame_latency = 0, timems_latency = 0;
    bool b_raw_in = false;
    bool b_raw_out = false;
    uint64_t frames_written_hw = out->last_frames_position;
    bool b_deepbuffer = (out->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER);

    if (frames_written_hw == 0) {
        ALOGV("%s(), not ready yet", __func__);
        return -EINVAL;
    }
    *frames = frames_written_hw;
    *timestamp = out->lasttimestamp;

    {
        if (direct_continuous((struct audio_stream_out *)stream) && adev->ms12.dolby_ms12_enable && out->ms12_dec_handle) {
            pthread_mutex_lock(&adev->ms12.main_apts_update_lock);
            clock_gettime(CLOCK_MONOTONIC, timestamp);
            frames_written_hw = out->ms12_dec_handle->last_frames_position;
            if (out->ms12_dec_handle->ms12_position_update) {
                int diff_ms = calc_time_interval_us(&out->ms12_dec_handle->timestamp, timestamp) / MSEC_PER_SEC;
                /*ms12 output is 32ms, so we only consider the below drift*/
                if (adev->debug_flag) {
                    ALOGI(" original frames:%"PRIu64" , diff_ms = %d", *frames, diff_ms);
                }
                if (diff_ms > 32) {
                    diff_ms = 32;
                }
                frames_written_hw += diff_ms * (MM_FULL_POWER_SAMPLING_RATE/MSEC_PER_SEC);
            }
            pthread_mutex_unlock(&adev->ms12.main_apts_update_lock);

        }

        if (out->is_normal_pcm && adev->ms12.dolby_ms12_enable) {
            if (b_deepbuffer) {
                frames_written_hw = adev->ms12.deep_buf_audio_frame_pos;
                //add this code for Youtube test.
                if (adev->ms12.deep_buf_write2alsa_status) {
                    *timestamp = adev->ms12.deep_buf_audio_timestamp;
                }
            } else {
                frames_written_hw = adev->ms12.sys_audio_frame_pos;
                //add this code for Youtube test.
                if (adev->ms12.sys_data_write2alsa_status) {
                    *timestamp = adev->ms12.sys_audio_timestamp;
                }
            }
        }

        *frames = frames_written_hw;
        frame_latency = aml_audio_get_ms12_nontunel_tune_latency(stream);
    }

    ALOGV("[%s]cur_devices %#x out->hal_internal_format %x adev->ms12.sink_format %x adev->continuous_audio_mode %d \n",
            __func__,adev->cur_out_devices, out->hal_internal_format, adev->ms12.sink_format, adev->continuous_audio_mode);
    ALOGV("[%s]adev->atmos_lock_flag %d\n",
            __func__, adev->atoms_lock_flag);
    ALOGV("[%s]  *frames:%"PRIu64"  frame_latency %d\n",__func__, *frames, frame_latency);

    if (frame_latency < 0) {
        *frames -= frame_latency;
    } else if (*frames >= (uint64_t)abs(frame_latency)) {
        *frames -= frame_latency;
    } else {
        *frames = 0;
    }

    if ((out->hal_rate != MM_FULL_POWER_SAMPLING_RATE) &&
        (!is_bypass_dolbyms12((struct audio_stream_out *)stream))) {
        *frames = (*frames * out->hal_rate) / MM_FULL_POWER_SAMPLING_RATE;
    }

    if (out->streamType == STREAM_PCM_HWSYNC) {
        //write data not update to trigge underrun
        //~580ms from xts Audio Pause to Resume, so setup the threshold 200ms
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        int time_gap_ms = calc_time_interval_us(&out->timestamp, &ts)/1000LL;
        int xts_latency_frames = get_ms12_tunnel_xts_latency();
        //ALOGI("%s %d  time_gap_ms:%d,  write_count:%d, xts_latency_frames:%d", __func__, __LINE__,
        //    time_gap_ms, out->write_count, xts_latency_frames);
        if (!out->frame_write_sum_updated || abs(time_gap_ms) > 200) {
            *frames = out->hwsync_parsed_frames_sum;
        }
        if (abs(time_gap_ms) > 300 && out->hwsync_parsed_frames_sum_paused) {
            *frames = out->hwsync_parsed_frames_sum_paused;
        }

        *frames += xts_latency_frames;//10ms, 441 frames
    }
    return 0;
}


uint32_t aml_audio_out_get_ms12_latency_frames(struct audio_stream_out *stream) {
    return out_get_ms12_latency_frames(stream);
}

int aml_audio_ms12_update_presentation_position(struct audio_stream_out *stream) {
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;


    return 0;
}

static int dtv_get_ms12_input_latency(audio_format_t input_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        prop_name = AVSYNC_MS12_DTV_PCM_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DTV_PCM_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC3: {
        prop_name = AVSYNC_MS12_DTV_DD_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DTV_DD_LATENCY;
        break;
    }

    case AUDIO_FORMAT_E_AC3: {
        prop_name = AVSYNC_MS12_DTV_DDP_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DTV_DDP_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC4: {
        prop_name = AVSYNC_MS12_DTV_AC4_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DTV_AC4_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_AAC_LATM:
    case AUDIO_FORMAT_AAC_HE_V1:
    case AUDIO_FORMAT_AAC_HE_V2:
    case AUDIO_FORMAT_HE_AAC_V1:
    case AUDIO_FORMAT_HE_AAC_V2: {
        prop_name = AVSYNC_MS12_DTV_AAC_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DTV_AAC_LATENCY;
        break;
    }
    case AUDIO_FORMAT_MP2: {
        prop_name = AVSYNC_MS12_DTV_MP2_LATENCY_PROPERTY;
        latency_ms = AVSYNC_MS12_DTV_MP2_LATENCY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}


static int dtv_get_ms12_output_latency(audio_format_t output_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (output_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        latency_ms = AVSYNC_MS12_DTV_PCM_OUT_LATENCY;
        prop_name = AVSYNC_MS12_DTV_PCM_OUT_LATENCY_PROPERTY;
        break;
    }
    case AUDIO_FORMAT_AC3: {
        latency_ms = AVSYNC_MS12_DTV_DD_OUT_LATENCY;
        prop_name = AVSYNC_MS12_DTV_DD_OUT_LATENCY_PROPERTY;
        break;
    }
    case AUDIO_FORMAT_E_AC3: {
        latency_ms = AVSYNC_MS12_DTV_DDP_OUT_LATENCY;
        prop_name = AVSYNC_MS12_DTV_DDP_OUT_LATENCY_PROPERTY;
        break;
    }
    case AUDIO_FORMAT_MAT: {
        latency_ms = AVSYNC_MS12_DTV_MAT_OUT_LATENCY;
        prop_name = AVSYNC_MS12_DTV_MAT_OUT_LATENCY_PROPERTY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    ALOGV("%s output format =0x%x latency ms =%d", __func__, output_format, latency_ms);
    return latency_ms;
}

int dtv_get_ms12_port_latency(struct audio_stream_out *stream, enum OUT_PORT port, audio_format_t output_format)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (port)  {
        case OUTPORT_HDMI_ARC:
        {
            if (output_format == AUDIO_FORMAT_AC3) {
                latency_ms = AVSYNC_MS12_DTV_HDMI_ARC_OUT_DD_LATENCY;
                prop_name = AVSYNC_MS12_DTV_HDMI_ARC_OUT_DD_LATENCY_PROPERTY;
            }
            else if (output_format == AUDIO_FORMAT_E_AC3) {
                latency_ms = AVSYNC_MS12_DTV_HDMI_ARC_OUT_DDP_LATENCY;
                prop_name = AVSYNC_MS12_DTV_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY;
            }
            else {
                latency_ms = AVSYNC_MS12_DTV_HDMI_ARC_OUT_PCM_LATENCY;
                prop_name = AVSYNC_MS12_DTV_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY;
            }
            break;
        }
        case OUTPORT_HDMI:
        {
            if (output_format == AUDIO_FORMAT_AC3) {
                latency_ms = AVSYNC_MS12_DTV_HDMI_OUT_DD_LATENCY;
                prop_name = AVSYNC_MS12_DTV_HDMI_OUT_DD_LATENCY_PROPERTY;
            }
            else if (output_format == AUDIO_FORMAT_E_AC3) {
                latency_ms = AVSYNC_MS12_DTV_HDMI_OUT_DDP_LATENCY;
                prop_name = AVSYNC_MS12_DTV_HDMI_OUT_DDP_LATENCY_PROPERTY;
            }
            else if (output_format == AUDIO_FORMAT_MAT) {
                latency_ms = AVSYNC_MS12_DTV_HDMI_OUT_MAT_LATENCY;
                prop_name = AVSYNC_MS12_DTV_HDMI_OUT_MAT_LATENCY_PROPERTY;
            }
            else {
                latency_ms = AVSYNC_MS12_DTV_HDMI_OUT_PCM_LATENCY;
                prop_name = AVSYNC_MS12_DTV_HDMI_OUT_PCM_LATENCY_PROPERTY;
            }
            break;
        }
        case OUTPORT_SPEAKER:
        case OUTPORT_AUX_LINE:
        {
            if (is_TV(adev)) {
                latency_ms = AVSYNC_MS12_TV_DTV_SPEAKER_LATENCY;
                prop_name = AVSYNC_MS12_TV_DTV_SPEAKER_LATENCY_PROPERTY;
            } else {
                latency_ms = AVSYNC_MS12_DTV_SPEAKER_LATENCY;
                prop_name = AVSYNC_MS12_DTV_SPEAKER_LATENCY_PROPERTY;
            }
            break;
        }
        default :
            break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}

static int dtv_get_ms12_latency_offset(
    struct audio_stream_out *stream
    , enum OUT_PORT port
    , audio_format_t input_format
    , audio_format_t output_format
    )
{
    int latency_ms = 0;
    int input_latency_ms = 0;
    int output_latency_ms = 0;
    int port_latency_ms = 0;
    int is_dv = getprop_bool(MS12_OUTPUT_5_1_DDP); /* suppose that Dolby Vision is under test */

    //ALOGD("%s  prot:%d, is_netflix:%d, input_format:0x%x, output_format:0x%x", __func__,
    //            port, is_netflix, input_format, output_format);
    input_latency_ms  = dtv_get_ms12_input_latency(input_format);
    output_latency_ms = dtv_get_ms12_output_latency(output_format);
    port_latency_ms   = dtv_get_ms12_port_latency(stream, port, output_format);

    latency_ms = input_latency_ms + output_latency_ms + port_latency_ms;
    ALOGV("%s total latency %d(ms) input %d(ms) out %d(ms) port %d(ms)",
        __func__, latency_ms, input_latency_ms, output_latency_ms, port_latency_ms);
    return latency_ms;
}

int dtv_get_ms12_bypass_latency_offset(void)
{
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;

    prop_name = AVSYNC_MS12_DTV_BYPASS_LATENCY_PROPERTY;
    latency_ms = AVSYNC_MS12_DTV_BYPASS_LATENCY;

    ret = property_get(prop_name, buf, NULL);
    if (ret > 0) {
        latency_ms = atoi(buf);
    }
    return latency_ms;
}


int aml_audio_dtv_get_ms12_latency(struct audio_stream_out *stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int32_t latency_frames = 0;
    int32_t alsa_delay = 0;
    int32_t tuning_frame_delay = 0;

    tuning_frame_delay = 48 * dtv_get_ms12_latency_offset(
        stream, get_output_by_devices(adev->cur_out_devices), out->hal_internal_format, adev->ms12.optical_format);

    latency_frames = tuning_frame_delay;
    if (is_TV(adev)) {
        latency_frames += get_media_video_delay(&adev->alsa_mixer) * out->hal_rate / 1000;
    }

    ALOGV("latency frames =%d tuning delay=%d ms", latency_frames, tuning_frame_delay / 48);
    return latency_frames;
}

static int dtv_get_nonms12_input_latency(audio_format_t input_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (input_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        prop_name = AVSYNC_NONMS12_DTV_PCM_LATENCY_PROPERTY;
        latency_ms = AVSYNC_NONMS12_DTV_PCM_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AC3: {
        prop_name = AVSYNC_NONMS12_DTV_DD_LATENCY_PROPERTY;
        latency_ms = AVSYNC_NONMS12_DTV_DD_LATENCY;
        break;
    }

    case AUDIO_FORMAT_E_AC3: {
        prop_name = AVSYNC_NONMS12_DTV_DDP_LATENCY_PROPERTY;
        latency_ms = AVSYNC_NONMS12_DTV_DDP_LATENCY;
        break;
    }
    case AUDIO_FORMAT_MP2: {
        prop_name = AVSYNC_NONMS12_DTV_MP2_LATENCY_PROPERTY;
        latency_ms = AVSYNC_NONMS12_DTV_MP2_LATENCY;
        break;
    }
    case AUDIO_FORMAT_AAC:
    case AUDIO_FORMAT_AAC_LATM: {
        prop_name = AVSYNC_NONMS12_DTV_AAC_LATENCY_PROPERTY;
        latency_ms = AVSYNC_NONMS12_DTV_AAC_LATENCY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}


static int dtv_get_nonms12_output_latency(audio_format_t output_format) {
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (output_format) {
    case AUDIO_FORMAT_PCM_16_BIT: {
        latency_ms = AVSYNC_NONMS12_DTV_PCM_OUT_LATENCY;
        prop_name = AVSYNC_NONMS12_DTV_PCM_OUT_LATENCY_PROPERTY;
        break;
    }
    case AUDIO_FORMAT_AC3: {
        latency_ms = AVSYNC_NONMS12_DTV_DD_OUT_LATENCY;
        prop_name = AVSYNC_NONMS12_DTV_DD_OUT_LATENCY_PROPERTY;
        break;
    }
    case AUDIO_FORMAT_E_AC3: {
        latency_ms = AVSYNC_NONMS12_DTV_DDP_OUT_LATENCY;
        prop_name = AVSYNC_NONMS12_DTV_DDP_OUT_LATENCY_PROPERTY;
        break;
    }
    default:
        break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }
    ALOGV("%s output format =0x%x latency ms =%d", __func__, output_format, latency_ms);
    return latency_ms;
}

int dtv_get_nonms12_port_latency(struct audio_stream_out * stream, enum OUT_PORT port, audio_format_t output_format)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int attend_type, earc_latency;
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = -1;
    int latency_ms = 0;
    char *prop_name = NULL;
    switch (port) {
        case OUTPORT_HDMI_ARC:
        {
            if (output_format == AUDIO_FORMAT_AC3) {
                latency_ms = AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_DD_LATENCY;
                prop_name = AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_DD_LATENCY_PROPERTY;
            }
            else if (output_format == AUDIO_FORMAT_E_AC3) {
                latency_ms = AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_DDP_LATENCY;
                prop_name = AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_DDP_LATENCY_PROPERTY;
            }
            else {
                latency_ms = AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_PCM_LATENCY;
                prop_name = AVSYNC_NONMS12_DTV_HDMI_ARC_OUT_PCM_LATENCY_PROPERTY;
            }
            break;
        }
        case OUTPORT_HDMI:
        {
            if (output_format == AUDIO_FORMAT_AC3) {
                latency_ms = AVSYNC_NONMS12_DTV_HDMI_OUT_DD_LATENCY;
                prop_name = AVSYNC_NONMS12_DTV_HDMI_OUT_DD_LATENCY_PROPERTY;
            }
            else if (output_format == AUDIO_FORMAT_E_AC3) {
                latency_ms = AVSYNC_NONMS12_DTV_HDMI_OUT_DDP_LATENCY;
                prop_name = AVSYNC_NONMS12_DTV_HDMI_OUT_DDP_LATENCY_PROPERTY;
            }
            else {
                latency_ms = AVSYNC_NONMS12_DTV_HDMI_OUT_PCM_LATENCY;
                prop_name = AVSYNC_NONMS12_DTV_HDMI_OUT_PCM_LATENCY_PROPERTY;
            }
            break;
        }
        case OUTPORT_SPEAKER:
        case OUTPORT_AUX_LINE:
        {
            latency_ms = AVSYNC_NONMS12_DTV_SPEAKER_LATENCY;
            prop_name = AVSYNC_NONMS12_DTV_SPEAKER_LATENCY_PROPERTY;
            break;
        }
        default :
            break;
    }

    if (prop_name) {
        ret = property_get(prop_name, buf, NULL);
        if (ret > 0) {
            latency_ms = atoi(buf);
        }
    }

    return latency_ms;
}


static int dtv_get_nonms12_latency_offset(
    struct audio_stream_out * stream
    ,enum OUT_PORT port
    , audio_format_t input_format
    , audio_format_t output_format
    )
{
    int latency_ms = 0;
    int input_latency_ms = 0;
    int output_latency_ms = 0;
    int port_latency_ms = 0;
    int is_dv = getprop_bool(MS12_OUTPUT_5_1_DDP); /* suppose that Dolby Vision is under test */

    //ALOGD("%s  prot:%d, is_netflix:%d, input_format:0x%x, output_format:0x%x", __func__,
    //            port, is_netflix, input_format, output_format);
    input_latency_ms  = dtv_get_nonms12_input_latency(input_format);
    output_latency_ms = dtv_get_nonms12_output_latency(output_format);
    port_latency_ms   = dtv_get_nonms12_port_latency(stream, port, output_format);

    latency_ms = input_latency_ms + output_latency_ms + port_latency_ms;
    ALOGV("%s total latency %d ms input %d ms output %d ms port %d ms",
        __func__, latency_ms, input_latency_ms, output_latency_ms, port_latency_ms);
    return latency_ms;
}

int aml_audio_dtv_get_nonms12_latency(struct audio_stream_out * stream)
{
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    int32_t tuning_delay = 0;
    int latency_frames = 0;

    tuning_delay = 48 * dtv_get_nonms12_latency_offset(stream,
        get_output_by_devices(adev->cur_out_devices), out->hal_internal_format, adev->sink_format);

    latency_frames = tuning_delay;
    if (is_TV(adev)) {
        latency_frames += get_media_video_delay(&adev->alsa_mixer) * out->hal_rate / 1000;
        latency_frames += property_get_int32(AVSYNC_DTV_TV_MODE_LATENCY_PROPERTY, AVSYNC_DTV_TV_MODE_LATENCY) * out->hal_rate / 1000;
    }

    ALOGV("latency frames =%d tuning delay=%d ms", latency_frames, tuning_delay / 48);

    return latency_frames;
}

