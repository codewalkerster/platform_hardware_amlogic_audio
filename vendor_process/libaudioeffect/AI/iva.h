/*
* Copyright (C) 2023 Amlogic Corporation.
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

#ifndef IVA_HPP
#define IVA_HPP

typedef enum
{
    IVA_STATUS_MIX_LINE = 1,
    IVA_STATUS_OK = 0,
    IVA_STATUS_ERROR = -1,
    IVA_STATUS_OUT_OF_MEMORY = -2,
} IVA_STATUS_E;

//IVA audio prebuilt library
#define AUDIO_AI_LIB_PATH     "/vendor/lib/libaaisdk.so"
#define AUDIO_AI_LIB64_PATH   "/vendor/lib64/libaaisdk.so"

#define AUDIO_ENHANCMENT_MODEL_PATH "/vendor/etc/"
#define AUDIO_VOCAL_ISOLATE_MODEL_PATH "/vendor/etc/"

#endif