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
 */
#define LOG_TAG "audio_hw_hal_cfgdata"


#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <string.h>
#include <hardware/audio.h>
#include <audio_hw_utils.h>

#include "aml_config_data.h"


#define AML_AUDIO_CONFIG_FILE_PATH "/vendor/etc/aml_audio_config.json"
cJSON *audio_config_jason = NULL;

int aml_audio_config_parser()
{
    audio_config_jason = aml_config_parser(AML_AUDIO_CONFIG_FILE_PATH);
    if (audio_config_jason) {
        return 0;
    } else {
        return -1;
    }
}

int aml_get_jason_int_value(char* key,int defvalue)
{
    cJSON *temp = NULL;
    int value = defvalue;
    if (audio_config_jason) {
        temp = cJSON_GetObjectItem(audio_config_jason, key);
        aml_printf_cJSON(key, temp);
        if (temp) {
            value = temp->valueint;
        }
    }
    return value;
}

const char *aml_get_jason_str_value(const char* key, const char* defvalue)
{
    if (audio_config_jason) {
        cJSON *temp = cJSON_GetObjectItem(audio_config_jason, key);
        aml_printf_cJSON(key, temp);
        if (temp) {
            return temp->valuestring;
        }
    }
    return defvalue;
}

bool aml_get_codec_support(char* aformat)
{
    cJSON *item = NULL;
    cJSON *list = NULL;
    cJSON *format = NULL;
    cJSON *support = NULL;
    int array_size = 0;
    ALOGI("aformat %s!\n", aformat);
    if (audio_config_jason) {
        list = cJSON_GetObjectItem(audio_config_jason, "Codec_Support_List");
        if (!list || !cJSON_IsArray(list)) {
            ALOGI("no Codec_Support_List or not a Array!");
            return false;
        }

        array_size = cJSON_GetArraySize(list);
        for (int i=0; i< array_size; i++) {
            item = cJSON_GetArrayItem(list, i);
            format = cJSON_GetObjectItem(item, "Format");
            if (!format) {
                ALOGI("no format string!");
                continue;
            }
            if (strcmp(aformat, format->valuestring) == 0) {
                support = cJSON_GetObjectItem(item, "Support");
                if (!support) {
                    ALOGI("no support string!\n");
                    return false;
                } else {
                    ALOGI("support:%d", support->type == cJSON_True);
                    return (support->type == cJSON_True);
                }
            }
        }

    }
    return false;
}

void aml_audio_board_config_init(struct audio_board_config *config)
{
    config->hdmitx_src = -1;
    config->hdmitx_hbr_src = -1;
    config->hdmitx_multi_ch_src = -1;
    config->cpux_affinity_support = -1;

#if defined(TV_AUDIO_OUTPUT)
    config->default_alsa_ch =  aml_audio_get_default_alsa_output_ch();
#else
    /* for stb/ott, fixed 2 channels speaker output for alsa*/
    config->default_alsa_ch = 2;
    if (aml_audio_check_sbr_product()) {
        config->default_alsa_ch = aml_audio_get_default_alsa_output_ch();
    }
#endif

    if (aml_audio_config_parser() == 0) {
        config->hdmitx_src = aml_get_jason_int_value("HDMITX_Src_Select", -1);
        if (config->hdmitx_src != -1) {
            config->spdif_independent = true;
        }

        config->hdmitx_multi_ch_src = aml_get_jason_int_value("HDMITX_Multi_CH_Src_Select", -1);
        config->hdmitx_hbr_src = aml_get_jason_int_value("HDMITX_HBR_Src_Select", -1);
        config->default_alsa_ch = aml_get_jason_int_value("ALSA_Speaker_Channels", config->default_alsa_ch);
        config->ms12_output_mask = aml_get_jason_int_value("MS12_Output_Masks", 0);
        config->DTS_output_ch = aml_get_jason_int_value("DTS_Output_Channels", 0);
        config->cpux_affinity_support = aml_get_jason_int_value("CPUX_Affinity_Support", -1);
        config->sbr_spk_ott_hbr_same_tdm = aml_get_jason_int_value("SBR_Speaker_OTT_HBR_Same_TDM", 0);
        /* get config of alsa device id for builtinmic, default -1 for using pdm device */
        config->builtinmic_alsa_dev_id = aml_get_jason_int_value("Builtinmic_Alsa_Dev_Id", -1);
        config->audio_process_bitwidth    = aml_get_jason_int_value("Audio_Process_BitWidth", 16);
        /* Start: Audio Setting UI/Core relevant configurations */
        /*if not support MS12, to change Dolby_MS12_Audio_Config with "-1" in adev_open() */
        const char* str_val = aml_get_jason_str_value("Dolby_MS12_Audio_Config", "Y");
        if (strcasestr(str_val, "Z") != NULL) {
            config->dolby_ms12_audio_config = MS12_CONFIG_Z;
        } else if (strcasestr(str_val, "X") != NULL) {
            config->dolby_ms12_audio_config = MS12_CONFIG_X;
        } else {
            config->dolby_ms12_audio_config = MS12_CONFIG_Y;
        }
        config->dts_virtualx_audio_config = aml_get_jason_int_value("Dts_Virtualx_Audio_Config", 0);
        config->effect_EQ_Audio_Config = aml_get_jason_int_value("Effect_EQ_Audio_Config", 0);
        config->effect_Balance_Audio_Config = aml_get_jason_int_value("Effect_Balance_Audio_Config", 0);
        config->effect_TrebleBass_Audio_Config = aml_get_jason_int_value("Effect_TrebleBass_Audio_Config", 0);
        config->effect_VirtualSurround_Audio_Config = aml_get_jason_int_value("Effect_VirtualSurround_Audio_Config", 0);
        config->effect_DPE_Audio_Config = aml_get_jason_int_value("Effect_DPE_Audio_Config", 0);
        config->dolby_DRC_Audio_Config = aml_get_jason_int_value("Dolby_DRC_Audio_Config", 0);
        config->dts_DRC_Audio_Config = aml_get_jason_int_value("Dts_DRC_Audio_Config", 0);
        config->audio_Latency_Config = aml_get_jason_int_value("Audio_Latency_Config", 0);
        config->force_DDP_Config = aml_get_jason_int_value("Force_DDP_Config", 0);
        config->passthrough_Audio_Config = aml_get_jason_int_value("Passthrough_Audio_Config", 0);
        config->engineer_Mode_Audio_Config = aml_get_jason_int_value("Engineer_Mode_Audio_Config", 0);
        /* End: Audio Setting UI/Core relevant configurations */
    } else {
        ALOGW("%s() Fail!", __func__);
    }
}

char * get_parameters_from_json_config(struct audio_board_config *board_config, const char *keys)
{
    char temp_buf[AUDIO_HAL_CHAR_MAX_LEN] = {0};

    if (!strcmp(keys, "Dolby_MS12_Audio_Config")) {
        sprintf(temp_buf, "Dolby_MS12_Audio_Config=%s",
            (board_config->dolby_ms12_audio_config == MS12_CONFIG_Z ? "Z" :\
            (board_config->dolby_ms12_audio_config == MS12_CONFIG_Y ? "Y" :\
            (board_config->dolby_ms12_audio_config == MS12_CONFIG_X ? "X" :"N"))));
        ALOGV("%s() temp_buf=%s",__func__, temp_buf);
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Dts_Virtualx_Audio_Config")) {
        sprintf(temp_buf, "Dts_Virtualx_Audio_Config=%s",
            (((board_config->dts_virtualx_audio_config == 1) && Check_VX_lib()) ? "1" : "0"));
        ALOGV("%s() temp_buf=%s",__func__, temp_buf);
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Effect_EQ_Audio_Config")) {
        sprintf(temp_buf, "Effect_EQ_Audio_Config=%s",
            (board_config->effect_EQ_Audio_Config == 1 ? "1" :\
            (board_config->effect_EQ_Audio_Config == 5 ? "5" :\
            (board_config->effect_EQ_Audio_Config == 7 ? "7" :\
            (board_config->effect_EQ_Audio_Config == 9 ? "9" : "0")))));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Effect_Balance_Audio_Config")) {
        sprintf(temp_buf, "Effect_Balance_Audio_Config=%s",
            (board_config->effect_Balance_Audio_Config == 1 ? "1" : "0"));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Effect_TrebleBass_Audio_Config")) {
        sprintf(temp_buf, "Effect_TrebleBass_Audio_Config=%s",
            (board_config->effect_TrebleBass_Audio_Config == 1 ? "1" : "0"));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Effect_VirtualSurround_Audio_Config")) {
        sprintf(temp_buf, "Effect_VirtualSurround_Audio_Config=%s",
            (board_config->effect_VirtualSurround_Audio_Config == 1 ? "1" : "0"));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Effect_DPE_Audio_Config")) {
        sprintf(temp_buf, "Effect_DPE_Audio_Config=%s",
            (board_config->effect_DPE_Audio_Config == 1 ? "1" : "0"));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Dolby_DRC_Audio_Config")) {
        sprintf(temp_buf, "Dolby_DRC_Audio_Config=%s",
            (board_config->dolby_DRC_Audio_Config == 1 ? "1" : "0"));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Dts_DRC_Audio_Config")) {
        sprintf(temp_buf, "Dts_DRC_Audio_Config=%s",
            (board_config->dts_DRC_Audio_Config == 1 ? "1" : "0"));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Audio_Latency_Config")) {
        sprintf(temp_buf, "Audio_Latency_Config=%s",
            (board_config->audio_Latency_Config == 1 ? "1" : "0"));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Force_DDP_Config")) {
        sprintf(temp_buf, "Force_DDP_Config=%s",
            (board_config->force_DDP_Config == 1 ? "1" : "0"));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Passthrough_Audio_Config")) {
        sprintf(temp_buf, "Passthrough_Audio_Config=%s",
            (board_config->passthrough_Audio_Config == 1 ? "1" : "0"));
        return strdup(temp_buf);
    } else if (!strcmp(keys, "Engineer_Mode_Audio_Config")) {
        sprintf(temp_buf, "Engineer_Mode_Audio_Config=%s",
            (board_config->engineer_Mode_Audio_Config == 1 ? "1" : "0"));
        return strdup(temp_buf);
    }

    return NULL;
}

