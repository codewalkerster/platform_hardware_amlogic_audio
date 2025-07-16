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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif
struct audio_stream_out;
struct aml_audio_device;
struct aml_audio_buffer;

#define MPEGH_AUDIO_LANG     0
#define MPEGH_LABEL_LANG     1
#define MPEGH_DRC_ATT        2
#define MPEGH_ACCESSIBILITY  3
#define MPEGH_DRC_EFFECT     4
#define MPEGH_DRC_ALBUM      5
#define MPEGH_DRC_BOOST      6
#define MPEGH_TL             7
#define PERSISTENCE_BUFSIZE 2048
#define BASE64_BUFSIZE ((PERSISTENCE_BUFSIZE + 2) / 3) * 4
#define AML_MPEGH_UIMANAGER_INPUT_FILE_DUMP_DIR     "/data/vendor/audiohal/ui_manager_input.raw"
#define AML_MPEGH_UIMANAGER_OUTPUT_FILE_DUMP_DIR    "/data/vendor/audiohal/ui_manager_output.raw"
#define AML_MPEGH_UI_MANAGER_SYSTEM_SETTING_NUM 8

typedef struct mpegh_ui_system_setting {
    int keyid;
    char actionevent[64];
}mpegh_ui_system_setting_t;

int base64_encode(const unsigned char* input, int input_length, struct aml_audio_device *adev);
int base64_decode(char* input, int input_length, struct aml_audio_device *adev);
void addNewActionEvent(char * event);
int assembleMpeghSystemActionEventXml(int key, char* value, char **xmlbuf);
int saveMpeghSysActioneventXml(char *key, char *value);
int aml_mpegh_uimanager_process(struct audio_stream_out *stream, aml_audio_buffer_t *outBuffer, int *asiupdate);
int aml_mpegh_applyxmlaction (struct aml_audio_device *adev, char *xmlstr, int xmlsize);
char *aml_mpegh_getxmlsceneinfo (struct aml_audio_device *adev);
int aml_mpegh_savepersistencememory (struct aml_audio_device *adev, void *persistenceMemory, int size);
int aml_uimanager_close(struct aml_audio_device *adev, void *handle);
int set_MPEGH_parameters(struct aml_audio_device *adev, struct str_parms *parms);
#ifdef __cplusplus
}
#endif

