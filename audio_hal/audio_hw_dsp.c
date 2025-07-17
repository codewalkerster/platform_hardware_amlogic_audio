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
#include <cutils/log.h>
#include <time.h>

#include "rpc_client_pcm.h"
#include "rpc_client_shm.h"
#include "rpc_client_aipc.h"
#include <audio_hw_dsp.h>
#include <string.h>
#include "aml_flatbuf_api.h"
#include "aml_malloc_debug.h"
#include <errno.h>
#include "alsa_device_parser.h"

#define UNUSED(x)	(void)(x)
struct pcm_open_config* open_config = NULL;
int sound_trigger_cmd = SOUND_TRIGGER_CLOSE_DEVICE;

/* copy the struct from dsp_util/hifi4rpc_client/rpc_client_pcm.c
 for pcm_client_open().
 */
struct tAmlPcmCtx {
    tAcodecPcmSrvHdl pcm_srv_hdl;
    rpc_pcm_config config;
    int aipchdl;
};

typedef struct {
    AML_FLATBUF_HANDLE hFlat;
    void *buf_fetch;
    void *buf;
    uint32_t size;
    uint32_t fetched_size;
    pthread_t task;
} dumper_t;

void * sound_trigger_hdl = NULL;
void* pcm_open_dsp(unsigned int card,
        unsigned int device,
        unsigned int flags,
        struct pcm_config *config)
{
    rpc_pcm_config rpc_config;
    rpc_config.channels = config->channels;
    rpc_config.rate = config->rate;
    rpc_config.period_size = config->period_size;
    rpc_config.period_count = config->period_count;
    rpc_config.format = config->format;
    rpc_config.start_threshold = config->start_threshold;
    rpc_config.stop_threshold = config->stop_threshold;
    rpc_config.silence_threshold = config->silence_threshold;
    rpc_config.period_count = 60; // dma buffer is 4s
    ALOGI("%s, %d, card=%u device=%u flags=%x channel=%d rate=%d card = %u format=%u period_size=%d period_count=%d\n",
            __func__, __LINE__, card, device, flags, rpc_config.channels, rpc_config.rate, card, rpc_config.format, rpc_config.period_size, rpc_config.period_count);

    if (sound_trigger_hdl == NULL)
        sound_trigger_hdl = pcm_client_open(card, device, flags, &rpc_config);
    else
        ALOGI("sound_trigger_hdl is exist %p\n", sound_trigger_hdl);

    if (get_sound_trigger_cmd() == SOUND_TRIGGER_DEFAULT)
        send_ffv_suspend_status(0, false);

    return sound_trigger_hdl;
}

void send_pcm_open_config_dsp(unsigned int card,
        unsigned int device,
        unsigned int flags,
        struct pcm_config *config)
{
    if (open_config == NULL)
        open_config = aml_audio_malloc(sizeof(struct pcm_open_config));
    open_config->card = card;
    open_config->device = device;
    open_config->flags = flags;
    open_config->config = config;
}

struct pcm_open_config*  get_pcm_open_config_sound_trigger()
{
    struct pcm_open_config* sound_trigger_config = open_config;
    return sound_trigger_config;
}

int pcm_close_dsp(void* hdl)
{
    int ret;
    aml_audio_free(open_config);
    open_config = NULL;
    ret = pcm_client_close(hdl);
    sound_trigger_hdl = NULL;
    // Notify DSP that ffv has been turned off
    aml_enable_ffv_to_dsp(false);
    return ret;
}

uint32_t pcm_client_bytes_to_frame_dsp(int sound_trigger_hdl_num, uint32_t bytes)
{
    void* hdl = open_config->dsp_pcm_handles[sound_trigger_hdl_num];
    return pcm_client_bytes_to_frame(hdl, bytes);
}

int alsa_device_update_pcm_index_dsp(int alsaPORT, int stream)
{
    if (alsaPORT == PORT_BUILTINMIC && stream == CAPTURE)
        return 5;
    else
        return 0;
}

int pcm_read_dsp(void* hdl, void *data, unsigned int bytes)
{
    int ret = 0;
    if (hdl == NULL) {
        ALOGE("%s %d hdl is NULL\n", __func__, __LINE__);
        return -1;
    }
    uint32_t fr = pcm_client_bytes_to_frame(hdl, bytes);
    void* hShmBuf;
    hShmBuf = AML_MEM_Allocate(bytes);
    if (hShmBuf == NULL) {
        ALOGE("%s %d AML_MEM_Allocate failed\n", __func__, __LINE__);
        return -1;
    }
    void *buf = AML_MEM_GetVirtAddr(hShmBuf);

    if (buf == NULL) {
        ALOGE("%s %d AML_MEM_GetVirtAddr failed\n", __func__, __LINE__);
        return -1;
    }
    void *phybuf = AML_MEM_GetPhyAddr(hShmBuf);

    ret = pcm_client_readi(hdl, phybuf, fr);
    AML_MEM_Invalidate(phybuf, bytes);

    memcpy(data, buf, bytes);
    AML_MEM_Free(hShmBuf);
    return ret;
}

int dumper_close(dumper_t *p)
{
    int ret = 0;
    if (p == NULL) {
        return ret;
    }
    if (p->task) {
        pthread_join(p->task, (void *)&p);
        ret = p->fetched_size;
    }
    if (p->hFlat) {
        AML_FLATBUF_Destroy(p->hFlat);
    }
    if (p->buf_fetch) {
        aml_audio_free(p->buf_fetch);
    }
    aml_audio_free(p);
    return ret;
}

static int aml_audio_dump_audio_bitstreams(const char *path, const void *buf, size_t bytes)
{
    if (!path) {
        return -1;
    }

    FILE *fp = fopen(path, "a+");
    if (fp) {
        fwrite((char *)buf, 1, bytes, fp);
        fclose(fp);
        return 0;
    }
    ALOGE("fail to open path=%s, errno=%d/%s",
            path, errno, strerror(errno));

    return -1;
}

void adjustData(int frames, int16_t* input, int16_t* output)
{
    int i;
    for (i = 0; i < frames/2; i++) {
        output[i * 2] = input[i];
        output[i * 2 + 1] = input[frames/2 + i];
    }
}

void *dumper_thread(void *t)
{
    dumper_t *p = (dumper_t *)t;
    int ret = 0;
    ret = AML_FLATBUF_Read(p->hFlat, p->buf_fetch, p->size, 10);
    if (ret > 0)
        adjustData(ret/2, (int16_t*)p->buf_fetch, (int16_t*)p->buf);
    p->fetched_size += ret;
    return p;
}

dumper_t *dumper_open(const char *id, void* buf, size_t chunk_size)
{
    dumper_t *p = aml_audio_malloc(sizeof(dumper_t));
    if (p == NULL) {
        ALOGE("fail to allocate dumper\n");
        return NULL;
    }
    memset(p, 0x00, sizeof(dumper_t));

    p->buf = buf;
    struct flatbuffer_config cfg;
    cfg.size = chunk_size;
    cfg.phy_ch = FLATBUF_CH_ARM2DSPA;
    p->hFlat = AML_FLATBUF_Create(id, FLATBUF_FLAG_RD, &cfg);
    if (p->hFlat == NULL) {
        ALOGE("fail to create flatbuf id=%s\n", id);
        goto fail;
    }

    p->buf_fetch = aml_audio_malloc(chunk_size);
    if (p->buf_fetch == NULL) {
        ALOGE("fail to create transfer buffer size=%zu\n", chunk_size);
        goto fail;
    }
    p->size = chunk_size;
    p->fetched_size = 0;
    int r = pthread_create(&p->task, NULL, dumper_thread, p);
    if (r != 0) {
        ALOGE("fail to create dumper's task, r=%d\n", r);
        goto fail;
    }

    return p;

fail:
    dumper_close(p);
    return NULL;
}

int fetch_suspend_data_from_dsp(void* buf)
{
    int ret;
    dumper_t *d0 = dumper_open("VWE.ASR", buf, 16 * 2 * 2000 * 2); /* 16KHz 16bit 2s 2ch */
    if (d0 == NULL) {
        ALOGE("%s %d fail to create dumper\n", __func__, __LINE__);
        return -1;
    }

    int h = xAudio_Ipc_init();
    if (h < 0) {
        dumper_close(d0);
        return 0;
    }
    xAIPC(h, MBX_CMD_VAD_AWE_WAKEUP_END, NULL, 0);
    xAudio_Ipc_Deinit(h);

    ret = dumper_close(d0);

    return ret;
}

void pcm_get_latency_dsp(int sound_trigger_hdl_num, unsigned int *avail_dsp)
{
    void* hdl = open_config->dsp_pcm_handles[sound_trigger_hdl_num];
    *avail_dsp = pcm_client_get_latency(hdl) * (open_config->config->rate / 1000);
}

void set_sound_trigger_cmd(int cmd)
{
    sound_trigger_cmd = cmd;
}

int get_sound_trigger_cmd(void)
{
    return sound_trigger_cmd;
}

void send_ffv_suspend_status(int sound_trigger_hdl_num, bool ffv_suspend_status)
{
    vad_awe_wakeup_dsp param = {1, 1, 1, 0};

    if (ffv_suspend_status) {
        void* hdl = open_config->dsp_pcm_handles[sound_trigger_hdl_num];
        struct tAmlPcmCtx* pcm_hdl = (struct tAmlPcmCtx*)hdl;

        param.hdl = pcm_hdl->pcm_srv_hdl;
    } else {
        param.times = 0;
        param.is_dsp_clk = 0;
        param.is_arm_on = 0;
    }
    ALOGD("%s ffv_suspend_status=%d\n", __func__, ffv_suspend_status);

    int h = xAudio_Ipc_init();

    if (h < 0)
        return;
    xAIPC_SEND(h, MBX_CMD_VAD_AWE_WAKEUP, &param, sizeof(param));
    xAudio_Ipc_Deinit(h);
}

void aml_enable_ffv_to_dsp(bool enable_ffv)
{
    bool arg;
    int h = xAudio_Ipc_init();

    if (h < 0)
        return;
    arg = enable_ffv;
    xAIPC_SEND(h, MBX_CMD_AML_ENABLE_FFV, &arg, sizeof(arg));
    xAudio_Ipc_Deinit(h);
    ALOGD("%s enable_ffv=%d\n", __func__, enable_ffv);
}

