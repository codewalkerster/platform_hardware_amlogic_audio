/*
* Copyright 2023 Amlogic Inc. All rights reserved.
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

#ifndef IVA_AUDIO_SPEECH_ENHANCEMENT_H
#define IVA_AUDIO_SPEECH_ENHANCEMENT_H

#include "audio_enhancement/iva_audio_enhancement_common.h"

#ifdef __cplusplus
extern "C"
{
#endif
   /**
     * @brief NN audio speech enhancement module initialize function
     *
     * @param pt_enhancement_param structure of audio enhancement  parameters
     * @return iva_audio_enhancement_strut_t NN audio enhancement module global handle
     */

    __attribute ((visibility("default"))) extern iva_audio_enhancement_strut_t *aai_iva_audio_enhancement_init(aai_iva_audio_enhancement_param_t *pt_enhancement_param);

   /**
     * @brief NN audio enhancement module release function
     *
     * @param nnans_imple global audio enhancement handle .
     * @return void
     */
    __attribute ((visibility("default"))) extern void aai_iva_audio_enhancement_deinit(iva_audio_enhancement_strut_t *nnans_imple);

   /**
     * @brief process functions for audio enhancement module
     *
     * @param pt_enhancement_input structure of enhancement  input
     * @param pt_enhancement_output structure of enhancement  output
     * @param nnans_imple NN audio enhancement module global handle
     * @param db          enhancement db (from 0 to 15)
     * @return void
     * @details
     * run audio speech enhancement function frame by frame
     */
    __attribute ((visibility("default"))) extern void aai_iva_audio_enhancement_process(aai_iva_audio_enhancement_input_t *pt_enhancement_input, aai_iva_audio_enhancement_output_t *pt_enhancement_output, iva_audio_enhancement_strut_t *enhancement_imple,int db);


   /**
     * @brief process functions for audio enhancement module
     *
     * @param pt_enhancement_input structure of enhancement  input
     * @param pt_enhancement_output structure of enhancement  output
     * @param nnans_imple NN audio enhancement module global handle
     * @param db          enhancement db (from 0 to 15)
     * @return void
     * @details
     * run audio speech enhancement function frame by frame
     */
    __attribute ((visibility("default"))) extern void aai_iva_audio_enhancement_process_int32(aai_iva_audio_enhancement_input_t *pt_enhancement_input, aai_iva_audio_enhancement_output_t *pt_enhancement_output, iva_audio_enhancement_strut_t *enhancement_imple,int db);

#ifdef __cplusplus
}
#endif

#endif /* IVA_AUDIO_SPEECH_ENHANCEMENT_H */
