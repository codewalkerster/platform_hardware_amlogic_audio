/*
 * Copyright (C) 2010 Amlogic Corporation.
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
#ifndef _AUDIO_HW_FFV_H_
#define _AUDIO_HW_FFV_H_

#include <audio_hw.h>

#ifdef LOWPOWER_DSP_FFV
int sound_trigger_open(struct aml_stream_in *in, unsigned int port, unsigned int card);
ssize_t in_read_from_fetch_buf(struct audio_stream_in *stream, void* buffer, size_t bytes);
int sound_trigger_read(struct aml_stream_in *in, void* buffer, size_t bytes, struct timespec *ts);
int sound_trigger_close(struct aml_stream_in *in);
int sound_trigger_to_suspend(struct aml_stream_in *in);
void dsp_ffv_stream_init(struct aml_stream_in *in);
void dsp_ffv_stream_deinit(struct aml_stream_in *in);

void dsp_ffv_dev_init(struct aml_audio_device *adev);
void dsp_ffv_dev_deinit(struct aml_audio_device *adev);
void get_vwe_wakeup_event(struct aml_audio_device *adev);

#endif
#endif
