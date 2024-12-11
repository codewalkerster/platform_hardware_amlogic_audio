/*
 * Copyright (C) 2018 Amlogic Corporation.
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
 *
 * DESCRIPTION:
 *     This program is the factory of PCM data.
 *
 */

#ifndef AML_REVERB_H
#define AML_REVERB_H

int AML_Reverb_Init(void **reverb_handle);
int AML_Reverb_Process(void *reverb_handle, int16_t *inBuffer, int16_t *outBuffer, int frameCount);
int AML_Reverb_Release(void *reverb_handle);
void AML_Reverb_Set_Mode(void *reverb_handle, unsigned int mode);

#endif

