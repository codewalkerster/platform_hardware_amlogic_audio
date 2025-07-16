/*
 * Copyright (C) 2025 Amlogic Corporation.
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
#define LOG_TAG "audio_mpegh"
//#define LOG_NDEBUG 0

#include <stdint.h>
#include <stddef.h>
#include <cutils/properties.h>
extern "C" {
#include "audio_hw.h"
#include "aml_dump_debug.h"
#include "aml_audio_stream.h"
#include "aml_mpegh_uimanager.h"
#include "aml_stream_manager.h"
}
#include "audio_mpegh.h"
#include <unordered_map>
#include <vector>
#include <queue>
#include <string>
#include <iterator>
using namespace std;
#define AML_MPEGH_UIMANAGER_PROP_DUMP_FLAG                    "vendor.media.audio.mpegh.uimanager.dump"
#define AML_MPEGH_UIMANAGER_PROP_DEBUG_FLAG                   "vendor.media.audio.mpegh.uimanager.debug"

unordered_map<std::string, std::string> uiManagerSystemSetting;
queue<std::string> ActionEventQueue;
const char *mpeghSystemActionEventXml[] = {
    "<ActionEvent uuid=\"00000000-0000-0000-0000-000000000000\" version=\"11.0\" actionType=\"70\" paramInt=\"0\" paramText=\"%s\"/>",
    "<ActionEvent uuid=\"00000000-0000-0000-0000-000000000000\" version=\"11.0\" actionType=\"71\" paramInt=\"0\" paramText=\"%s\"/>",
    "<ActionEvent uuid=\"00000000-0000-0000-0000-000000000000\" version=\"11.0\" actionType=\"12\" paramFloat=\"%s\"/>",
    "<ActionEvent uuid=\"00000000-0000-0000-0000-000000000000\" version=\"11.0\" actionType=\"31\" paramInt=\"%s\"/>",
    "<ActionEvent uuid=\"00000000-0000-0000-0000-000000000000\" version=\"11.0\" actionType=\"10\" paramInt=\"%s\"/>",
    "<ActionEvent uuid=\"00000000-0000-0000-0000-000000000000\" version=\"11.0\" actionType=\"21\" paramBool=\"%s\"/>",
    "<ActionEvent uuid=\"00000000-0000-0000-0000-000000000000\" version=\"11.0\" actionType=\"11\" paramFloat=\"%s\"/>",
    "<ActionEvent uuid=\"00000000-0000-0000-0000-000000000000\" version=\"11.0\" actionType=\"20\" paramFloat=\"%s\"/>"
};

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
int base64_encode(const unsigned char* input, int input_length, struct aml_audio_device *adev) {
    if (!input || !input_length || !adev) {
        ALOGE("%s parameters invalid", __func__);
        return -1;
    }
    int enc_len = 4 * ((input_length + 2) / 3);
    adev->mpegh_base64_encode_mem = aml_audio_realloc(adev->mpegh_base64_encode_mem, enc_len + 1);
    char * enc_buf = (char*)adev->mpegh_base64_encode_mem;
    int i, j = 0;

    for (i; i < input_length; i += 3) {
        int val = (input[i] << 16);
        if (i + 1 < input_length) {
            val |= (input[i + 1] << 8);
        }
        if (i + 2 < input_length) {
            val |= input[i + 2];
        }

        enc_buf[j++] = base64_table[(val >> 18) & 0x3F];
        enc_buf[j++] = base64_table[(val >> 12) & 0x3F];
        enc_buf[j++] = (i + 1 < input_length) ? base64_table[(val >> 6) & 0x3F] : '=';
        enc_buf[j++] = (i + 2 < input_length) ? base64_table[val & 0x3F] : '=';
    }
    enc_buf[j] = '\0';

    return 0;
}

int base64_decode(char* input, int input_length, struct aml_audio_device *adev) {
    if (!input || !input_length || !adev) {
        ALOGE("%s input param invalid", __func__);
        return -1;
    }
    if (input_length % 4 != 0) {
        ALOGE("%s input length not a multiple of 4", __func__);
        return -1;
    }

    int output_len = input_length / 4 * 3;
    if (input_length > 0 && input[input_length - 1] == '=') output_len--;
    if (input_length > 1 && input[input_length - 2] == '=') output_len--;

    //The size of the persistencemem must be a multiple of 1024.
    if (output_len % 1024) {
        output_len -= (output_len % 1024);
    }
    ALOGI("%s input_length = %d, output_len = %d", __func__, input_length, output_len);

    unsigned char* decoded_data = (unsigned char*)adev->mpegh_ui_persistencemem;

    unsigned char decoding_table[256];
    memset(decoding_table, 0xFF, 256);
    for (int i = 0; i < 64; i++) {
        decoding_table[(unsigned char)base64_table[i]] = i;
    }

    for (int i = 0, j = 0; i < input_length;) {
        unsigned char a = input[i] == '=' ? 0 & i++ : decoding_table[(unsigned char)input[i++]];
        unsigned char b = input[i] == '=' ? 0 & i++ : decoding_table[(unsigned char)input[i++]];
        unsigned char c = input[i] == '=' ? 0 & i++ : decoding_table[(unsigned char)input[i++]];
        unsigned char d = input[i] == '=' ? 0 & i++ : decoding_table[(unsigned char)input[i++]];

        if (a == 0xFF || b == 0xFF || c == 0xFF || d == 0xFF) {
            return -1;
        }

        uint32_t triple = (a << 3 * 6) | (b << 2 * 6) | (c << 1 * 6) | (d << 0 * 6);

        if (j < output_len) decoded_data[j++] = (unsigned char)((triple >> 2 * 8) & 0xFF);
        if (j < output_len) decoded_data[j++] = (unsigned char)((triple >> 1 * 8) & 0xFF);
        if (j < output_len) decoded_data[j++] = (unsigned char)((triple >> 0 * 8) & 0xFF);
    }
    return 0;
}

int assembleMpeghSystemActionEventXml(int key, char* value, char **xmlbuf)
{
    if (key < 0 || key > 7 || !value || !xmlbuf) {
        printf("%s Invalid param key:%d, value:%p, xmlbuf:%p\n",
               __func__, key, value, (void *)xmlbuf);
        return -1;
    }

    size_t len = snprintf(NULL, 0, mpeghSystemActionEventXml[key], value) + 1;
    *xmlbuf = (char *)aml_audio_malloc(len);
    if (!*xmlbuf) return -1;

    snprintf(*xmlbuf, len, mpeghSystemActionEventXml[key], value);
    return 0;
}

void addNewActionEvent(char * event) {
    if (!event)return ;
    ActionEventQueue.push(event);
    ALOGI("%s ActionEventQueue size =%zu",__func__, ActionEventQueue.size());
}

int saveMpeghSysActioneventXml(string key, char *value)
{
    if (key.empty()) return -1;
    if (uiManagerSystemSetting.find(key) != uiManagerSystemSetting.end()) {
        uiManagerSystemSetting[key] = value;
    } else {
        uiManagerSystemSetting.emplace(key,value);
    }
    return 0;
}

int aml_mpegh_uimanager_process(struct audio_stream_out *stream, aml_audio_buffer_t *outBuffer, int *asiupdate)
{
    AM_LOGV("enter");
    struct aml_stream_out *aml_out = (struct aml_stream_out *) stream;
    struct aml_audio_device *adev = aml_out->dev;
    struct aml_audio_buffer *abuffer_out = outBuffer;
    void *mpegh_uiop = aml_out->mpegh_uimanager_handle;
    int ret = -1;
    int isupdate = 0;
    void *outbuf = NULL;
    int outlen   = 0;
    if (!outBuffer || !abuffer_out->pData || !abuffer_out->size || !aml_out) {
        AM_LOGE("outBuffer:%p, mpegh_uimanager_handle:%p, input error!", outBuffer, aml_out);
        return ret;
    }

    if (!mpegh_uiop) {
        ret = aml_mpegh_uimanager_open(&mpegh_uiop);
        if (ret != 0) {
            AM_LOGE(" aml_mpegh_uimanager_open fail, ret:%d, mpegh_uiop:%p", ret, mpegh_uiop);
            return ret;
        }
        aml_out->mpegh_uimanager_handle = mpegh_uiop;
        //set persistency ctx
        if (adev->mpegh_ui_persistencemem && adev->mpegh_ui_persistencememsize > 0) {
            ret = aml_mpegh_uimanager_setpersistencememory(mpegh_uiop, (char*)adev->mpegh_ui_persistencemem, adev->mpegh_ui_persistencememsize);
            if (ret != 0) {
                AM_LOGE(" aml_mpegh_uimanager_setpersistencememory fail, ret:%d", ret);
            }
        }
        //set system setting
        for (auto it = uiManagerSystemSetting.begin(); it != uiManagerSystemSetting.end(); it++) {
            char *sysSetXml = it->second.data();
            aml_mpegh_applyxmlaction(adev, sysSetXml, strlen(sysSetXml));
        }
    }

    while (!ActionEventQueue.empty()) {
        char * xml = ActionEventQueue.front().data();
        aml_mpegh_applyxmlaction(adev, xml, strlen(xml));
        ActionEventQueue.pop();
    }

    ret = aml_mpegh_uimanager_feedmhas(mpegh_uiop, (char*) abuffer_out->pData, abuffer_out->size);
    if (ret != 0) {
        AM_LOGE(" aml_mpegh_uimanager_feedmhas fail, ret:%d", ret);
        return ret;
    }

    ret = aml_mpegh_uimanager_updatemhas(mpegh_uiop, (char *)abuffer_out->pData, abuffer_out->size, &outbuf, &outlen);
    if (ret != 0) {
        AM_LOGE(" aml_mpegh_uimanager_updatemhas fail, ret:%d", ret);
        return ret;
    }

    abuffer_out->pData = outbuf;
    abuffer_out->size   = outlen;

    ret = aml_mpegh_uimanager_getxmlscenestate(mpegh_uiop, &isupdate);
    if ((ret == 0) && (isupdate == 1)) {
        mpegh_uimanager_oper_t *uioper = (mpegh_uimanager_oper_t *) mpegh_uiop;
        *asiupdate = 1;
        if (property_get_bool(AML_MPEGH_UIMANAGER_PROP_DUMP_FLAG, 0)) {
            AM_LOGI("getxml::::\r\n%s \r\n ::::end", uioper->xmloutbuf);
            static int count = 0;
            aml_dump_audio_bitstreams_with_id("xmlscenestate.txt", uioper->xmloutbuf, strlen(uioper->xmloutbuf), count++);
        }
    }
    return ret;
}

int aml_mpegh_applyxmlaction (struct aml_audio_device *adev, char *xmlstr, int xmlsize)
{
    struct aml_stream_out *aml_out = NULL;
    int ret = -1;
    if (property_get_bool(AML_MPEGH_UIMANAGER_PROP_DEBUG_FLAG, 0)) {
        AM_LOGI("xmlinstr::\r\n%s\r\n", xmlstr);
    }

    for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
        aml_out = adev->active_outputs[i];
        if (aml_out && is_mpegh_format(aml_out->hal_internal_format)) {
            AM_LOGI("got mpegh stream: aml_out:%p format:0x%x", aml_out, aml_out->hal_internal_format);
            mpegh_uimanager_oper_t *uioper = (mpegh_uimanager_oper_t *) aml_out->mpegh_uimanager_handle;
            int flagsout = 0;
            ret = aml_mpegh_uimanager_applyxmlaction(uioper, xmlstr, xmlsize, &flagsout);
            break;
        }
    }
    return ret;
}

char *aml_mpegh_getxmlsceneinfo (struct aml_audio_device *adev)
{
    struct aml_stream_out *aml_out = NULL;
    mpegh_uimanager_oper_t *uioper = NULL;
    AM_LOGI("enter");
    int ret = -1;

    for (int i = 0 ; i < STREAM_TYPE_MAX; i++) {
        aml_out = adev->active_outputs[i];
        if (aml_out && is_mpegh_format(aml_out->hal_internal_format)) {
            uioper = (mpegh_uimanager_oper_t *) aml_out->mpegh_uimanager_handle;
            AM_LOGI("got mpegh stream:aml_out:%p format:0x%x, uioper:%p", aml_out, aml_out->hal_internal_format, uioper);
            break;
        }
    }

    if (!uioper || !uioper->xmloutbuf) {
        return NULL;
    }
    if (property_get_bool(AML_MPEGH_UIMANAGER_PROP_DEBUG_FLAG, 0)) {
        AM_LOGI("xmloutstr::\r\n%s\r\n", uioper->xmloutbuf);
    }

    return uioper->xmloutbuf;
}

int aml_mpegh_savepersistencememory(struct aml_audio_device *adev, void *persistencemem, int persistencememsize)
{
    if (!persistencemem || persistencememsize <= 0)
        return -1;
    memset(adev->mpegh_ui_persistencemem, 0, adev->mpegh_ui_persistencememsize);
    int ret = base64_decode((char *)persistencemem, persistencememsize, adev);
    if (ret < 0) {
        AM_LOGE("base64 decode error");
        return -1;
    }
    return 0;
}

int aml_uimanager_close(struct aml_audio_device *adev, void *handle)
{
    AM_LOGI("enter!");
    if (!handle || !adev) {
        AM_LOGE("NULL handle or adev");
        return -1;
    }
    mpegh_uimanager_oper_t * uioper = (mpegh_uimanager_oper_t *) handle;
    aml_mpegh_uimanager_getpersistencememory(uioper, &adev->mpegh_ui_persistencemem, &adev->mpegh_ui_persistencememsize);

    if (property_get_bool(AML_MPEGH_UIMANAGER_PROP_DUMP_FLAG, 0) && adev->mpegh_ui_persistencememsize) {
        aml_dump_audio_bitstreams("/data/vendor/audiohal/mpegh_ui_persistency.txt", adev->mpegh_ui_persistencemem, adev->mpegh_ui_persistencememsize);
    }
    aml_mpegh_uimanager_close(uioper);
    return 0;
}

int set_MPEGH_parameters(struct aml_audio_device *adev, struct str_parms *parms)
{
    int ret = -1;
    char value[128] = {'\0'};
    //view setting
    ret = str_parms_get_str(parms, "mpegh_action_event", value, sizeof(value));
    if (ret >= 0) {
        //save event to queue
        addNewActionEvent(value);
        return ret;
    }
    //set persistency ctx
    if (str_parms_has_key(parms, "mpegh_persistency_ctx")) {
        char *mpegh_persictency_buf = (char *)aml_audio_calloc(1, (BASE64_BUFSIZE + 1));
        ret = str_parms_get_str(parms, "mpegh_persistency_ctx", mpegh_persictency_buf, (BASE64_BUFSIZE + 1));
        if (ret >= 0) {
            ret = aml_mpegh_savepersistencememory(adev, (void*)mpegh_persictency_buf, BASE64_BUFSIZE);
            AM_LOGD("set mpegh persistency ctx:, ret:%d", ret);
        }
        aml_audio_free(mpegh_persictency_buf);
        mpegh_persictency_buf = NULL;
        return ret;
    }
    //system setting
    ret = str_parms_get_str(parms, "mpegh_audio_lang", value, sizeof(value));
    if (ret >= 0) {
        //Splicing system actionevent XML
        char * mpegh_action_event = NULL;
        ret = assembleMpeghSystemActionEventXml(MPEGH_AUDIO_LANG, value, &mpegh_action_event);
        //store system setting to queue when exist active mpegh stream.
        if (aml_get_is_exist_active_mpegh_stream()) {
            addNewActionEvent(mpegh_action_event);
        }
        //store or update system setting
        if (mpegh_action_event) {
            saveMpeghSysActioneventXml("mpegh_audio_lang", mpegh_action_event);
        }
        aml_audio_free(mpegh_action_event);
        mpegh_action_event = NULL;
        return ret;
    }
    ret = str_parms_get_str(parms, "mpegh_label_lang", value, sizeof(value));
    if (ret >= 0) {
        char * mpegh_action_event = NULL;
        ret = assembleMpeghSystemActionEventXml(MPEGH_LABEL_LANG, value, &mpegh_action_event);
        if (aml_get_is_exist_active_mpegh_stream()) {
            addNewActionEvent(mpegh_action_event);
        }
        if (mpegh_action_event) {
            saveMpeghSysActioneventXml("mpegh_label_lang", mpegh_action_event);
        }
        aml_audio_free(mpegh_action_event);
        mpegh_action_event = NULL;
        return ret;
    }

    ret = str_parms_get_str(parms, "mpegh_drc_att", value, sizeof(value));
    if (ret >= 0) {
        char * mpegh_action_event = NULL;
        ret = assembleMpeghSystemActionEventXml(MPEGH_DRC_ATT, value, &mpegh_action_event);
        if (aml_get_is_exist_active_mpegh_stream()) {
            addNewActionEvent(mpegh_action_event);
        }
        if (mpegh_action_event) {
            saveMpeghSysActioneventXml("mpegh_drc_att", mpegh_action_event);
        }
        aml_audio_free(mpegh_action_event);
        mpegh_action_event = NULL;
        return ret;
    }

    ret = str_parms_get_str(parms, "mpegh_accessibility", value, sizeof(value));
    if (ret >= 0) {
        char * mpegh_action_event = NULL;
        ret = assembleMpeghSystemActionEventXml(MPEGH_ACCESSIBILITY, value, &mpegh_action_event);
        if (aml_get_is_exist_active_mpegh_stream()) {
            addNewActionEvent(mpegh_action_event);
        }
        if (mpegh_action_event) {
            saveMpeghSysActioneventXml("mpegh_accessibility", mpegh_action_event);
        }
        aml_audio_free(mpegh_action_event);
        mpegh_action_event = NULL;
        return ret;
    }

    ret = str_parms_get_str(parms, "mpegh_drc_effect", value, sizeof(value));
    if (ret >= 0) {
        char * mpegh_action_event = NULL;
        ret = assembleMpeghSystemActionEventXml(MPEGH_DRC_EFFECT, value, &mpegh_action_event);
        if (aml_get_is_exist_active_mpegh_stream()) {
            addNewActionEvent(mpegh_action_event);
        }
        if (mpegh_action_event) {
            saveMpeghSysActioneventXml("mpegh_drc_effect", mpegh_action_event);
        }
        aml_audio_free(mpegh_action_event);
        mpegh_action_event = NULL;
        return ret;
    }

    ret = str_parms_get_str(parms, "mpegh_drc_album", value, sizeof(value));
    if (ret >= 0) {
        char * mpegh_action_event = NULL;
        ret = assembleMpeghSystemActionEventXml(MPEGH_DRC_ALBUM, value, &mpegh_action_event);
        if (aml_get_is_exist_active_mpegh_stream()) {
            addNewActionEvent(mpegh_action_event);
        }
        if (mpegh_action_event) {
            saveMpeghSysActioneventXml("mpegh_drc_album", mpegh_action_event);
        }
        aml_audio_free(mpegh_action_event);
        mpegh_action_event = NULL;
        return ret;
    }

    ret = str_parms_get_str(parms, "mpegh_drc_boost", value, sizeof(value));
    if (ret >= 0) {
        char * mpegh_action_event = NULL;
        ret = assembleMpeghSystemActionEventXml(MPEGH_DRC_BOOST, value, &mpegh_action_event);
        if (aml_get_is_exist_active_mpegh_stream()) {
            addNewActionEvent(mpegh_action_event);
        }
        if (mpegh_action_event) {
            saveMpeghSysActioneventXml("mpegh_drc_boost", mpegh_action_event);
        }
        aml_audio_free(mpegh_action_event);
        mpegh_action_event = NULL;
        return ret;
    }

    ret = str_parms_get_str(parms, "mpegh_tl", value, sizeof(value));
    if (ret >= 0) {
        char * mpegh_action_event = NULL;
        ret = assembleMpeghSystemActionEventXml(MPEGH_TL, value, &mpegh_action_event);
        if (aml_get_is_exist_active_mpegh_stream()) {
            addNewActionEvent(mpegh_action_event);
        }
        if (mpegh_action_event) {
            saveMpeghSysActioneventXml("mpegh_tl", mpegh_action_event);
        }
        aml_audio_free(mpegh_action_event);
        mpegh_action_event = NULL;
        return ret;
    }
    return ret;
}