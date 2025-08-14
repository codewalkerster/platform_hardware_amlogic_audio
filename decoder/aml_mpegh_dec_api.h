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

#ifndef _AML_MPEGH_DEC_API_H_
#define _AML_MPEGH_DEC_API_H_
#include "EarconPacketWriter_lib.h"
#include "iec61937_enc.h"
#define TT_MHAS             20 /**< MPEG-H 3D Audio Stream. */
#define TT_MHAS_PACKETIZED  24 /**< MPEG-H 3D Audio Packets. */
#define TT_MHA_RAW          60 /**< MPEGH 3D Audio */

#define MPEGH_LIB_PATH "/odm/lib/libcdkMpeghDecoder.so"

#define OUT_MAX_CH    ( 24 )                           /*!< Can be reduced depending on supported target layouts */
/* two extra channels in case of additional stereo downmix output */
/*!< Size of decoder output buffer (maxCh * maxFrameSize * "PCM data size"). */
#define MPEGH_MAX_LENGTH ( (OUT_MAX_CH + 2) * (1024*3) * 4 )

typedef struct mpegh_decoder_operations {
    const char *name;
    int nAudioDecoderType;
    int nInBufSize;
    int nOutBufSize;
    int (*init)(void *);
    int (*decode)(void *, char *outbuf, int *outlen, char *inbuf, int inlen);
    int (*release)(void *);
    int (*getinfo)(void *, AudioInfo *pAudioInfo);
    void * priv_data;//point to audec
    void * priv_dec_data;//decoder private data
    void *pdecoder; // decoder instance
    int channels;
    unsigned long pts;
    int samplerate;
    int bps;
    int extradata_size;      ///< extra data size
    char extradata[4096];
    int NchOriginal;
    int lfepresent;
}mpegh_decoder_operations_t;

typedef struct mpegh_dec {
    aml_dec_t  aml_dec;
    aml_mpegh_config_t mpegh_config;
    mpegh_decoder_operations_t mpegh_op;
    aml_dec_stream_info_t stream_info;
    unsigned long total_raw_size;
    unsigned long total_pcm_size;
    unsigned long total_time;
    void *mpeghLibHandler;
    HANDLE_IEC61937_ENCODER encoder_handle;
    int rate_factor;
    int output_bw;
} mpegh_dec_t;
aml_dec_func_t *get_mpegh_dec_func_handle(void);
#endif

