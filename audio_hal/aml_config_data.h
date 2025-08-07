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
#ifndef _AML_CONFIG_DATA_H_
#define _AML_CONFIG_DATA_H_

#include <stdbool.h>
#include <aml_alsa_mixer.h>

#include "aml_config_parser.h"
#ifdef SUPPORT_KARAOKE
#include "karaoke_manager.h"
#endif

/* board specific json configs */
struct audio_board_config {
    int hdmitx_src; /* HDMITX src select for TDM */
    bool spdif_independent;  /*spdif output can be independent with HDMI output*/
    enum AML_SRC_TO_HDMITX hdmitx_multi_ch_src;
    enum AML_SRC_TO_HDMITX hdmitx_hbr_src;
    /*
    defined for default speaker output channels:
    stb: default 2 channels.
    tv:  default 8 channels(2ch speaker,2ch spdif,2ch headphone)
    soundbar:depending on the prop defined by device
    */
    int default_alsa_ch;
    int ms12_output_mask;
    int DTS_output_ch;
    int cpux_affinity_support;
    int audio_process_bitwidth;   /*this control the bidwidth of audio hal processing*/
     /*
      0: Soundbar, Speaker(8ch-PCM) vs HDMI(MAT), HBR do not use a same TDM.
      1: Soundbar, Speaker(8ch-PCM) vs HDMI(MAT), HBR use a same TDM.
     */
    int sbr_spk_ott_hbr_same_tdm;

    /*
      some models of chips do not support pdm
      some projects do not use pdm for builtinmic
      define alsa device id for builtinmic
      -1: use default id of pdm device (default -1)
      >= 0: specific alsa device id for builtinmic
    */
    int builtinmic_alsa_dev_id;
    /*
      -1: Not support MS12
      0: config with Y
      1: config with X
      2: config with Z
    */
    int dolby_ms12_audio_config;
    /*
      0: off virtualx
      1: on virtualx
    */
    int dts_virtualx_audio_config;
    /*
      0: off HPEQ
      1/5: on 5 band HPEQ
      7: on 5 band HPEQ
      9: on 9 band HPEQ
    */
    int effect_EQ_Audio_Config;
    /*
      0: off Balance
      1: on Balance
    */
    int effect_Balance_Audio_Config;
    /*
      0: off Balance
      1: on Balance
    */
    int effect_TrebleBass_Audio_Config;
    /*
      0: off VirtualSurround
      1: on VirtualSurround
    */
    int effect_VirtualSurround_Audio_Config;
    /*
      0: off DPE
      1: on DPE
    */
    int effect_DPE_Audio_Config;
    /*
    */
   int effect_aml_peq_Audio_Config;
    /*
      0: hide Dolby DRC UI
      1: Display Dolby DRC UI
    */
    int dolby_DRC_Audio_Config;
    /*
      0: hide DTS DRC UI
      1: display DTS DRC UI
    */
    int dts_DRC_Audio_Config;
    /*
      0: hide audio latency UI
      1: display audio latency UI
    */
    int audio_Latency_Config;
    /*
      0: hide force DDP UI
      1: display force DDP UI
    */
    int force_DDP_Config;
    /*
      Advanced -> Select format -> Sub UI: Passthrough
      0: hide passthrough UI
      1: display passthrough under "Select format"
    */
    int passthrough_Audio_Config;
    /*
      0: hide EQ Volume UI
      1: display EQ Volume UI
    */
    int volume_eq_config;
    /*
      0: hide AI DE UI
      1: display AI DE UI
    */
    int ai_de_config;
    /*
      0: hide AI AQ UI
      1: display AI AQ UI
    */
    int ai_aq_config;

    /*
      0: hide engineer Mode UI
      1: display engineer Mode UI
    */
    int engineer_Mode_Audio_Config;

#ifdef SUPPORT_KARAOKE
    struct karaoke_config usb_kara_config; /* usb karaoke config */
    struct karaoke_config linein_kara_config; /* linein karaoke config */
#endif

    /*
      The type of Global Mic for UI
      0: None
      1: USB
      2: Linein
      3: USB + Linein
    */
    int global_Mic_Device_Type;

    /*
      0: hide VAD Switch UI
      1: display VAD Switch UI
    */
   int vad_Switch_Audio_Config;
    /*
      Currently, the OTT MS12 config X differs from the general MS12 config X
      -1: Not support MS12
      1: config with X but there are functional differences
    */
   int ott_dolby_ms12_audio_config;

};

int aml_audio_config_parser();
int aml_get_jason_int_value(char* key,int defvalue);
const char *aml_get_jason_str_value(const char* key, const char* defvalue);
bool aml_get_codec_support(char* aformat);
void aml_audio_board_config_init(struct audio_board_config *config);
char * get_parameters_from_json_config(struct audio_board_config *board_config, const char *keys);

#endif
