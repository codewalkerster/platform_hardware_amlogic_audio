/*
 * Copyright (C) 2024 Amlogic Corporation.
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

#ifndef AUDIO_MS12_CONTINUOUS_STANDBY_H
#define AUDIO_MS12_CONTINUOUS_STANDBY_H

#define STANDBY_REPEAT_FORMAT_PCM       0
#define STANDBY_REPEAT_FORMAT_MCH       1
#define STANDBY_REPEAT_FORMAT_DAP       2
#define STANDBY_REPEAT_FORMAT_DD        3
#define STANDBY_REPEAT_FORMAT_DDP       4
#define STANDBY_REPEAT_FORMAT_MAT_UPPER 5
#define STANDBY_REPEAT_FORMAT_MAT_LOWER 6
#define STANDBY_MAX_REPEAT_FORMAT       7

#define STANDBY_SET_STATUS            0
#define STANDBY_SET_OUTPUT_PORT       1
#define STANDBY_SET_ATMOS_LOCK        2

#include <stdint.h>
#include "audio_hw_ms12_v2.h"

typedef struct audio_continuous_standby audio_continuous_standby_t;


int audio_continuous_standby_open(void **pphandle, void *callback, void *priv_data);

int audio_continuous_standby_close(void **pphandle);

int audio_continuous_standby_run(void *phandle, int delay);

int audio_continuous_standby_attachframe(void *phandle, void *buf, int size, int format, aml_ms12_dec_info_t *info);

int audio_continuous_standby_check(void *phandle);

int audio_continuous_standby_reset(void *phandle);

int audio_continuous_standby_set(void *phandle, int type, int params);

#endif

