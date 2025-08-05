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
#define LOG_TAG "audio_mpegh_parser"
//#define LOG_NDEBUG 0

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <cutils/log.h>
#include <inttypes.h>
#include <aml_dump_debug.h>
#include "aml_audio_bitsparser.h"
#include "aml_audio_mpeghparser.h"
#include "aml_malloc_debug.h"
#include "audio_data_process.h"

typedef struct {
    ParseState current_state;  //Current parsing state‌
    unsigned char *working_buf;         //parse cached buf
    int32_t working_end;       //data lens in working buf
    int32_t working_start;     //next output start position
    int32_t working_pos;       //current process position
    char need_input;           //need input data flag
    int32_t header_size;       //Received header bytes
    MhasHeader active_header;  //Parsed header information
    int debug_enable;
    struct audio_bit_parser bit_parser;
    mpegh_parser_info parser_info;
    bool is_sub_parser;
    bool is_tv_src_flag;       //tv src or dtv src
} aml_mpegh_parser;

static uint64_t read_escaped_int(struct audio_bit_parser *bit_parser, int nBits1, int nBits2, int nBits3, int *total)
{
    uint64_t value, valueAdd;
    int total_tmp = 0;
    value = aml_audio_bitparser_getBits(bit_parser, nBits1);
    total_tmp = nBits1;
    AM_LOGV("escValue %d: %" PRIu64"\n", nBits1, value);
    if (value == (uint32_t)(1 << nBits1) - 1) {
        valueAdd = aml_audio_bitparser_getBits(bit_parser, nBits2);
        total_tmp += nBits2;
        AM_LOGV("escValue %d: %" PRIu64"\n", nBits2, valueAdd);
        value += valueAdd;
        if (valueAdd == (uint32_t)(1 << nBits2) - 1) {
            valueAdd = aml_audio_bitparser_getBits(bit_parser, nBits3);
            total_tmp += nBits3;
            AM_LOGV("escValue %d: %" PRIu64"\n", nBits3, valueAdd);
            value += valueAdd;
        }
    }
    *total = total_tmp;
    return value;
}

static int aml_mpegh_getSampleLength(uint8_t usacSamplingFrequencyIndex)
{
    switch (usacSamplingFrequencyIndex) {
        case 0x03:  /* 48000 Hz --> 1024 samples. */
            return 1024;
            break;
        case 0x05:  /* 32000 Hz --> 1536 samples. */
            return 1536;
            break;
        case 0x06:  /* 24000 Hz --> 2048 samples. */
            return 2048;
            break;
        case 0x08:  /* 16000 Hz --> 3072 samples. */
            return 3072;
            break;
        default:
            break;
    }
    return 0;
}

static int getSamplingFrequency(int index)
{
    static const int samplingFrequencies[] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
        16000, 12000, 11025, 8000, 7350, 0, 0, 0
    };
    return (index < 15) ? samplingFrequencies[index] : -1;
}

static bool detect_syncword (uint8_t *data, int data_len, int *sync_pos)
{
    const uint8_t sync_bytes[] = {0xC0, 0x01, 0xA5};
    if (data_len < 3) {
        return false;
    }

    // Sliding window search for syncword‌
    for (int i = 0; i <= data_len - 3; i++) {
        if (memcmp(data + i, sync_bytes, sizeof(sync_bytes)) == 0) {
            *sync_pos = i;  // Record syncword start position
            return true;
        }
    }
    return false;
}

static bool mpegh_parse_header(struct audio_bit_parser *bit_parser, unsigned char *header_buf, int size, MhasHeader *header)
{
    int num = 0, total = 0;
    aml_audio_bitparser_init(bit_parser, header_buf, size);

    // 1. Read frame type (3+8+8 bits 3-level escape)
    header->frame_type = read_escaped_int(bit_parser, 3, 8, 8, &num);
    if (-1 == header->frame_type) {
        AM_LOGE("frame_type:%d, error!", header->frame_type);
        goto fail;
    }
    total += num;

    // 2. Read packet label (2+8+32 bits 3-level escape)
    header->label = read_escaped_int(bit_parser, 2, 8, 32, &num);
    if ((-1 == header->label) || (0x10 < header->label)) {
        AM_LOGE("label:%" PRIu64", error!", header->label);
        goto fail;
    }
    total += num;

    // 3. Validate label-frame_type combinations
    if (header->label == 0) {
        switch (header->frame_type) {
            case PACTYP_MPEGH3DACFG:
            case PACTYP_AUDIOTRUNCATION:
            case PACTYP_MPEGH3DAFRAME:
                AM_LOGE("frame_type:%d label:%" PRIu64", error!", header->frame_type, header->label);
                goto fail;
            default:
                AM_LOGV("frame_type:%d label:%" PRIu64, header->frame_type, header->label);
                break;
        }
    }

    // 4. Read payload length (11+24+24 bits 3-level escape)
    header->payload_length = read_escaped_int(bit_parser, 11, 24, 24, &num);
    if (-1 == header->payload_length) {
        AM_LOGE("payload_length:%" PRIu64", error!", header->payload_length);
        goto fail;
    }
    total += num;
    if (0 != total % 8) {
        AM_LOGW("FAIL read header (not byte-aligned)!");
        goto fail;
    }

    header->header_len = total / 8;
    AM_LOGI("frame_type:%d label:%" PRIu64", header_len:%d, payload_len:%" PRIu64", total:%d, [0x%x%x%x%x]", header->frame_type, header->label,
                    header->header_len, header->payload_length, total, header_buf[0], header_buf[1],header_buf[2], header_buf[3]);

    if ((1 <= header->label) && (16 >= header->label)) {
        switch (header->frame_type) {
            case PACTYP_MPEGH3DACFG:
            {
                aml_audio_bitparser_getBits(bit_parser, 8);
                uint8_t usacSamplingFrequencyIndex = aml_audio_bitparser_getBits(bit_parser, 5);
                if (0x1f == usacSamplingFrequencyIndex) {
                    AM_LOGI("0x1f, rate:%d", aml_audio_bitparser_getBits(bit_parser, 24));
                }

                int32_t coreSbrFrameLengthIndex = aml_audio_bitparser_getBits(bit_parser, 3);
                int32_t sample_lens = aml_mpegh_getSampleLength(usacSamplingFrequencyIndex);
                int32_t sample_rate = getSamplingFrequency(usacSamplingFrequencyIndex);
                if ((0 < sample_lens) && (0 < sample_rate)) {
                    header->sample_lens = sample_lens;
                    header->sample_rate = sample_rate;
                } else {
                    AM_LOGW("samplerate_idx:%d, errsamples:%d, errrate:%d", usacSamplingFrequencyIndex, sample_lens, sample_rate);
                    goto fail;
                }
                AM_LOGI("samplerate_idx:%d, frame_samples:%d, sample_rate:%d, coreSbrFrameLengthIndex:%d",
                        usacSamplingFrequencyIndex, header->sample_lens, header->sample_rate, coreSbrFrameLengthIndex);
                break;
            }
            case PACTYP_AUDIOTRUNCATION:
            {
                uint8_t trunc_isActive = aml_audio_bitparser_getBits(bit_parser, 1);
                aml_audio_bitparser_getBits(bit_parser, 1);
                uint8_t truncFromBegin = aml_audio_bitparser_getBits(bit_parser, 1);
                uint16_t nTruncSamples = aml_audio_bitparser_getBits(bit_parser, 13);
                if (trunc_isActive) {
                    header->truncSamples = nTruncSamples;
                    if (truncFromBegin) {
                        AM_LOGI("Trunc %d samples from begin\n", nTruncSamples);
                    } else {
                        AM_LOGI("Trunc %d samples at end\n", nTruncSamples);
                    }
                } else {
                    AM_LOGI("Trunc not active\n");
                }
                AM_LOGI("PACTYP_AUDIOTRUNCATION\n");
                break;
            }
            case PACTYP_MPEGH3DAFRAME:
                header->dur_samples = header->sample_lens - header->truncSamples;
                break;
            default:
                AM_LOGI("header->frame_type: %d", header->frame_type);
                break;
        }
    }
    return true;

fail:
    return false;
}

void mpegh_reset_parser (aml_mpegh_parser *parser)
{
    unsigned char *working_tmp = parser->working_buf;
    ParseState current_state = parser->current_state;
    memset(parser->working_buf, 0, MAX_MPEGH_FRAME_LENGTH);
    int32_t sample_lens = parser->active_header.sample_lens;
    int32_t sample_rate = parser->active_header.sample_rate;
    int debug_enable = parser->debug_enable;
    memset(parser, 0, sizeof(aml_mpegh_parser));
    parser->active_header.sample_lens = sample_lens;
    parser->active_header.sample_rate = sample_rate;
    parser->debug_enable = debug_enable;
    parser->working_buf = working_tmp;
    parser->current_state = STATE_FINDING_SYNC;//STATE_READING_HEADER;
    return;
}

/**
 * MHAS protocol stream parser
 * @param parser parser context
 * @param input Raw byte stream to process
 * @param input_len Length of input buffer
 * @param output Pointer to completed frame buffer (output parameter)
 * @param used_size Length of input used size (output parameter)
 * @param output_len Length of completed frame (output parameter)
 * @param mpegh_info mpegh stream info (output parameter)
 * @return 0 on success, error code otherwise
 */
int aml_mpegh_parser_internal (aml_mpegh_parser *parser, const void *input, int32_t input_len,
                   void **output, int32_t *used_size, int32_t *output_len, mpegh_parser_info *mpegh_info)
{
    int32_t processed  = 0;     //input processed size
    int working_remain = 0;     //working buf remain size
    int ret     = 0;
    *output     = NULL;
    *output_len = 0;
    int debug   = parser->debug_enable;
    AM_LOGV("enter");

    /*
    1.for local play,new data input need cache into working buffer first.
    2.for dtv/hdmi in,always save the data first.
    */
    if (parser->need_input || parser->is_tv_src_flag) {
        if (0 != parser->working_start) {
            if (debug) {
                AM_LOGI("start:%d, pos:%d, size:%d", parser->working_start, parser->working_pos, parser->working_end);
            }
            working_remain = parser->working_end - parser->working_start;
            memmove(parser->working_buf, parser->working_buf + parser->working_start, working_remain);
            parser->working_pos  -= parser->working_start;
            parser->working_start = 0;
            parser->working_end   = working_remain;
        }

        if (MAX_MPEGH_FRAME_LENGTH < (input_len + parser->working_end)) {
            if (debug) {
                AM_LOGI("working_buf overflow, input_len:%d + working_end:%d > %d, drop lens:%d!", input_len, parser->working_end, MAX_MPEGH_FRAME_LENGTH, input_len);
            }
            processed = input_len;
            mpegh_reset_parser(parser);
            ret = -1;
            goto finish;
        }
        memcpy((parser->working_buf + parser->working_end), input, input_len);
        parser->working_end += input_len;
        parser->need_input = 0;
        if (debug) {
            AM_LOGI("start:%d, pos:%d, size:%d", parser->working_start, parser->working_pos, parser->working_end);
        }
    }

    // State machine processing loop‌
    while (processed < input_len) {
        if (debug) {
            AM_LOGI("enter parser->current_state:%d", parser->current_state);
        }
        switch (parser->current_state) {
            case STATE_FINDING_SYNC: {
                int sync_pos = -1;
                if (detect_syncword((uint8_t *)input + processed, input_len - processed, &sync_pos)) {
                    processed += sync_pos;  // Skip invalid preamble data
                    if (sync_pos >= 0) {
                        memcpy(parser->working_buf, (uint8_t *)input + processed, input_len - processed);
                        parser->working_end = input_len - processed;
                        parser->working_pos = 0;
                        if (debug) {
                            AM_LOGI("start skip lens:%d", sync_pos);
                        }
                    }
                    parser->working_pos += MHAS_SYNC_WORD_LENGTH;
                    //parser->working_start += MHAS_SYNC_WORD_LENGTH;
                    processed += MHAS_SYNC_WORD_LENGTH;
                    parser->current_state = STATE_READING_HEADER;
                    if (debug) {
                        AM_LOGI("start:%d, pos:%d, size:%d", parser->working_start, parser->working_pos, parser->working_end);
                    }
                } else {
                    processed = input_len;
                    mpegh_reset_parser(parser);
                    goto finish;
                }
                break;
            }

            case STATE_READING_HEADER: {
                AM_LOGI("start:%d, pos:%d, size:%d", parser->working_start, parser->working_pos, parser->working_end);
                working_remain = parser->working_end - parser->working_pos;
                if (MIN_HEADER_SIZE > working_remain) {
                    AM_LOGI("remain%d < %d, need more", working_remain, MIN_HEADER_SIZE);
                    processed = input_len;
                    goto finish;
                }
                if (!mpegh_parse_header(&parser->bit_parser, parser->working_buf + parser->working_pos,
                                  working_remain, &parser->active_header)) {
                    mpegh_reset_parser(parser);
                    processed = input_len;
                    ret = -1;
                    AM_LOGE("parse header error drop lens:%d!", input_len);
                    goto finish;
                }
                parser->working_pos += parser->active_header.header_len;
                working_remain = parser->working_end - parser->working_pos;
                processed = input_len - working_remain;
                parser->current_state = STATE_READING_PAYLOAD;
                AM_LOGI("working_pos:%d, working_remain:%d, current_state:%d", parser->working_pos, working_remain, parser->current_state);
                break;
            }

            case STATE_READING_PAYLOAD: {
                AM_LOGI("start:%d, pos:%d, size:%d", parser->working_start, parser->working_pos, parser->working_end);
                working_remain = parser->working_end - parser->working_pos;
                if (parser->active_header.payload_length > working_remain) {
                    AM_LOGI("remain:%d < %" PRIu64", need more", working_remain, parser->active_header.payload_length);
                    processed = input_len;
                    goto finish;
                }

                parser->working_pos += parser->active_header.payload_length;
                working_remain = parser->working_end - parser->working_pos;
                processed = input_len - working_remain;
                parser->current_state = STATE_READING_HEADER;
                AM_LOGI("pos:%d, remain:%d, processed:%d, input_len:%d, parser->working_end:%d, frame_type:%d",
                            parser->working_pos, working_remain, processed, input_len,
                            parser->working_end, parser->active_header.frame_type);
                if (PACTYP_MPEGH3DAFRAME == parser->active_header.frame_type) {
                    *output = parser->working_buf + parser->working_start;
                    int32_t frame_size = parser->working_pos - parser->working_start;
                    *output_len = frame_size;
                    AM_LOGI("Got frame size:%d, frame_samples:%d, truncSamples:%d, sample_rate:%d, dur_samples:%d",frame_size,
                                            parser->active_header.sample_lens,
                                            parser->active_header.truncSamples, parser->active_header.sample_rate,
                                            parser->active_header.dur_samples);
                    parser->active_header.truncSamples = 0;
                    parser->working_start = parser->working_pos;
                    goto finish; // Frame processing complete
                }
                break;
            }
            default:
                break;
        }
    }

finish:
    mpegh_info->frame_samples = parser->active_header.dur_samples;
    mpegh_info->sample_rate   = parser->active_header.sample_rate;
    mpegh_info->nb_channels   = 6;  //default

    *used_size = processed;
    if (processed == input_len) {
        parser->need_input = 1;
    }

    return ret;
}

int aml_mpegh_parser_open(void **parser_handle, void *pParserConfig)
{
    AM_LOGI("enter");
    if (NULL == parser_handle) {
        AM_LOGE("Input parser_handle = NULL error!");
        return -1;
    }
    parser_config_t *pConfig = (parser_config_t *)pParserConfig;
    aml_mpegh_parser *phandle = NULL;
    phandle = (aml_mpegh_parser *)aml_audio_calloc(1, sizeof(aml_mpegh_parser));
    if (NULL == phandle) {
        AM_LOGE("malloc working buffer size:%zu, error!", sizeof(aml_mpegh_parser));
        goto error;
    }

    phandle->working_buf = (unsigned char *)aml_audio_calloc(1, MAX_MPEGH_FRAME_LENGTH);
    if (NULL == phandle->working_buf) {
        AM_LOGE("malloc working buffer size:%d, error!", MAX_MPEGH_FRAME_LENGTH);
        aml_audio_free(phandle);
        phandle = NULL;
        goto error;
    }

    phandle->current_state = STATE_FINDING_SYNC;//STATE_READING_HEADER;
    phandle->working_end   = 0;
    phandle->working_pos   = 0;
    phandle->header_size   = 0;
    phandle->working_start = 0;
    phandle->need_input    = 1;
    phandle->debug_enable  = 1;
    memset(&phandle->active_header, 0x0, sizeof(MhasHeader));
    if (pConfig) {
        phandle->is_sub_parser = pConfig->isSubParser;
        phandle->is_tv_src_flag = pConfig->isTvFlag;
    }
    *parser_handle = phandle;
    AM_LOGI("success, phandle:%p, is_sub_parser = %d, is_tv_src_falg = %d", phandle, phandle->is_sub_parser, phandle->is_tv_src_flag);
    return 0;

error:
    AM_LOGE("error!");
    (void)aml_mpegh_parser_close(phandle);
    *parser_handle = NULL;
    return -1;
}


int aml_mpegh_parser_close(void *parser_handle)
{
    aml_mpegh_parser *phandle = (aml_mpegh_parser *)parser_handle;

    if (phandle) {
        if (phandle->working_buf) {
            aml_audio_free(phandle->working_buf);
        }
        aml_audio_free(phandle);
    }

    AM_LOGI("closed!");
    return 0;
}

int aml_mpegh_parser_reset(void *parser_handle)
{
    aml_mpegh_parser *phandle = (aml_mpegh_parser *)parser_handle;

    if (phandle) {
        phandle->current_state = STATE_FINDING_SYNC;//STATE_READING_HEADER;
        phandle->working_end   = 0;
        phandle->working_pos   = 0;
        phandle->working_start = 0;
        phandle->header_size   = 0;
        memset(&phandle->active_header, 0x0, sizeof(MhasHeader));
    }

    AM_LOGI("reset!");
    return 0;
}

int aml_mpegh_parser_flush(void *parser_handle)
{
    //use reset to complete flush action.
    aml_mpegh_parser_reset(parser_handle);
    ALOGI("%s exit", __func__);
    return 0;
}

int aml_mpegh_parser_process(void *parserhandle, const void *inABuffer, void *outABuffer, void *parser_callback)
{
    aml_mpegh_parser *phandle = (aml_mpegh_parser *)parserhandle;
    int ret = -1;
    aml_audio_buffer_t *audioBuffer = (aml_audio_buffer_t *)inABuffer;
    const void *inBuffer = audioBuffer->pData;
    size_t inBytes = audioBuffer->size;
    aml_audio_buffer_t *outAudioBuffer = (aml_audio_buffer_t *)outABuffer;
    int retValue = 0;
    void *outBuffer = NULL;
    int32_t outBytes = 0;
    int32_t leftBytes = inBytes;
    int32_t usedBytes = 0, totalUsedBytes = 0;
    int32_t inSize = (int32_t)inBytes;
    char *inBuf = (char *)inBuffer;
    if (!phandle) {
        AM_LOGE("phandle or mpegh_info is NULL!, phandle: %p", phandle);
        return -1;
    }
    aml_mpegh_parser *pParserHanle = (aml_mpegh_parser *)phandle;
    bool is_sub_parser = pParserHanle->is_sub_parser;
    do {
        aml_mpegh_parser_internal(phandle, inBuf, inSize, &outBuffer, &usedBytes, &outBytes, &(phandle->parser_info));
        ALOGI("%s %d usedBytes = %d",__func__, __LINE__, usedBytes);
        totalUsedBytes += usedBytes;
        if (leftBytes >= usedBytes) {
            leftBytes -= usedBytes;
            inSize = leftBytes;
        }
        inBuf = inBuf + usedBytes;

        if (parser_callback && outBuffer && outBytes > 0) {
            aml_parser_data_callback_t *pCallback = (aml_parser_data_callback_t *)parser_callback;
            Func_Write_CallBack __callback = pCallback->callback;
            outAudioBuffer->pData = outBuffer;
            outAudioBuffer->size = outBytes;
            outAudioBuffer->apts = audioBuffer->apts;
            memcpy(&outAudioBuffer->bufFormat, &audioBuffer->bufFormat, sizeof(buffer_data_format_t));

            retValue = (*__callback)(pCallback->common.pAmlParser, outAudioBuffer, phandle);
        }
        AM_LOGV(" is_sub_parser:%d  phandle:%p inBuf:%p inSize(leftBytes):%d %d,  used_bytes:%d totalUsedBytes:%d outBuffer:%p out_frame_size:%d  phandle->parser_info.frame_samples:%d",
            is_sub_parser, phandle, inBuf, inSize, leftBytes, usedBytes, totalUsedBytes, outBuffer, outBytes, phandle->parser_info.frame_samples);

        /*if it is sub parser, shouldn't break directly.
         *one hwsync packet maybe contains multi data frames,
         *so it should loop parse all frames.
         */
        if (!is_sub_parser) {
            retValue = usedBytes;
            break;
        }
    } while (inSize > 0);
    ALOGI("%s %d finish",__func__, __LINE__);

    return retValue;
}

aml_parser_func_t *get_mpegh_parser_func_handle(void)
{
    aml_parser_func_t *amlParserFunc = NULL;

    amlParserFunc = (struct aml_parser_func *)aml_audio_calloc(1, sizeof(struct aml_parser_func));
    if (amlParserFunc) {
        amlParserFunc->f_init       = aml_mpegh_parser_open;
        amlParserFunc->f_deinit     = aml_mpegh_parser_close;
        amlParserFunc->f_process    = aml_mpegh_parser_process;
        amlParserFunc->f_reset      = aml_mpegh_parser_reset;
        amlParserFunc->f_flush      = aml_mpegh_parser_flush;
    } else {
        AM_LOGE(" calloc amlParserFunc:%p failed", amlParserFunc);
        amlParserFunc = NULL;
    }

    return amlParserFunc;
}

