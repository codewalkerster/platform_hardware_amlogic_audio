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

#define LOG_TAG "audio_hw_hal_stream"
//#define LOG_NDEBUG 0
#include <inttypes.h>
#include <cutils/log.h>
#include <tinyalsa/asoundlib.h>
#include <cutils/properties.h>
#include <audio_utils/channels.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <audio_utils/Metadata.h>

#include "aml_alsa_mixer.h"
#include "aml_audio_stream.h"
#include "dolby_lib_api.h"
#include "audio_hw_utils.h"
#include "audio_hw_profile.h"
#include "alsa_manager.h"
#include "alsa_device_parser.h"
#include "tv_patch_avsync.h"
#include "aml_android_utils.h"
#include "alsa_config_parameters.h"
#include "audio_hw_ms12.h"
#include "amlAudioMixer.h"
#include "audio_hw_ms12_common.h"
#include "audio_hw_resource_mgr.h"
#include "aml_audio_ms12_sync.h"

#ifdef MS12_V24_ENABLE
#include "audio_hw_ms12_v2.h"
#endif
#define DOLBY_AC4_FMT_UPDATE_THRESHOLD  (40)
#define FMT_UPDATE_THRESHOLD_MAX    (100)
#define DOLBY_FMT_UPDATE_THRESHOLD  (5)
#define DTS_FMT_UPDATE_THRESHOLD    (1)

/*
 * DAP Speaker Virtualizer
 * -dap_surround_virtualizer    * <2 int> Virtualizer Parameter
 *                                         - virtualizer_mode (0,1,2, def: 1)
 *                                            0:OFF
 *                                            1:ON
 *                                            2:AUTO
 *                                         - surround_boost (0...96, def: 96)
 */
#define MS12_DAP_SPEAKER_VIRTUALIZER_OFF  (0)//Disable Speaker Virtualizer(Disable Dolby Atmos Virtualization).
#define MS12_DAP_SPEAKER_VIRTUALIZER_ON   (1)//Enable Speaker Virtualizer.
#define MS12_DAP_SPEAKER_VIRTUALIZER_AUTO (2)//Enable Dolby Atmos Virtualization.

#define HDMI_HDR_STATUS_NODE        "/sys/class/amhdmitx/amhdmitx0/hdmi_hdr_status"
#define SINK_DV_KEYWORD             "DolbyVision"

#define JITTER_PRINT_THRESHOLD (100) // milliseconds
#define INFO_TIME_PRINT_THRESHOLD (100) // milliseconds


static audio_format_t ms12_max_support_output_format() {
#ifndef MS12_V24_ENABLE
    return AUDIO_FORMAT_E_AC3;
#else
    return AUDIO_FORMAT_MAT;
#endif
}

int get_file_size(char *name)
{
    struct stat statbuf;
    int ret;

    ret = stat(name, &statbuf);
    if (ret != 0)
        return -1;

    return statbuf.st_size;
}

/*
 *@brief get sink capability
 */
static audio_format_t get_sink_capability (struct aml_audio_device *adev)
{
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);

    bool dd_is_support = hdmi_desc->dd_fmt.is_support;
    bool ddp_is_support = hdmi_desc->ddp_fmt.is_support;
    bool mat_is_support = hdmi_desc->mat_fmt.is_support;
    bool mat_truehd_only = hdmi_desc->mat_fmt.mat_truehd_only;

    audio_format_t sink_capability = AUDIO_FORMAT_PCM_16_BIT;

    //STB case
    //TV + STB case (BDS)
    //TODO HDMITX+ARC mixed connected case
    //need check active port ???
    if (!is_TV(adev))
    {
        char *cap = NULL;
        /*we should get the real audio cap, so we need it report the correct truehd info*/
        cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_FORMATS,0, hdmi_desc, true);

        dd_is_support = hdmi_desc->dd_fmt.is_support;
        ddp_is_support = hdmi_desc->ddp_fmt.is_support;
        mat_is_support = hdmi_desc->mat_fmt.is_support;
        mat_truehd_only = hdmi_desc->mat_fmt.mat_truehd_only;

        if (cap) {
            /*
             * Dolby MAT 2.0/2.1 has low latency vs Dolby MAT 1.0(TRUEHD inside)
             * Dolby MS12 prefers to output MAT2.0/2.1.
             */
            if (mat_is_support) {
                sink_capability = AUDIO_FORMAT_MAT;
            } else if (mat_truehd_only) {
                sink_capability = AUDIO_FORMAT_DOLBY_TRUEHD;
            }
            /*
             * DDP vs DDP
             * Dolby MS12 prefers to output DDP.
             */
            else if (ddp_is_support) {
                sink_capability = AUDIO_FORMAT_E_AC3;
            }
            /*
             * DD vs PCM
             * Dolby MS12 prefers to output DD.
             */
            else if (dd_is_support) {
                sink_capability = AUDIO_FORMAT_AC3;
            }
            ALOGI ("%s mbox+dvb case sink_capability =  %#x\n", __FUNCTION__, sink_capability);
            aml_audio_free(cap);
            cap = NULL;
        }

    } else {
        if (mat_is_support || hdmi_desc->mat_fmt.MAT_PCM_48kHz_only) {
            sink_capability = AUDIO_FORMAT_MAT;
            mat_is_support = true;
            hdmi_desc->mat_fmt.is_support = true;
        } else if (ddp_is_support) {
            sink_capability = AUDIO_FORMAT_E_AC3;
        } else if (dd_is_support) {
            sink_capability = AUDIO_FORMAT_AC3;
        }

        /* eARC TXs support formats at least support dd, for Test ID HFR5-1-27 */
        if (sink_capability == AUDIO_FORMAT_PCM_16_BIT &&
            aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_ATTENDED_TYPE) == ATTEND_TYPE_EARC &&
            is_arc_connected(adev)) {
            sink_capability = AUDIO_FORMAT_AC3;
            dd_is_support = true;
            hdmi_desc->dd_fmt.is_support = true;
        }

        ALOGI ("%s mat_is_support:%d, dd support:%d ddp support:%#x\n", __FUNCTION__, mat_is_support, dd_is_support, ddp_is_support);
    }

    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        bool b_force_ddp = adev->ms12_force_ddp_out;
        ALOGI("force ddp out =%d", b_force_ddp);
        if (b_force_ddp) {
            if (ddp_is_support) {
                sink_capability = AUDIO_FORMAT_E_AC3;
            } else if (dd_is_support) {
                sink_capability = AUDIO_FORMAT_AC3;
            }
        }
    }

    return sink_capability;
}

static audio_format_t get_sink_dts_capability (struct aml_audio_device *adev)
{
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);

    bool dts_is_support = hdmi_desc->dts_fmt.is_support;
    bool dtshd_is_support = hdmi_desc->dtshd_fmt.is_support;

    audio_format_t sink_capability = AUDIO_FORMAT_PCM_16_BIT;

    //STB case
    if (!is_TV(adev))
    {
        char *cap = NULL;
        cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_FORMATS,0,hdmi_desc, true);
        if (cap) {
            if (hdmi_desc->dtshd_fmt.is_support) {
                sink_capability = AUDIO_FORMAT_DTS_HD;
            } else if (hdmi_desc->dts_fmt.is_support) {
                sink_capability = AUDIO_FORMAT_DTS;
            }
            AM_LOGI("mbox+dvb case sink_capability: %s(%#x)", audioFormat2Str(sink_capability), sink_capability);
            aml_audio_free(cap);
            cap = NULL;
        }
    } else {
        if (dtshd_is_support) {
            sink_capability = AUDIO_FORMAT_DTS_HD;
        } else if (dts_is_support) {
            sink_capability = AUDIO_FORMAT_DTS;
        }
        ALOGI ("%s dts support %d dtshd support %d\n", __FUNCTION__, dts_is_support, dtshd_is_support);
    }
    return sink_capability;
}


static audio_format_t get_sink_mpegh_capability (struct aml_audio_device *adev)
{
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);

    bool mpegh_is_support = hdmi_desc->mpegh_fmt.is_support;

    audio_format_t sink_capability = AUDIO_FORMAT_PCM_16_BIT;

    //STB case
    if (!is_TV(adev))
    {
        char *cap = NULL;
        cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_FORMATS, 0, hdmi_desc, true);
        if (cap) {
            if (hdmi_desc->mpegh_fmt.is_support) {
                sink_capability = (audio_format_t)AUDIO_FORMAT_MPEGH;
            }
            AM_LOGI("mbox+dvb case sink_capability: %s(%#x)", audioFormat2Str(sink_capability), sink_capability);
            aml_audio_free(cap);
            cap = NULL;
        }
    } else {
        if (mpegh_is_support) {
            sink_capability = (audio_format_t)AUDIO_FORMAT_MPEGH;
        }
        ALOGI ("%s mpegh support %d\n", __FUNCTION__, mpegh_is_support);
    }
    return sink_capability;
}

static void get_sink_pcm_capability(struct aml_audio_device *adev)
{
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);
    char *cap = NULL;
    hdmi_desc->pcm_fmt.sample_rate_mask = 0;

    cap = (char *) get_hdmi_sink_cap_new (AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, AUDIO_FORMAT_PCM_16_BIT,hdmi_desc, true);
    if (cap) {
        /*
         * bit:    6     5     4    3    2    1    0
         * rate: 192  176.4   96  88.2  48  44.1   32
         */
        if (strstr(cap, "32000") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<0);
        if (strstr(cap, "44100") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<1);
        if (strstr(cap, "48000") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<2);
        if (strstr(cap, "88200") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<3);
        if (strstr(cap, "96000") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<4);
        if (strstr(cap, "176400") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<5);
        if (strstr(cap, "192000") != NULL)
            hdmi_desc->pcm_fmt.sample_rate_mask |= (1<<6);

        aml_audio_free(cap);
        cap = NULL;
    }

    ALOGI("pcm_fmt support sample_rate_mask:0x%x", hdmi_desc->pcm_fmt.sample_rate_mask);
}

static unsigned int get_sink_format_max_channels(struct aml_audio_device *adev, audio_format_t sink_format) {
    unsigned int max_channels = 2;
    struct aml_arc_hdmi_desc *hdmi_desc = get_arc_hdmi_cap(adev);

    switch (sink_format) {
    case AUDIO_FORMAT_PCM_16_BIT:
        max_channels = hdmi_desc->pcm_fmt.max_channels;
        break;
    case AUDIO_FORMAT_AC3:
        max_channels = hdmi_desc->dd_fmt.max_channels;
        break;
    case AUDIO_FORMAT_E_AC3:
        max_channels = hdmi_desc->ddp_fmt.max_channels;
        break;
    case AUDIO_FORMAT_DTS:
        max_channels = hdmi_desc->dts_fmt.max_channels;
        break;
    case AUDIO_FORMAT_DTS_HD:
        max_channels = hdmi_desc->dtshd_fmt.max_channels;
        break;
    case AUDIO_FORMAT_MAT:
        max_channels = hdmi_desc->mat_fmt.max_channels;
        break;
    default:
        max_channels = 2;
        break;
    }
    if (max_channels == 0) {
        max_channels = 2;
    }
    return max_channels;
}

static bool get_sink_dv_capability()
{
    char buffer[128];
    FILE *fp = NULL;
    bool dv_enable = false;

    memset(buffer, 0, sizeof(buffer));
    fp = fopen(HDMI_HDR_STATUS_NODE, "r");
    if (fp) {
        int read_count = fread((char *)buffer, 1, sizeof(buffer)-1, fp);
        if (ferror(fp)) {
            ALOGE("%s : fread has IO wrong", __func__);
       }
        fclose(fp);
    }
    ALOGI("%s : %s = %s", __func__, HDMI_HDR_STATUS_NODE, buffer);

    if (strstr(buffer, SINK_DV_KEYWORD) != NULL) {
        dv_enable = true;
    }

    ALOGI("%s : dv enable %d", __func__, dv_enable);
    return dv_enable;
}

bool is_sink_support_dolby_passthrough(audio_format_t sink_capability)
{
    return sink_capability == AUDIO_FORMAT_MAT ||
        sink_capability == AUDIO_FORMAT_E_AC3 ||
        sink_capability == AUDIO_FORMAT_AC3;
}

/*
 *1. source format includes these formats:
 *        AUDIO_FORMAT_PCM_16_BIT = 0x1u
 *        AUDIO_FORMAT_AC3 =    0x09000000u
 *        AUDIO_FORMAT_E_AC3 =  0x0A000000u
 *        AUDIO_FORMAT_DTS =    0x0B000000u
 *        AUDIO_FORMAT_DTS_HD = 0x0C000000u
 *        AUDIO_FORMAT_AC4 =    0x22000000u
 *        AUDIO_FORMAT_MAT =    0x24000000u
 *
 *2. if the source format can output directly as PCM format or as IEC61937 format,
 *   btw, AUDIO_FORMAT_PCM_16_BIT < AUDIO_FORMAT_AC3 < AUDIO_FORMAT_MAT is true.
 *   we can use the min(a, b) to get an suitable output format.
 *3. if source format is AUDIO_FORMAT_AC4, we can not use the min(a,b) to get the
 *   suitable format but use the sink device max capability format.
 */
static audio_format_t get_suitable_output_format(struct aml_stream_out *out,
        audio_format_t source_format, audio_format_t sink_format)
{
    audio_format_t output_format;
    if (IS_EXTERNAL_DECODER_SUPPORT_FORMAT(source_format)) {
        output_format = MIN(source_format, sink_format);
        /*
         * if source: AUDIO_FORMAT_DOLBY_TRUEHD and sink: AUDIO_FORMAT_MAT
         * use the AUDIO_FORMAT_MAT as output format(from IEC 61937-1).
         */
        output_format = (output_format != AUDIO_FORMAT_DOLBY_TRUEHD) ? output_format: AUDIO_FORMAT_MAT;
    } else {
        output_format = sink_format;
    }
    if ((out->hal_rate == 32000 || out->hal_rate == 128000) && output_format == AUDIO_FORMAT_E_AC3) {
        output_format = AUDIO_FORMAT_AC3;
    }
    return output_format;
}

/*
 * When turn on the Automatic function to play the dolby audio in low speed,the TV might
 * can't decode.So if the play mode was Automatic and the output speed was in not equal
 * 1.0f, set the ret_format to AUDIO_FORMAT_PCM_16_BIT to send the PCM data to spdif.
 */
static audio_format_t reconfig_optical_audio_format(struct aml_stream_out *aml_out,
        audio_format_t org_optical_format)
{
    audio_format_t ret_format = org_optical_format;
    struct aml_audio_device *aml_dev = aml_out->dev;

    if (aml_out == NULL || eDolbyMS12Lib == aml_dev->dolby_lib_type)
        return org_optical_format;

    if (aml_out->output_speed != 1.0f && aml_out->output_speed != 0.0f) {
        ALOGI("change to micro speed need reconfig optical audio format to PCM");
        ret_format = AUDIO_FORMAT_PCM_16_BIT;
    }

    return ret_format;
}

/*
 *@brief get sink format by logic min(source format / digital format / sink capability)
 * For Speaker/Headphone output, sink format keep PCM-16bits
 * For optical output, min(dd, source format, digital format)
 * For HDMI_ARC output
 *      1.digital format is PCM, sink format is PCM-16bits
 *      2.digital format is dd, sink format is min (source format,  AUDIO_FORMAT_AC3)
 *      3.digital format is auto, sink format is min (source format, digital format)
 */
void get_sink_format(struct audio_stream_out *stream)
{
   if (stream == NULL) {
        ALOGE("stream NULL");
        return;
    }
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    adev->sink_format_updating = true;
    /*set default value for sink_audio_format/optical_audio_format*/
    audio_format_t sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
    audio_format_t optical_audio_format = AUDIO_FORMAT_PCM_16_BIT;
    audio_format_t sink_capability = get_sink_capability(adev);
    audio_format_t sink_dts_capability = get_sink_dts_capability(adev);
    audio_format_t sink_mpegh_capability = get_sink_mpegh_capability(adev);
    audio_format_t source_format = aml_out->hal_internal_format;

    get_sink_pcm_capability(adev);

    adev->bDVEnable = get_sink_dv_capability();

    // After all sink capability updated
    adev->sink_format_updating = false;
    AM_LOGI("out:%p out devices:%#x cur_out_devices:%#x format:%s(%#x) digital_mode(%s) sink cap:%s(%#x)", aml_out,
          adev->out_device, adev->cur_out_devices, audioFormat2Str(aml_out->hal_internal_format), aml_out->hal_internal_format,
          digitalAudioModeType2Str(adev->digital_audio_mode), audioFormat2Str(sink_capability), sink_capability);


    if (adev->cur_out_devices & AUDIO_DEVICE_OUT_ALL_A2DP || adev->cur_out_devices & AUDIO_DEVICE_OUT_ALL_USB) {
        ALOGD("get_sink_format: a2dp and usb set to pcm");
        adev->sink_format = AUDIO_FORMAT_PCM_16_BIT;
        adev->sink_capability = AUDIO_FORMAT_PCM_16_BIT;
        adev->optical_format = AUDIO_FORMAT_PCM_16_BIT;
        aml_out->dual_output_flag = false;
        adev->sink_max_channels = 2;
        return;
    }

    if (adev->is_netflix && adev->aaudio_low_latency) {
        unsigned int max_channels = get_sink_format_max_channels(adev, AUDIO_FORMAT_PCM_16_BIT);

        ALOGD("get_sink_format: netflix aaudio_low_latency set to pcm");
        adev->sink_format = AUDIO_FORMAT_PCM_16_BIT;
        adev->sink_capability = AUDIO_FORMAT_PCM_16_BIT;
        adev->optical_format = AUDIO_FORMAT_PCM_16_BIT;

#ifndef AUDIO_HAL_DISABLE_MS12
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            if (adev->ms12.dolby_ms12_enable
                && !is_arc_connected(adev)
                && adev->is_ui_force_dap_disable == false
                && adev->effect_ctrl.effect_mode == EFFECT_MODE_DAP) {
                // LLP not request dap
                set_ms12_full_dap_disable(&adev->ms12, true);
            }
            audiohal_send_msg_2_ms12(&adev->ms12, MS12_MESG_TYPE_RESET_MS12_ENCODER);
            set_ms12_alsa_limit_frame(&adev->ms12, MS12_ALSA_LOW_LIMIT_FRAME);
        }
#endif

        // For multi aaudio stream : always output mc pcm if sink device support.
        adev->sink_max_channels = max_channels;
        return;
    }

    /*when device is HDMI_ARC*/

    if ((source_format != AUDIO_FORMAT_PCM_16_BIT) && \
        (source_format != AUDIO_FORMAT_AC3) && \
        (source_format != AUDIO_FORMAT_E_AC3) && \
        (source_format != AUDIO_FORMAT_MAT) && \
        (source_format != AUDIO_FORMAT_AC4) && \
        (source_format != AUDIO_FORMAT_DTS) &&
        (source_format != AUDIO_FORMAT_DTS_HD) && \
        (source_format != AUDIO_FORMAT_DTS_UHD_P2) && \
        (source_format != AUDIO_FORMAT_DOLBY_TRUEHD) && \
        (source_format != AUDIO_FORMAT_AAC) && \
        (source_format != AUDIO_FORMAT_AAC_LATM) && \
        (source_format != AUDIO_FORMAT_HE_AAC_V1) && \
        (source_format != AUDIO_FORMAT_HE_AAC_V2) && \
        (source_format != AUDIO_FORMAT_MPEGH)) {
        /*unsupport format [dts-hd/true-hd]*/
        ALOGI("%s() source format %#x change to %#x", __FUNCTION__, source_format, AUDIO_FORMAT_PCM_16_BIT);
        source_format = AUDIO_FORMAT_PCM_16_BIT;
    }
    adev->sink_capability = sink_capability;

    // "adev->digital_audio_mode" is the UI selection item.
    // "adev->active_outport" was set when HDMI ARC cable plug in/off
    // condition 1: ARC port, single output.
    // condition 2: for STB case with dolby-ms12 libs
    // condition 3: T7 BDS with HDMITX case
    if ((adev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) != 0 || !is_TV(adev)) {
        struct audio_board_config *bd_config = &adev->board_config;
        ALOGI("%s() HDMI ARC or mbox + dvb case", __FUNCTION__);
        switch (adev->digital_audio_mode) {
        case AML_DIGITAL_AUDIO_MODE_PCM:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            break;
        case AML_DIGITAL_AUDIO_MODE_DD:
            if (dts_stream_active(adev)) {
                sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            } else {
                sink_audio_format = get_suitable_output_format(aml_out, AUDIO_FORMAT_AC3, sink_capability);
            }
            optical_audio_format = sink_audio_format;
            break;
        case AML_DIGITAL_AUDIO_MODE_AUTO:
            if (is_dts_format(source_format)) {
                sink_audio_format = MIN(source_format, sink_dts_capability);
            } else {
                sink_audio_format = get_suitable_output_format(aml_out, source_format, sink_capability);
            }
            if (eDolbyMS12Lib == adev->dolby_lib_type && !is_dts_format(source_format)) {
                sink_audio_format = MIN(ms12_max_support_output_format(), sink_capability);
            }
            optical_audio_format = sink_audio_format;

            /*if the sink device only support pcm, we check whether we can output dd or dts to spdif,
             *For arc case, we can't support pcm and dd dual output, so we limit it to non arc case
             */
            if (bd_config->spdif_independent && ((adev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) == 0) && adev->dual_spdif_support) {
                if (sink_audio_format == AUDIO_FORMAT_PCM_16_BIT) {
                    if (is_dts_format(source_format)) {
                        optical_audio_format = MIN(source_format, AUDIO_FORMAT_DTS);
                    } else {
                        if (eDolbyMS12Lib == adev->dolby_lib_type) {
                            optical_audio_format = AUDIO_FORMAT_AC3;
                        } else {
                            optical_audio_format = MIN(source_format, AUDIO_FORMAT_AC3);
                        }
                    }
                }
            }

            optical_audio_format = reconfig_optical_audio_format(aml_out, optical_audio_format);
            break;
        case AML_DIGITAL_AUDIO_MODE_BYPASS:
            if (is_dts_format(source_format)) {
                sink_audio_format = MIN(source_format, sink_dts_capability);
            } else if (is_mpegh_format(source_format)) {
                sink_audio_format = sink_mpegh_capability;
            } else {
                sink_audio_format = get_suitable_output_format(aml_out, source_format, sink_capability);
            }
            optical_audio_format = sink_audio_format;
            /*if the sink device only support pcm, we check whether we can output dd or dts to spdif*/
            if (bd_config->spdif_independent && ((adev->cur_out_devices & AUDIO_DEVICE_OUT_HDMI_ARC) == 0) && adev->dual_spdif_support) {
                if (sink_audio_format == AUDIO_FORMAT_PCM_16_BIT) {
                    if (is_dts_format(source_format)) {
                        optical_audio_format = MIN(source_format, AUDIO_FORMAT_DTS);
                    } else {
                        optical_audio_format = MIN(source_format, AUDIO_FORMAT_AC3);
                    }
                }
            }

            break;
        default:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            break;
        }
        if (adev->enable_soundbar_mode) {
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            ALOGI("%s() enable_soundbar_mode %d sink_audio_format %#x optical_audio_format %#x",
                __FUNCTION__, adev->enable_soundbar_mode, sink_audio_format, optical_audio_format);
        }
    }
    /*when device is SPEAKER/HEADPHONE*/
    else {
        ALOGI("%s() SPEAKER/HEADPHONE case", __FUNCTION__);
        switch (adev->digital_audio_mode) {
        case AML_DIGITAL_AUDIO_MODE_PCM:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            break;
        case AML_DIGITAL_AUDIO_MODE_DD:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = AUDIO_FORMAT_AC3;
            if (dts_stream_active(adev)) {
                optical_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            }
            break;
        case AML_DIGITAL_AUDIO_MODE_AUTO:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = (source_format != AUDIO_FORMAT_DTS && source_format != AUDIO_FORMAT_DTS_HD)
                                   ? MIN(source_format, AUDIO_FORMAT_AC3)
                                   : AUDIO_FORMAT_DTS;

            if (eDolbyMS12Lib == adev->dolby_lib_type && !is_dts_format(source_format)) {
                optical_audio_format = AUDIO_FORMAT_AC3;
            }

            optical_audio_format = reconfig_optical_audio_format(aml_out, optical_audio_format);
            break;
        case AML_DIGITAL_AUDIO_MODE_BYPASS:
           sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
           if (is_dts_format(source_format)) {
               optical_audio_format = MIN(source_format, AUDIO_FORMAT_DTS);
           } else {
               optical_audio_format = MIN(source_format, AUDIO_FORMAT_AC3);
           }
           break;
        default:
            sink_audio_format = AUDIO_FORMAT_PCM_16_BIT;
            optical_audio_format = sink_audio_format;
            break;
        }
    }
    if (adev->sink_format != sink_audio_format) {
        adev->sink_format_changed = true;
    }
    adev->sink_format = sink_audio_format;
    adev->optical_format = optical_audio_format;
    adev->sink_max_channels = get_sink_format_max_channels(adev, adev->sink_format);

    /* set the dual output format flag */
    if (adev->sink_format != adev->optical_format) {
        aml_out->dual_output_flag = true;
    } else {
        aml_out->dual_output_flag = false;
    }
    AM_LOGI("sink_format:%s(%#x) max channel:%d optical_format:%s(%#x) dual_output %d",
           audioFormat2Str(adev->sink_format), adev->sink_format, adev->sink_max_channels,
           audioFormat2Str(adev->optical_format), adev->optical_format, aml_out->dual_output_flag);
    return ;
}





bool is_dual_output_stream(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *)stream;
    if (aml_out == NULL)
        return 0;

    return aml_out->dual_output_flag;
}






const char *write_func_strs[MIXER_WRITE_FUNC_MAX] = {
    "OUT_WRITE_NEW",
    "MIXER_AUX_BUFFER_WRITE_SM",
    "MIXER_MAIN_BUFFER_WRITE_SM",
    "MIXER_MMAP_BUFFER_WRITE_SM",
    "MIXER_AUX_BUFFER_WRITE",
    "MIXER_MAIN_BUFFER_WRITE",
    "MIXER_APP_BUFFER_WRITE",
    "PROCESS_BUFFER_WRITE,"
};

const char *write_func_to_str(enum stream_write_func func)
{
    return write_func_strs[func];
}

/*this should match with stream_status_t in audio_hw.h*/
const char *stream_status_2_string[STREAM_STATUS_MAX] = {
    "_STANDBY",
    "_HW_WRITING",
    "_MIXING",
    "_PAUSED"
};

void aml_stream_out_info_print(struct aml_stream_out *aml_out, uint64_t *frames, struct timespec *timestamp)
{
    struct aml_audio_device *adev = aml_out->dev;
    struct timespec *cur_timestamp = timestamp;

    uint64_t cur_info_time_in_ms = ((uint64_t)cur_timestamp->tv_sec * 1000 + (uint64_t)cur_timestamp->tv_nsec / 1000000);
    uint64_t last_info_timestamp_in_ms = ((uint64_t)aml_out->last_info_timestamp.tv_sec * 1000 + (uint64_t)aml_out->last_info_timestamp.tv_nsec / 1000000);

    int64_t time_gap = cur_info_time_in_ms - last_info_timestamp_in_ms;
    int64_t position_gap = (*frames - aml_out->last_frame_reported) / (aml_out->hal_rate / 1000);

    /* Print the audio stream out log if one of below conditions is true.
     *
     * 1. Print per 5 seconds.
     * 2. Time gap between last and current output system time exceeds threshold.
     * 3. The absolute value of jitter is over the threshold.
     * 4. Debug flag is enabled.
     */
    if (llabs(aml_out->jitter_ms) > JITTER_PRINT_THRESHOLD
        || cur_info_time_in_ms - aml_out->last_periodic_print_time_in_ms > 5000
        || adev->debug_flag > 1) {
        char *stream_type = audio_is_linear_pcm(aml_out->hal_format) ? "pcm" : "raw";
        char *sync_mode = aml_out->hw_sync_mode ? "tunnel" : "non tunnel";
        char *jitter_case = aml_out->jitter_ms >= 0 ?
            "Position gap is ahead of system time gap by" : "Position gap is behind system time gap by";

        ALOGI("%s: out:%p, stream_type:%s, sync_mode:%s, input_size:%"PRIu64" bytes\n"
                "%s: last_time:%"PRIu64" ms (sec:%ld, nsec:%ld), last_position:%"PRIu64" ms (%"PRIu64"), "
                "cur_time:%"PRIu64" ms (sec:%ld, nsec:%ld), cur_position:%"PRIu64" ms (%"PRIu64")\n"
                "%s: time_gap:%"PRId64" ms (thr:%d ms), position_gap:%"PRId64" ms, delay:%d ms, jitter: %s %"PRId64" ms (thr:%d ms)",
            __func__, aml_out, stream_type, sync_mode, aml_out->input_bytes_size,
            __func__, last_info_timestamp_in_ms, aml_out->last_info_timestamp.tv_sec,
            aml_out->last_info_timestamp.tv_nsec, aml_out->last_frame_reported / (aml_out->hal_rate / 1000), aml_out->last_frame_reported,
            cur_info_time_in_ms, cur_timestamp->tv_sec, cur_timestamp->tv_nsec, *frames / (aml_out->hal_rate / 1000), *frames,
            __func__, time_gap, INFO_TIME_PRINT_THRESHOLD, position_gap,
            aml_out->audio_delay / (aml_out->hal_rate / 1000), jitter_case, (int64_t)llabs(aml_out->jitter_ms), JITTER_PRINT_THRESHOLD);

        if (cur_info_time_in_ms - aml_out->last_periodic_print_time_in_ms > 5000) {
            aml_out->last_periodic_print_time_in_ms = cur_info_time_in_ms;
        }
    }

    aml_out->last_info_timestamp.tv_sec = cur_timestamp->tv_sec;
    aml_out->last_info_timestamp.tv_nsec = cur_timestamp->tv_nsec;

    return;
}


void aml_stream_out_dump(struct aml_stream_out *aml_out, int fd)
{
    if (aml_out) {
        dprintf(fd, "\t\t-usecase: %s\n", streamType2Str(aml_out->streamType));
        dprintf(fd, "\t\t-out device: %#x\n", aml_out->out_device);
        dprintf(fd, "\t\t-is tv source stream: %s\n", aml_out->is_tv_src_stream?"true":"false");
        dprintf(fd, "\t\t-stream status:%d %s\n", aml_out->stream_status, stream_status_2_string[aml_out->stream_status]);
        dprintf(fd, "\t\t-standby: %s\n", aml_out->standby?"true":"false");

        uint64_t frames;
        struct timespec timestamp;
        aml_out->stream.get_presentation_position((const struct audio_stream_out *)aml_out, &frames, &timestamp);
        dprintf(fd, "\t\t-presentation_position:%" PRIu64 "    | sec:%ld  nsec:%ld\n", frames, timestamp.tv_sec, timestamp.tv_nsec);
    }
}

void aml_adev_stream_out_dump(struct aml_audio_device *aml_dev, int fd) {
    dprintf(fd, "\n-------------[AML_HAL] aml TV out stream -------------------------\n");
    dprintf(fd, "[AML_HAL]    streamCount: %#x\n", aml_dev->streamCount);
    dprintf(fd, "[AML_HAL]    stream outs:\n");
    for (int i = 0; i < STREAM_TYPE_MAX ; i++) {
        struct aml_stream_out *aml_out = aml_dev->active_outputs[i];
        if (aml_out && aml_out->is_tv_src_stream) {
            dprintf(fd, "\tout: %d, pointer: %p\n", i, aml_out);
            aml_stream_out_dump(aml_out, fd);
        }
    }
}

int aml_dev_dump_latency(struct aml_audio_device *aml_dev, int fd)
{
    struct aml_stream_in *in = aml_dev->active_input;
    struct aml_audio_patch *patch = get_dev_patch(aml_dev);

    dprintf(fd, "\n-------------[AML_HAL] audio patch Latency-----------------------\n");

    if (patch && patch->patch_src != SRC_DTV) {
        aml_dev_sample_audio_path_latency(aml_dev, NULL);
        dprintf(fd, "[AML_HAL]      audio patch latency         : %6d ms\n", patch->audio_latency.ringbuffer_latency);
        dprintf(fd, "[AML_HAL]      audio spk tuning latency    : %6d ms\n", patch->audio_latency.user_tune_latency);
        dprintf(fd, "[AML_HAL]      MS12 buffer latency         : %6d ms\n", patch->audio_latency.ms12_latency);
        dprintf(fd, "[AML_HAL]      alsa out hw i2s latency     : %6d ms\n", patch->audio_latency.alsa_i2s_out_latency);
        dprintf(fd, "[AML_HAL]      alsa out hw spdif latency   : %6d ms\n", patch->audio_latency.alsa_spdif_out_latency);
        dprintf(fd, "[AML_HAL]      alsa in hw latency          : %6d ms\n\n", patch->audio_latency.alsa_in_latency);
        dprintf(fd, "[AML_HAL]      audio total latency         :%6d ms\n", patch->audio_latency.total_latency);

        int v_ltcy = aml_dev_sample_video_path_latency(patch);
        if (v_ltcy > 0) {
            dprintf(fd, "[AML_HAL]      video path total latency    : %6d ms\n", v_ltcy);
        } else {
            dprintf(fd, "[AML_HAL]      video path total latency    : N/A\n");
        }
    }
    return 0;
}

void aml_alsa_device_status_dump(struct aml_audio_device* aml_dev, int fd)
{
    dprintf(fd, "\n-------------[AML_HAL]  ALSA devices status ---------------------\n");
    bool stream_using = false;
    /* StreamOut using alsa devices list */
    for (int i = 0; i < ALSA_DEVICE_CNT; i++) {
        pthread_mutex_lock(&aml_dev->lock);
        pthread_mutex_lock(&aml_dev->alsa_pcm_lock);
        struct pcm *pcm_1 = aml_dev->pcm_handle[i];
        void *alsa_handle = aml_dev->alsa_handle[i];
        if (!pcm_1 && !alsa_handle) {
            pthread_mutex_unlock(&aml_dev->alsa_pcm_lock);
            pthread_mutex_unlock(&aml_dev->lock);
            continue;
        }

        if (!stream_using) {
            dprintf(fd, "  [AML_HAL] StreamOut using PCM list:\n");
            stream_using = true;
        }

        if (pcm_1) {
            aml_alsa_pcm_info_dump(pcm_1, fd);
        }

        if (alsa_handle) {
            struct pcm* pcm_2 = (struct pcm*) get_internal_pcm(alsa_handle);
            aml_alsa_pcm_info_dump(pcm_2, fd);
        }
        pthread_mutex_unlock(&aml_dev->alsa_pcm_lock);
        pthread_mutex_unlock(&aml_dev->lock);
    }
    if (!stream_using) {
        dprintf(fd, "  [AML_HAL] StreamOut using PCM list: None!\n");
    }

    /* Mixer outport using alsa devices list */
    dprintf(fd, "  [AML_HAL] mixer using PCM list:\n");
    mixer_using_alsa_device_dump(fd, aml_dev);

    /* StreamIn using alsa devices list */
    pthread_mutex_lock(&aml_dev->lock);
    stream_using = false;
    if (aml_dev->active_input) {
        struct pcm *pcm = aml_dev->active_input->pcm;
        if (pcm) {
            stream_using = true;
            dprintf(fd, "  [AML_HAL] StreamIn using PCM list:\n");
            aml_alsa_pcm_info_dump(pcm, fd);
        }
    }
    if (!stream_using) {
       dprintf(fd, "  [AML_HAL] StreamIn using PCM list: None!\n");
    }
    pthread_mutex_unlock(&aml_dev->lock);
}

void aml_decoder_info_dump(struct aml_audio_device *adev, int fd)
{
    dprintf(fd, "\n-------------[AML_HAL] licence decoder --------------------------\n");
    dprintf(fd, "[AML_HAL]    dolby_lib: %d\n", adev->dolby_lib_type);
    dprintf(fd, "[AML_HAL]    build ms12 version: %d\n", adev->support_ms12_version);
    dprintf(fd, "[AML_HAL]    dts_lib: %d\n", adev->dts_lib_type);
    dprintf(fd, "[AML_HAL]    MS12 library size:\n");
    dprintf(fd, "             \t-V2 Encrypted: %d\n", get_file_size("/oem/lib/ms12/libdolbyms12.so"));
    dprintf(fd, "             \t-V2 Decrypted: %d\n", get_file_size("/odm/lib/ms12/libdolbyms12.so"));
    dprintf(fd, "             \t-V1 Encrypted: %d\n", get_file_size("/oem/lib/libdolbyms12.so"));
    dprintf(fd, "             \t-V1 Decrypted: %d\n", get_file_size("/odm/lib/libdolbyms12.so"));
    dprintf(fd, "[AML_HAL]    DDP library size:\n");
    dprintf(fd, "             \t-lib32: %d\n", get_file_size("/odm/lib/libHwAudio_dcvdec.so"));
    dprintf(fd, "             \t-lib64: %d\n", get_file_size("/odm/lib64/libHwAudio_dcvdec.so"));
    dprintf(fd, "[AML_HAL]    DTS library size:\n");
    dprintf(fd, "             \t-lib32: %d\n", get_file_size("/odm/lib/libHwAudio_dtshd.so"));
    dprintf(fd, "             \t-lib64: %d\n", get_file_size("/odm/lib64/libHwAudio_dtshd.so"));
}

static void print_enum(struct mixer_ctl *ctl, int fd)
{
    unsigned int num_enums;
    unsigned int i;
    unsigned int value;
    const char *string;

    num_enums = mixer_ctl_get_num_enums(ctl);
    value = mixer_ctl_get_value(ctl, 0);

    for (i = 0; i < num_enums; i++) {
        string = mixer_ctl_get_enum_string(ctl, i);
        dprintf(fd, "%s%s, ", value == i ? "> " : "", string);
    }
}

static void print_control_values(struct mixer_ctl *control, int fd)
{
    enum mixer_ctl_type type;
    unsigned int num_values;
    unsigned int i;
    int min, max;
    int ret;
    char *buf = NULL;

    type = mixer_ctl_get_type(control);
    num_values = mixer_ctl_get_num_values(control);

    if ((type == MIXER_CTL_TYPE_BYTE) && (num_values > 0)) {
        buf = calloc(1, num_values);
        if (buf == NULL) {
            ALOGE("Failed to alloc mem for bytes %u", num_values);
            return;
        }

        ret = mixer_ctl_get_array(control, buf, num_values);
        if (ret < 0) {
            ALOGE("Failed to mixer_ctl_get_array");
            free(buf);
            return;
        }
    }

    for (i = 0; i < num_values; i++) {
        switch (type)
        {
        case MIXER_CTL_TYPE_INT:
            dprintf(fd,"%d", mixer_ctl_get_value(control, i));
            break;
        case MIXER_CTL_TYPE_BOOL:
            dprintf(fd,"%s", mixer_ctl_get_value(control, i) ? "On" : "Off");
            break;
        case MIXER_CTL_TYPE_ENUM:
            print_enum(control, fd);
            break;
        case MIXER_CTL_TYPE_BYTE:
            dprintf(fd,"%02hhx", buf[i]);
            break;
        default:
            dprintf(fd,"unknown");
            break;
        };
        if ((i + 1) < num_values) {
           dprintf(fd, ", ");
        }
    }

    if (type == MIXER_CTL_TYPE_INT) {
        min = mixer_ctl_get_range_min(control);
        max = mixer_ctl_get_range_max(control);
        dprintf(fd, " (range %d->%d)", min, max);
    }

    free(buf);
}

void aml_alsa_mixer_status_dump(struct aml_audio_device *adev, int fd)
{
    dprintf(fd, "\n-------------[AML_HAL] ALSA mixer status ------------------------\n");

    struct mixer_ctl *ctl;
    const char *name, *type;
    unsigned int num_ctls, num_values;
    unsigned int i;
    struct aml_mixer_handle *aml_mixer = &adev->alsa_mixer;

    if (!aml_mixer->pMixer) {
        ALOGW("%s() Warning! mixer = NULL!, return!", __func__);
        return;
    }

    num_ctls = mixer_get_num_ctls(aml_mixer->pMixer);

    dprintf(fd,"Number of controls: %u\n", num_ctls);

    dprintf(fd,"ctl\ttype\tnum\t%-40svalue\n", "name");

    for (i = 0; i < num_ctls; i++) {
        ctl = mixer_get_ctl(aml_mixer->pMixer, i);  //ask one mixer_ctrl

        name = mixer_ctl_get_name(ctl);
        type = mixer_ctl_get_type_string(ctl);
        num_values = mixer_ctl_get_num_values(ctl);
        dprintf(fd, "%u\t%s\t%u\t%-40s", i, type, num_values, name);

        pthread_mutex_lock(&aml_mixer->lock);
        print_control_values(ctl, fd);
        pthread_mutex_unlock(&aml_mixer->lock);
        dprintf(fd, "\n");
    }
}


bool is_use_spdifb(struct aml_stream_out *out) {
    struct aml_audio_device *adev = out->dev;
    /*this patch is for DCV DDP noise.
    **DCV have two kinds of mode, DCV decoder and passthrough.
    **the dolby_decode_enable is 0 when DCV passthrough.
    **so here should remove the dolby_decode_enable judgment.
    */
    if (eDolbyDcvLib == adev->dolby_lib_type /*&& adev->dolby_decode_enable*/ &&
        (out->hal_format == AUDIO_FORMAT_E_AC3 || out->hal_internal_format == AUDIO_FORMAT_E_AC3 ||
        (out->need_convert && out->hal_internal_format == AUDIO_FORMAT_AC3))) {
        /*dual spdif we need convert
          or non dual spdif, we need check audio setting and optical format*/
        if (adev->dual_spdif_support) {
            out->dual_spdif = true;
        }
        if (out->dual_spdif && ((adev->digital_audio_mode == AML_DIGITAL_AUDIO_MODE_AUTO) &&
            adev->optical_format == AUDIO_FORMAT_E_AC3) &&
            out->hal_rate != 32000) {
            return true;
        }
    }

    return false;
}

bool is_dolby_ms12_support_compression_format(audio_format_t format)
{

#ifdef MS12_V24_ENABLE
    if (format == AUDIO_FORMAT_HE_AAC_V1 ||
        format == AUDIO_FORMAT_HE_AAC_V2 ||
        format == AUDIO_FORMAT_AAC ||
        format == AUDIO_FORMAT_AAC_LATM)  {
        if (property_get_bool("ro.vendor.audio.use.ms12heaac", true)) {
            return true;
        }
    }
#endif

    return (format == AUDIO_FORMAT_AC3 ||
            format == AUDIO_FORMAT_E_AC3 ||
            format == AUDIO_FORMAT_E_AC3_JOC ||
            format == AUDIO_FORMAT_DOLBY_TRUEHD ||
            format == AUDIO_FORMAT_AC4 ||
            format == AUDIO_FORMAT_MAT);
}

bool is_dolby_ddp_support_compression_format(audio_format_t format)
{
    return (format == AUDIO_FORMAT_AC3 ||
            format == AUDIO_FORMAT_E_AC3 ||
            format == AUDIO_FORMAT_E_AC3_JOC);
}

bool is_direct_stream_and_pcm_format(struct aml_stream_out *out)
{
    return audio_is_linear_pcm(out->hal_internal_format) && (out->flags & AUDIO_OUTPUT_FLAG_DIRECT);
}

bool is_mmap_stream_and_pcm_format(struct aml_stream_out *out)
{
    return audio_is_linear_pcm(out->hal_internal_format) && (out->flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ);
}

void get_audio_indicator(struct aml_audio_device *dev, char *temp_buf) {
    struct aml_audio_device *adev = (struct aml_audio_device *) dev;

    if (adev->audio_hal_info.update_type == TYPE_PCM)
        sprintf (temp_buf, "audioindicator=");
    else if (adev->audio_hal_info.update_type == TYPE_DTS_EXPRESS)
        sprintf (temp_buf, "audioindicator=DTS EXPRESS");
    else if (adev->audio_hal_info.update_type == TYPE_AC3)
        sprintf (temp_buf, "audioindicator=Dolby AC3");
    else if (adev->audio_hal_info.update_type == TYPE_EAC3)
        sprintf (temp_buf, "audioindicator=Dolby EAC3");
    else if (adev->audio_hal_info.update_type == TYPE_AC4)
        sprintf (temp_buf, "audioindicator=Dolby AC4");
    else if (adev->audio_hal_info.update_type == TYPE_MAT)
        sprintf (temp_buf, "audioindicator=Dolby MAT");
    else if (adev->audio_hal_info.update_type == TYPE_TRUE_HD)
        sprintf (temp_buf, "audioindicator=Dolby THD");
    else if (adev->audio_hal_info.update_type == TYPE_DDP_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby EAC3,Dolby Atmos");
    else if (adev->audio_hal_info.update_type == TYPE_TRUE_HD_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby THD,Dolby Atmos");
    else if (adev->audio_hal_info.update_type == TYPE_MAT_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby MAT,Dolby Atmos");
    else if (adev->audio_hal_info.update_type == TYPE_AC4_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby AC4,Dolby Atmos");
    else if (adev->audio_hal_info.update_type == TYPE_DTS)
        sprintf (temp_buf, "audioindicator=DTS");
    else if (adev->audio_hal_info.update_type == TYPE_DTS_HD_MA)
        sprintf (temp_buf, "audioindicator=DTS HD");
    else if (adev->audio_hal_info.update_type == TYPE_DDP_ATMOS_PROMPT_ON_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby EAC3");
    else if (adev->audio_hal_info.update_type == TYPE_TRUE_HD_ATMOS_PROMPT_ON_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby THD");
    else if (adev->audio_hal_info.update_type == TYPE_MAT_ATMOS_PROMPT_ON_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby MAT");
    else if (adev->audio_hal_info.update_type == TYPE_AC4_ATMOS_PROMPT_ON_ATMOS)
        sprintf (temp_buf, "audioindicator=Dolby AC4");
    else if (adev->audio_hal_info.update_type == TYPE_HEAAC)
        sprintf (temp_buf, "audioindicator=HEAAC");
    else if (adev->audio_hal_info.update_type == TYPE_AAC)
        sprintf (temp_buf, "audioindicator=AAC");

    ALOGI("%s(), [%s]", __func__, temp_buf);
}

static int update_audio_hal_info(struct aml_audio_device *adev, audio_format_t format, int atmos_flag)
{
    struct dolby_ms12_desc *ms12 = &(adev->ms12);
    int update_type = get_codec_type(format);
    int update_threshold = DOLBY_FMT_UPDATE_THRESHOLD;
    int cur_aml_dap_surround_virtualizer = dolby_ms12_get_dap_surround_virtualizer();
    bool is_headphone_x = 0;
    bool is_dolby_atmos_off = 0;

    if (is_dolby_ms12_support_compression_format(format)) {
        update_threshold = DOLBY_FMT_UPDATE_THRESHOLD;
        if ((format == AUDIO_FORMAT_AC4) && (!is_same_patch_src(adev, SRC_DTV))) {
            update_threshold = DOLBY_AC4_FMT_UPDATE_THRESHOLD;
        }
    } else if (is_dts_format(format)) {
        update_threshold = DTS_FMT_UPDATE_THRESHOLD;
    }

    /* avoid the value out-of-bounds */
    if (adev->audio_hal_info.update_cnt < FMT_UPDATE_THRESHOLD_MAX) {
        adev->audio_hal_info.update_cnt++;
    }

    /* Check whether the update_type is stable or not as bellow. */
    bool is_virt_updated_off_vs_on_auto = (!!adev->audio_hal_info.aml_dap_surround_virtualizer != !!cur_aml_dap_surround_virtualizer);
    bool is_virt_updated_for_aml_atmos = (atmos_flag && is_virt_updated_off_vs_on_auto);

    if ((format != adev->audio_hal_info.format) ||
        (atmos_flag != adev->audio_hal_info.is_dolby_atmos) ||
        is_virt_updated_for_aml_atmos) {
        adev->audio_hal_info.update_cnt = 0;
    }

    /* @audio_format_t does not include dts_express.
     * So we get the format(dts_express especially) from the decoder(@dts_hd.stream_type).
     * @dts_hd.stream_type is updated after decoding at least one frame.
     */
    if (is_dts_format(format)) {
        if (adev->dts_lib_type == eDTSXLib) {
            if (adev->dts_x.stream_type <= 0 /*TYPE_PCM*/) {
                adev->audio_hal_info.update_cnt = 0;
            }
            update_type = adev->dts_x.stream_type;
        } else {
            if (adev->dts_hd.stream_type <= 0 /*TYPE_PCM*/) {
                adev->audio_hal_info.update_cnt = 0;
            }
            update_type = adev->dts_hd.stream_type;
        }

        if (update_type != adev->audio_hal_info.update_type) {
            adev->audio_hal_info.update_cnt = 0;
        }
    }

    if (adev->board_config.dolby_ms12_audio_config != MS12_CONFIG_Z) {
        is_dolby_atmos_off = 1;
    } else {
        is_dolby_atmos_off = (MS12_DAP_SPEAKER_VIRTUALIZER_OFF == cur_aml_dap_surround_virtualizer);
    }

    if (atmos_flag == 1) {
        if (format == AUDIO_FORMAT_E_AC3)
            update_type = (is_dolby_atmos_off) ? TYPE_DDP_ATMOS_PROMPT_ON_ATMOS : TYPE_DDP_ATMOS;
        else if (format == AUDIO_FORMAT_DOLBY_TRUEHD)
            update_type = (is_dolby_atmos_off) ? TYPE_TRUE_HD_ATMOS_PROMPT_ON_ATMOS : TYPE_TRUE_HD_ATMOS;
        else if (format == AUDIO_FORMAT_MAT)
            update_type = (is_dolby_atmos_off) ? TYPE_MAT_ATMOS_PROMPT_ON_ATMOS : TYPE_MAT_ATMOS;
        else if (format == AUDIO_FORMAT_AC4)
            update_type = (is_dolby_atmos_off) ? TYPE_AC4_ATMOS_PROMPT_ON_ATMOS : TYPE_AC4_ATMOS;
    }

    ALOGV("%s() update_cnt %d format %#x vs hal_internal_format %#x  atmos_flag %d vs is_dolby_atmos %d update_type %d\n",
        __FUNCTION__, adev->audio_hal_info.update_cnt, format, adev->audio_hal_info.format,
        atmos_flag, adev->audio_hal_info.is_dolby_atmos, update_type);

    if ((atmos_flag == 1) && (is_dolby_atmos_off == 0)) {
        adev->atmos_indicator_status = true;
    } else {
        adev->atmos_indicator_status = false;
    }

    adev->audio_hal_info.format = format;
    adev->audio_hal_info.is_dolby_atmos = atmos_flag;
    adev->audio_hal_info.update_type = update_type;
    adev->audio_hal_info.aml_dap_surround_virtualizer = cur_aml_dap_surround_virtualizer;

    if (adev->audio_hal_info.update_cnt == update_threshold) {
        if (adev->dts_lib_type == eDTSXLib) {
            is_headphone_x = adev->dts_x.is_headphone_x;
        } else {
            is_headphone_x = adev->dts_hd.is_headphone_x;
        }

        if (is_dts_format(format) && is_headphone_x) {
            aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, TYPE_DTS_HP);
        }

        ALOGD("%s() audio hal format change to %x, atmos flag = %d, is_dolby_atmos = %d, dts_hp_x = %d, update_type = %d is_dolby_atmos_off = %d virtual_bass_enable %d\n",
            __FUNCTION__, adev->audio_hal_info.format, adev->audio_hal_info.is_dolby_atmos, adev->ms12.focus_is_dolby_atmos,
            is_headphone_x, adev->audio_hal_info.update_type, is_dolby_atmos_off, get_ms12_dap_virtual_bass_enable());

        ALOGD("%s() cur_out_devices %#x, dap_bypass_enable = %d, is_ms12_tuning_dat = %d, dolby_ms12_enable = %d, output_config = %#x\n",
            __FUNCTION__, adev->cur_out_devices, adev->ms12.dap_bypass_enable, adev->is_ms12_tuning_dat, ms12->dolby_ms12_enable, ms12->output_config);

        /* This dolby audio infor is required by the info bar. */
        /* if (eDolbyMS12Lib == adev->dolby_lib_type && adev->enable_soundbar_mode && ((update_type >= TYPE_AC3) && (update_type <=TYPE_MAT))) {
            ALOGD("%s() MS12 inside, enable_soundbar_mode %d, update_type = %d, DONOT popup the Dolby Audio in Config Z device!\n",
                __FUNCTION__, adev->enable_soundbar_mode, update_type);
            return 0;
        }*/

        aml_mixer_ctrl_set_int(&adev->alsa_mixer, AML_MIXER_ID_AUDIO_HAL_FORMAT, update_type);
    }

    return 0;
}

void update_audio_format(struct aml_audio_device *adev, audio_format_t format)
{
    int atmos_flag = 0;
    int update_type = TYPE_PCM;
    bool is_dolby_active = dolby_stream_active(adev);
    bool is_dolby_format = is_dolby_ms12_support_compression_format(format);
    bool is_dts_active = dts_stream_active(adev);
    bool is_dolby_atmos = 0;

    if (adev->ms12.focus_is_dolby_atmos) {
        is_dolby_atmos = 1;
    }
    /*
     *for dolby & pcm case or dolby case
     *to update the dolby stream's format
     */
    if (is_dolby_active && is_dolby_format) {
        if (eDolbyMS12Lib == adev->dolby_lib_type) {
            atmos_flag = is_dolby_atmos;
        }

#ifdef MS12_V24_ENABLE
        /* when ms12 is config x or y, or DAP is not in audio postprocessing, there is no ATMOS Experience */
        if ((adev->board_config.dolby_ms12_audio_config != MS12_CONFIG_Z) || (is_audio_postprocessing_add_dolbyms12_dap(adev) == 0)) {
            atmos_flag = 0;
        }
#else
        atmos_flag = 0; /* Todo: MS12 V1, how to indicate the Dolby ATMOS? */
#endif

        update_audio_hal_info(adev, format, atmos_flag);
    }
    /*
     *to update the audio format for other cases
     *DTS-format / DTS format & Mixer-PCM
     *only Mixer-PCM
     */
    else if (!is_dolby_active && !is_dolby_format) {

        /* if there is DTS/DTS alive, only update DTS/DTS-HD */
        if (is_dts_active) {
            if (is_dts_format(format)) {
                update_audio_hal_info(adev, format, false);
            }
            /*else case is PCM format, will ignore that PCM*/
        }

        /* there is none-dolby or none-dts alive, then update PCM format*/
        if (!is_dts_active) {
            update_audio_hal_info(adev, format, false);
        }
    }
    /*
     * **Dolby stream is active, and get the Mixer-PCM case steam format,
     * **we should ignore this Mixer-PCM update request.
     * else if (is_dolby_active && !is_dolby_format) {
     * }
     * **If Dolby steam is not active, the available format is LPCM or DTS
     * **The following case do not exit at all **
     //else //(!is_dolby_active && is_dolby_format) {
     * }
     */
}

int update_sink_format_after_hotplug(struct aml_audio_device *adev)
{
    struct audio_stream_out *stream = NULL;
    /* raw stream is at high priority */
    if (adev->active_outputs[STREAM_RAW_DIRECT]) {
        stream = (struct audio_stream_out *)adev->active_outputs[STREAM_RAW_DIRECT];
    }
    else if (adev->active_outputs[STREAM_RAW_HWSYNC]) {
        stream = (struct audio_stream_out *)adev->active_outputs[STREAM_RAW_HWSYNC];
    }
    /* pcm direct/hwsync stream is at medium priority */
    else if (adev->active_outputs[STREAM_PCM_HWSYNC]) {
        stream = (struct audio_stream_out *)adev->active_outputs[STREAM_PCM_HWSYNC];
    }
    else if (adev->active_outputs[STREAM_PCM_DIRECT]) {
        stream = (struct audio_stream_out *)adev->active_outputs[STREAM_PCM_DIRECT];
    }
    /* pcm stream from mixer is at lower priority */
    else if (adev->active_outputs[STREAM_PCM_NORMAL]){
        stream = (struct audio_stream_out *)adev->active_outputs[STREAM_PCM_NORMAL];
    }


    if (stream) {
        ALOGD("%s() active stream %p\n", __FUNCTION__, stream);
        get_sink_format(stream);
    }
    else {
        if (adev->ms12_out && continuous_mode(adev)) {
            ALOGD("%s() active stream is ms12_out %p\n", __FUNCTION__, adev->ms12_out);
            get_sink_format((struct audio_stream_out *)adev->ms12_out);
        }
        else {
            ALOGD("%s() active stream %p ms12_out %p\n", __FUNCTION__, stream, adev->ms12_out);
        }
    }
    if (eDolbyMS12Lib == adev->dolby_lib_type) {
        audiohal_send_msg_2_ms12(&adev->ms12, MS12_MESG_TYPE_RESET_MS12_ENCODER);
    }

    return 0;
}

bool is_dtv_stream_out(struct audio_stream_out *stream)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    aml_audio_buffer_info_t *pBuffer = (aml_audio_buffer_info_t *)aml_out->audio_buffer;
    aml_audio_buffer_t *audioBuffer = NULL;
    if (pBuffer) {
        audioBuffer = pBuffer->inBuffer;
    }
    if (audioBuffer) {
        return aml_out->is_dtv_src_stream && audioBuffer->isDtv;
    }
    return false;
}

bool is_tv_stream_out(struct aml_stream_out *aml_out)
{
   bool is_tv_source = aml_out->is_tv_src_stream || aml_out->is_dtv_src_stream;
   return is_tv_source;
}

uint32_t tv_in_write(struct audio_stream_out *stream, const void* buffer, size_t bytes)
{
    R_CHECK_POINTER_LEGAL(bytes, stream, "");
    R_CHECK_POINTER_LEGAL(bytes, buffer, "");
    struct aml_stream_out *out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = out->dev;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    R_CHECK_POINTER_LEGAL(bytes, patch, "");
    if (bytes == 0 || patch->tvin_buffer_inited != 1) {
        return bytes;
    }

    uint32_t bytes_written = 0;
    uint32_t wr_size = bytes;
    uint32_t full_count = 0;
    while (bytes_written < bytes) {
        uint32_t sent = ring_buffer_write(&patch->tvin_ringbuffer, (uint8_t *)buffer + bytes_written, bytes - bytes_written, UNCOVER_WRITE);
        AM_LOGV("need_write:%zu, actual sent:%d, bytes:%zu", (bytes - bytes_written), sent, bytes);
        bytes_written += sent;
        if (bytes_written == bytes) {
            if (adev->debug_flag) {
                AM_LOGD("write finished, bytes:%zu, timeout:%d ms", bytes, 5 * full_count);
            }
            return bytes;
        }
        full_count++;
        if (full_count >= 20) {
            AM_LOGW("write data timeout 100ms, need write:%zu, bytes_written:%d, reset buffer", bytes, bytes_written);
            ring_buffer_reset(&patch->tvin_ringbuffer);
            return bytes_written;
        }
        usleep(5000);
    }
    return bytes;
}

uint32_t tv_in_read(struct audio_stream_in *stream, void* buffer, size_t bytes)
{
    R_CHECK_POINTER_LEGAL(bytes, stream, "");
    R_CHECK_POINTER_LEGAL(bytes, buffer, "");
    struct aml_stream_in *in = (struct aml_stream_in *)stream;
    struct aml_audio_device *adev = in->dev;
    struct aml_audio_patch *patch = get_dev_patch(adev);
    R_CHECK_POINTER_LEGAL(bytes, patch, "");
    if (bytes == 0 || patch->tvin_buffer_inited != 1) {
        memset(buffer, 0, bytes);
        return bytes;
    }

    uint32_t read_bytes = 0;
    uint32_t nodata_count = 0;
    while (read_bytes < bytes) {
        uint32_t ret = ring_buffer_read(&patch->tvin_ringbuffer, (uint8_t *)buffer + read_bytes, bytes - read_bytes);
        AM_LOGV("need_write:%zu, actual sent:%d, bytes:%zu", (bytes - read_bytes), ret, bytes);
        read_bytes += ret;
        if (read_bytes == bytes) {
            if (adev->debug_flag) {
                int available = get_buffer_read_space(&patch->tvin_ringbuffer);
                AM_LOGD("read finished, bytes:%zu, timeout:%d ms, available:%d", bytes, 5 * nodata_count, available);
            }
            return bytes;
        }
        nodata_count++;
        if (nodata_count >= 20) {
            AM_LOGW("read data timeout 100ms, need:%zu, read_bytes:%d", bytes, read_bytes);
            memset(buffer, 0, bytes);
            return bytes;
        }
        usleep(5000);
    }
    return bytes;
}

int aml_deinit_audio_buffer(struct aml_stream_out *out)
{
    if (out && out->audio_buffer) {
        aml_audio_buffer_info_t *pBuffer = out->audio_buffer;
        int index = -1;
        for (index = 0; index < AUDIO_BUFFER_MAX; ++index) {
            if (pBuffer->outBuffer[index]) {
                aml_audio_free(pBuffer->outBuffer[index]);
                pBuffer->outBuffer[index] = NULL;
            }
        }
        if (out->parsedDataBuf) {
            aml_audio_free(out->parsedDataBuf);
            out->parsedDataBuf = NULL;
        }
        if (pBuffer->parsedBuffer) {
            aml_audio_free(pBuffer->parsedBuffer);
            pBuffer->parsedBuffer = NULL;
        }
        if (pBuffer->inBuffer) {
            aml_audio_free(pBuffer->inBuffer);
            pBuffer->inBuffer = NULL;
        }
        aml_audio_free(out->audio_buffer);
        out->audio_buffer = NULL;
    }
    return 0;
}

int aml_init_audio_buffer(struct aml_stream_out *out)
{
    int ret = 0;

    out->audio_buffer = aml_audio_calloc(1, sizeof(aml_audio_buffer_info_t));
    if (out->audio_buffer == NULL) {
        AM_LOGE("alloc audio_buffer failed, errno:%d %s\n", errno, strerror(errno));
        ret = -1;
        goto alloc_err;
    } else {
        aml_audio_buffer_info_t *pBuffer = (struct aml_audio_buffer_info *)out->audio_buffer;
        pBuffer->inBuffer = (struct aml_audio_buffer *)aml_audio_calloc(1, sizeof(aml_audio_buffer_t));
        if (pBuffer->inBuffer == NULL) {
            AM_LOGE("alloc pBuffer->inBuffer failed, errno:%d %s\n", errno, strerror(errno));
            ret = -1;
            goto alloc_err;
        }
        pBuffer->parsedBuffer = (struct aml_audio_buffer *)aml_audio_calloc(1, sizeof(aml_audio_buffer_t));
        if (pBuffer->parsedBuffer == NULL) {
            AM_LOGE("alloc pBuffer->parsedBuffer failed, errno:%d %s\n", errno, strerror(errno));
            ret = -1;
            goto alloc_err;
        } else {
            out->parsedDataBuf = aml_audio_calloc(1, AUDIO_BUFFER_DATA_SIZE);
            if (out->parsedDataBuf == NULL) {
                AM_LOGE("alloc out->parsedDataBuf failed, errno:%d %s\n", errno, strerror(errno));
                ret = -1;
                goto alloc_err;
            }
        }
        int index = -1;
        for (index = 0; index < AUDIO_BUFFER_MAX; ++index) {
            pBuffer->outBuffer[index] = (struct aml_audio_buffer *)aml_audio_calloc(1, sizeof(aml_audio_buffer_t));
            if (pBuffer->outBuffer[index] == NULL) {
                AM_LOGE("alloc pBuffer->outBuffer failed, errno:%d %s\n", errno, strerror(errno));
                ret = -1;
                goto alloc_err;
            }
        }
    }

    return ret;
alloc_err:
    aml_deinit_audio_buffer(out);
    return ret;
}

int aml_audio_earctx_get_type(struct aml_audio_device *adev)
{
    int attend_type = 0;

    attend_type = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_ATTENDED_TYPE);
    return attend_type;
}

int aml_audio_earcrx_get_type(struct aml_audio_device *adev)
{
    int attend_type = 0;

    attend_type = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_RX_ATTENDED_TYPE);
    return attend_type;
}

int aml_audio_earc_get_latency(struct aml_audio_device *adev)
{
    int latency = 0;

    latency = aml_mixer_ctrl_get_int(&adev->alsa_mixer, AML_MIXER_ID_EARC_TX_LATENCY);
    return latency;
}

#define AML_DETECT_VALUE 1500

void tv_do_ease(struct aml_stream_out *out, void *write_buf, size_t write_bytes)
{
    struct aml_audio_device *aml_dev = out->dev;
    int fade_at_where = property_get_int32("vendor.dtv.audio.fade_mode", DO_FADE_AT_HAL);
    bool is_ms12_case = (eDolbyMS12Lib == aml_dev->dolby_lib_type);
    switch (fade_at_where) {
        case DO_FADE_AT_ALSA:
            if (out->ease_config.ease_mode == EaseIn) {
                 ALOGI("start fade in fade_mode %d", fade_at_where);
                 set_output_device_mute(aml_dev, AUDIO_DEVICE_OUT_SPEAKER, false, true);
            } else if (out->ease_config.ease_mode == EaseOut) {
                 ALOGI("start fade out fade_mode %d", fade_at_where);
                 set_output_device_mute(aml_dev, AUDIO_DEVICE_OUT_SPEAKER, true, true/*use fade*/);
            }
            out->ease_config.ease_mode = Invalid;
            break;
        case DO_FADE_AT_HAL:
            if (is_ms12_case &&
                is_dolby_ms12_support_compression_format(out->hal_internal_format)) {
                ALOGV("dolby raw data, no need check ");
            } else {
                int ret = aml_audio_data_detect((int16_t *)write_buf, write_bytes , AML_DETECT_VALUE);
                if (ret == true)  {
                    return;
                }
            }
            if (is_ms12_case) {
                if (out->ease_config.ease_mode == EaseOut) {
                    set_ms12_decoder_mute(&out->stream, true, out->ease_config.duration_ms);
                } else if (out->ease_config.ease_mode == EaseIn) {
                    set_ms12_decoder_mute(&out->stream, false, out->ease_config.duration_ms);
                }
            } else {
                if (out->ease_config.ease_mode == EaseOut) {
                    ALOGI("start fade out fade_mode %d", fade_at_where);
                    start_ease_out(out->audio_stream_ease, is_TV(aml_dev), out->ease_config.duration_ms);
                } else if (out->ease_config.ease_mode == EaseIn) {
                    ALOGI("start fade in fade_mode %d", fade_at_where);
                    start_ease_in(out->audio_stream_ease, is_TV(aml_dev), out->ease_config.duration_ms);
                }
                aml_audio_ease_process(out->audio_stream_ease, write_buf, write_bytes, false);
            }
            out->ease_config.ease_mode = Invalid;
            break;
        default:
            ALOGW("invalid fade mode %d", fade_at_where);
    }

}

void tv_set_ease(struct aml_stream_out *out, int ease_mode)
{

   if (!out) {
        ALOGE("out stream NULL ,return");
        return;
    }
    struct aml_audio_device *aml_dev = out->dev;
    int fade_at_where = property_get_int32("vendor.dtv.audio.fade_mode", DO_FADE_AT_HAL);
    int duration_ms = 0;
    bool need_do_fade = false;
    bool is_ms12_case = (eDolbyMS12Lib == aml_dev->dolby_lib_type);


    switch (fade_at_where) {
        case DO_FADE_AT_ALSA:
            if (ease_mode == EaseOut) {
                out->ease_config.ease_mode = EaseOut;
                aml_audio_sleep(15000);
            } else if (ease_mode == EaseIn){
                out->ease_config.ease_mode = EaseIn;
            }
            break;
        case DO_FADE_AT_HAL:

            if (ease_mode == EaseOut) {
                if (is_ms12_case) {
                    need_do_fade = !aml_dev->ms12.is_muted;
                } else {
                    if (aml_dev->audio_ease) {
                        float vol_now = aml_audio_ease_get_current_volume(out->audio_stream_ease);
                        need_do_fade = (vol_now != 0.0f);
                    }
                }
                if (!need_do_fade) {
                    ALOGI("%s()skip fade out", __func__);
                    break;
                } else {
                    if (is_TV(aml_dev)) {
                        if (is_ms12_case) {
                            duration_ms = property_get_int32("vendor.media.audio.dtv.fadeout.us", MS12_AUDIO_FADEOUT_TV_DURATION_US) / 1000;
                        } else {
                            duration_ms = property_get_int32("vendor.media.audio.dtv.fadeout.us", AUDIO_FADEOUT_TV_DURATION_US) / 1000;
                        }
                    } else {
                        duration_ms = property_get_int32("vendor.media.audio.dtv.fadeout.us", AUDIO_FADEOUT_STB_DURATION_US) / 1000;
                    }
                    out->ease_config.ease_mode = EaseOut;
                    out->ease_config.duration_ms = duration_ms;
                    aml_audio_sleep((duration_ms + 10)* 1000);
                }
            } else if (ease_mode == EaseIn) {
                if (is_ms12_case) {
                    /* check if decode is muted or not, if muted, do fade in.
                     */
                    need_do_fade = out->is_decoder_muted;
                } else {
                    if (aml_dev->audio_ease) {
                        float vol_now = aml_audio_ease_get_current_volume(out->audio_stream_ease);
                        need_do_fade = (vol_now == 0.0f);
                    }
                }
                if (!need_do_fade) {
                   ALOGI("%s()skip fade in", __func__);
                } else {
                     if (is_TV(aml_dev)) {
                        if (is_ms12_case) {
                            duration_ms = property_get_int32("vendor.media.audio.dtv.fadein.us", MS12_AUDIO_FADEIN_TV_DURATION_US) / 1000;
                        } else {
                            duration_ms = property_get_int32("vendor.media.audio.dtv.fadein.us", NON_MS12_AUDIO_FADEIN_TV_DURATION_US) / 1000;
                        }
                    } else {
                        duration_ms = property_get_int32("vendor.media.audio.dtv.fadein.us", AUDIO_FADEIN_STB_DURATION_US) / 1000;
                    }
                    out->ease_config.ease_mode = EaseIn;
                    out->ease_config.duration_ms = duration_ms;
                }
            }
            break;
        default:
        ALOGW("invalid fade mode %d", fade_at_where);
    }

}

int set_device_control(struct audio_hw_device *dev, struct str_parms *parms)
{
    struct aml_audio_device *adev = (struct aml_audio_device *)dev;
    int ret = -1, val = 0;

    ret = str_parms_get_int(parms, "hp_mute", &val);
    if (ret >= 0) {
        int dac_value[2] = {251, 251};
        if (val > 0) {
            int mute[2] = {0,0};
            aml_mixer_ctrl_set_array(&adev->alsa_mixer, AML_MIXER_ID_DAC_PLAYBACK_VOLUME, &mute, 2);
            ALOGI("set hp mute,set dac to 0");
        } else {
            dac_value[0] = dac_value[1] = adev->dac_value;
            aml_mixer_ctrl_set_array(&adev->alsa_mixer, AML_MIXER_ID_DAC_PLAYBACK_VOLUME, &dac_value, 2);
            ALOGI("set hp unmute,set dac to dac_value[0]:%d,dac_value[1]:%d",dac_value[0], dac_value[1]);
        }
        goto exit;
    }
    ret = str_parms_get_int(parms, "set_hp_vol", &val);
    if (ret >= 0) {
        adev->sink_gain[OUTPORT_HEADPHONE] = DbToAmpl(val / 100.0);
        ALOGI("set set_hp_vol = %f", adev->sink_gain[OUTPORT_HEADPHONE]);
        goto exit;
    }
    ret = str_parms_get_int(parms, "set_bt_vol", &val);
    if (ret >= 0) {
        if (adev->bt_avrcp_supported)
            adev->sink_gain[OUTPORT_A2DP] = 1.0;
        else
            adev->sink_gain[OUTPORT_A2DP] = DbToAmpl(val / 100.0);
        ALOGI("bt_avrcp_supported:%d,set set_bt_vol = %f", adev->bt_avrcp_supported, adev->sink_gain[OUTPORT_A2DP]);
        adev->a2dp_vol = adev->sink_gain[OUTPORT_A2DP];
        goto exit;
    }
    ret = str_parms_get_int(parms, "a2dp_mute", &val);
    if (ret >= 0) {
        if (val > 0) {
            adev->sink_gain[OUTPORT_A2DP] = 0.0;
            ALOGI("set a2dp mute,a2dp gain is %f", adev->sink_gain[OUTPORT_A2DP]);
        } else {
            adev->sink_gain[OUTPORT_A2DP] = adev->a2dp_vol;
            ALOGI("set a2dp unmute,resume a2dp gain:%f",adev->sink_gain[OUTPORT_A2DP]);
        }
        goto exit;
    }
exit:
    return ret;
}

/*
 *@brief handle audio info change mask
 * OUTPUT_LATENCY_CHANGE   = 0x0001
 * SAMPLE_RATE_CHANGE      = 0x0010
 * CHANNEL_MASK_CHANGE     = 0x0100
 * FORMAT_CHANGE           = 0x1000
 */

static void handle_audio_info_change_mask (struct aml_stream_out *aml_out, audio_metadata_t *metadata) {
    int digitCount = sizeof(aml_out->audio_info_change_mask) * 8;
    for (int i = digitCount - 1; i >= 0; i--) {
        int digit = (aml_out->audio_info_change_mask >> (i)) & 0x1;
        if (digit == 0)
            continue;
        int case_mask;
        case_mask = i/4;
        switch (case_mask) {
            /*handle OUTPUT_LATENCY_CHANGE*/
            case 0: {
                audio_metadata_put(metadata, KEY_DTV_LATENCY,(int32_t)(aml_out->report_latency));
                break;
            }
            /*handle SAMPLE_RATE_CHANGE*/
            case 1:{
                audio_metadata_put(metadata, KEY_SAMPLE_RATE,(int32_t)(aml_out->hal_rate));
                break;
            }
            /*CHANNEL_MASK_CHANGE*/
            case 2:{
                 audio_metadata_put(metadata, KEY_CHANNEL_MASK,(int32_t)(aml_out->hal_channel_mask << 2));
                 break;
            }
            /*handle FORMAT_CHANGE*/
            case 3:{
                audio_metadata_put(metadata, KEY_AUDIO_ENCODING,(int32_t)(audioFormat2EncodingFormat(aml_out->hal_format)));
                break;
            }

            default:
                break;
        }
    }

}
void out_stream_send_codec_event(struct audio_stream_out *stream, const char *caller)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    int is_format_change = 0;
    int is_channel_mask_change = 0;
    int is_sample_rate_change = 0;
    int is_dtv_latency_change = 0;
    if (aml_out->stream_event_callback && aml_out->stream_cookie) {
        if (adev->debug_flag > 1) {
            ALOGI("caller:%s, out:%p, stream_cookie:%p dtv_audio_latency: %d", caller, aml_out, aml_out->stream_cookie,aml_out->latency_frames);
        }
        audio_metadata_t *metadata = audio_metadata_create();
        handle_audio_info_change_mask(aml_out,metadata);
        uint8_t *bs = NULL;
        ssize_t length = byte_string_from_audio_metadata(metadata, &bs);
        aml_out->stream_event_callback(STREAM_EVENT_CBK_TYPE_CODEC_FORMAT_CHANGED, (void*)bs, aml_out->stream_cookie);
        free(bs);
        audio_metadata_destroy(metadata);
        aml_out->audio_info_change_mask = 0;
    }
}

void aml_stream_clear_speed_aux_info(struct aml_stream_out *aml_out)
{
    aml_stream_speed_info_t *speed_info = NULL;
    aml_audio_speed_apts_gap_ease_t *gap_ease = NULL;
    if (aml_out == NULL) {
        return;
    }
    speed_info = &aml_out->speed_info;
    gap_ease = &speed_info->apts_gap_ease;

    aml_audio_speed_clear_start_ts(&speed_info->start_ts);
    aml_audio_speed_reset_apts_gap(&speed_info->sync_apts_gap, AML_AUDIO_SPEED_DETECT_GAP_TIME_MS);
    gap_ease->speed = speed_info->speed;
    gap_ease->target_frames = 0;
    gap_ease->current_frames = 0;
    gap_ease->start = false;
}

ssize_t mixer_aux_buffer_write_wrap(struct audio_stream_out *stream, void *abuffer)
{
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    aml_audio_buffer_t *audioBuffer = (aml_audio_buffer_t *)abuffer;
    void *buffer = audioBuffer->pData;
    const size_t bytes = audioBuffer->size;
    int rbuffer_size = aml_out->input_start_threshold * aml_out->hal_frame_size;
    int in_frames = bytes / aml_out->hal_frame_size;

    if (aml_out->standby && (aml_out->input_cache_frames + in_frames < aml_out->input_start_threshold)) {
        if (aml_out->input_cache_rbuffer == NULL) {
            aml_out->input_cache_rbuffer = (struct ring_buffer *)aml_audio_calloc(1, sizeof(struct ring_buffer));
            if (aml_out->input_cache_rbuffer != NULL) {
                if (ring_buffer_init(aml_out->input_cache_rbuffer, rbuffer_size) != 0) {
                    aml_audio_free(aml_out->input_cache_rbuffer);
                    aml_out->input_cache_rbuffer == NULL;
                    ALOGE("%s : ring_buffer_init fail !", __func__);
                }
            }
        }
        if (aml_out->input_cache_rbuffer != NULL) {
            ring_buffer_write(aml_out->input_cache_rbuffer, buffer, bytes, UNCOVER_WRITE);
            aml_out->input_cache_frames += in_frames;
            ALOGI("%s in_frames %d, input_cache_frames %d", __func__, in_frames, aml_out->input_cache_frames);
            //aml_audio_sleep(in_frames/2 * 1000 / 48);
            return bytes;
        }
    }

    if (aml_out->input_cache_frames > 0) {
        int cache_bytes  = aml_out->input_cache_frames * aml_out->hal_frame_size;
        int temp_bufsize = cache_bytes + bytes;
        void *temp_buffer = aml_audio_calloc(1, temp_bufsize);
        aml_audio_buffer_t abuffer_temp;

        if (temp_buffer != NULL) {
            ring_buffer_read(aml_out->input_cache_rbuffer, temp_buffer, cache_bytes);
            memcpy((uint8_t *)temp_buffer + cache_bytes, buffer, bytes);

            memset(&abuffer_temp, 0, sizeof(abuffer_temp));
            abuffer_temp.pData = temp_buffer;
            abuffer_temp.size = temp_bufsize;
            mixer_aux_buffer_write(stream, &abuffer_temp);
            aml_audio_free(temp_buffer);
            temp_buffer = NULL;
        }
        aml_out->input_cache_frames = 0;
    } else {
        return mixer_aux_buffer_write(stream, abuffer);
    }
    return bytes;
}

bool aml_stream_wait_callback_finish(struct aml_audio_device *adev, struct aml_stream_out *out)
{
    int wait_cnt = 20;
    if (adev == NULL || out == NULL) {
        return false;
    }

    while (wait_cnt > 0) {
        pthread_mutex_lock(&adev->stream_release_lock);
        if (out->is_callback_pending == false) {
            pthread_mutex_unlock(&adev->stream_release_lock);
            break;
        }
        pthread_mutex_unlock(&adev->stream_release_lock);
        aml_audio_sleep(10*1000);
        ALOGE("%s wait callback ...", __func__);
        wait_cnt--;
    }

    if (wait_cnt <= 0) {
        ALOGE("%s wait callback finish fail !", __func__);
        return false;
    }
    return true;
}

void aml_stream_delete_timer(struct aml_audio_device *adev, struct aml_stream_out *out)
{
    int ret = 0;
    int wait_cnt = 20;
    if (adev == NULL || out == NULL) {
        return;
    }

    if (out->is_callback_pending) {
        aml_stream_wait_callback_finish(adev, out);
    }
    if (out->streamType == STREAM_PCM_HWSYNC) {
        if (out->timer_id <= AML_TIMER_ID_NUM && out->timer_id != AML_TIMER_ID_INVALID) {
            ret = aml_audio_timer_delete(out->timer_id);
            if (ret >= 0) {
                out->timer_id = AML_TIMER_ID_INVALID;
            }
            AM_LOGI(" timer_id %d %s", out->timer_id, ret >= 0 ? "ok" : "fail !");
        }

        if (out->timer_id2 <= AML_TIMER_ID_NUM && out->timer_id2 != AML_TIMER_ID_INVALID) {
            ret = aml_audio_timer_delete(out->timer_id2);
            if (ret >= 0) {
                out->timer_id2 = AML_TIMER_ID_INVALID;
            }
            AM_LOGI(" timer_id2 %d %s", out->timer_id2, ret >= 0 ? "ok" : "fail !");
        }
    }
}
