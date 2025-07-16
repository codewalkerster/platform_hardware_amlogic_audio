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
#define LOG_TAG "audio_mpegh_uimanager"
//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <dlfcn.h>
#include <stdlib.h>
#include "aml_mpegh_uimanager.h"
#include "aml_malloc_debug.h"
#include "aml_dump_debug.h"

static int load_mpegh_uimanager_lib(mpegh_uimanager_oper_t *ui_ops)
{
    AM_LOGI("enter");
    mpegh_uimanager_oper_t *mpegh_uiop = (mpegh_uimanager_oper_t *)ui_ops;

    mpegh_uiop->puimanager = dlopen(MPEGH_LIB_PATH, RTLD_NOW);
    if (!mpegh_uiop->puimanager) {
        AM_LOGE("failed to open (libcdkMpeghDecoder.so), %s!", dlerror());
        return -1;
    }

    mpegh_uiop->open = (int (*) (void *)) dlsym(mpegh_uiop->puimanager, "mpegh_uimanager_open");
    if (!mpegh_uiop->open) {
        AM_LOGE("mpegh_uimanager_open dlsym fail");
        goto fail;
    }

    mpegh_uiop->close = (int (*)(void *)) dlsym(mpegh_uiop->puimanager, "mpegh_uimanager_close");
    if (!mpegh_uiop->close) {
        AM_LOGE("mpegh_uimanager_close dlsym fail");
        goto fail;
    }

    mpegh_uiop->FeedMHAS = (int (*) (void *, char *, int)) dlsym(mpegh_uiop->puimanager, "mpegh_uimanager_feedmhas");
    if (!mpegh_uiop->FeedMHAS) {
        AM_LOGE("mpegh_uimanager_feedmhas dlsym fail");
        goto fail;
    }

    mpegh_uiop->UpdateMHAS = (int (*) (void *, char *, int, int *)) dlsym(mpegh_uiop->puimanager, "mpegh_uimanager_updatemhas");
    if (!mpegh_uiop->UpdateMHAS) {
        AM_LOGE("mpegh_uimanager_updatemhas dlsym fail");
        goto fail;
    }

    mpegh_uiop->GetXmlSceneState = (int (*) (void *, char *, int, int, int *)) dlsym(mpegh_uiop->puimanager, "mpegh_uimanager_getxmlscenestate");
    if (!mpegh_uiop->GetXmlSceneState) {
        AM_LOGE("mpegh_uimanager_getxmlscenestate dlsym fail");
        goto fail;
    }

    mpegh_uiop->ApplyXmlAction = (int (*) (void *, char *, int, int *)) dlsym(mpegh_uiop->puimanager, "mpegh_uimanager_applyxmlaction");
    if (!mpegh_uiop->ApplyXmlAction) {
        AM_LOGE("mpegh_uimanager_applyxmlaction dlsym fail");
        goto fail;
    }

    mpegh_uiop->SetPersistenceMemory = (int (*) (void *, char *, int)) dlsym(mpegh_uiop->puimanager, "mpegh_uimanager_setpersistencememory");
    if (!mpegh_uiop->SetPersistenceMemory) {
        AM_LOGE("mpegh_uimanager_setpersistencememory dlsym fail");
        goto fail;
    }

    mpegh_uiop->GetPersistenceMemory = (int (*) (void *, void **, int *)) dlsym(mpegh_uiop->puimanager, "mpegh_uimanager_getpersistencememory");
    if (!mpegh_uiop->GetPersistenceMemory) {
        AM_LOGE("mpegh_uimanager_getpersistencememory dlsym fail");
        goto fail;
    }

    mpegh_uiop->set_mpegh_debug_level = (void (*)(int)) dlsym(mpegh_uiop->puimanager, "set_mpegh_debug_level");
    if (!mpegh_uiop->set_mpegh_debug_level) {
        AM_LOGE("set_mpegh_debug_level dlsym fail");
        goto fail;
    }
    AM_LOGI("success!");

    return 0;

fail:
    AM_LOGE("cant find uimanage function, %s!", dlerror());
    return -1;
}

static int unload_mpegh_uimanager_lib(mpegh_uimanager_oper_t *ui_ops)
{
    AM_LOGI("enter");
    mpegh_uimanager_oper_t *mpegh_uiop = (mpegh_uimanager_oper_t *)ui_ops;

    if (mpegh_uiop->puimanager) {
        dlclose(mpegh_uiop->puimanager);
        mpegh_uiop->puimanager = NULL;
    }
    return 0;
}

int aml_mpegh_uimanager_open(void **ui_ops)
{
    AM_LOGI("enter");
    mpegh_uimanager_oper_t *mpegh_uiop = NULL;
    int ret = -1;

    if (!ui_ops) {
        AM_LOGE("ui_ops NULL, failed!");
        goto fail;
    }

    mpegh_uiop = aml_audio_calloc(1, sizeof(mpegh_uimanager_oper_t));
    if (!mpegh_uiop) {
        AM_LOGE("malloc mpegh_uimanager size:%zu, failed!", sizeof(mpegh_uimanager_oper_t));
        goto fail;
    }

    ret = load_mpegh_uimanager_lib(mpegh_uiop);
    if (ret != 0) {
        AM_LOGE("load_mpegh_uimanager_lib, mpegh_uiop:%p, failed!", mpegh_uiop);
        goto fail;
    }

    if (mpegh_uiop->set_mpegh_debug_level) {
        mpegh_uiop->set_mpegh_debug_level(get_debug_value(AML_DEBUG_AUDIOHAL_DEBUG));
    }

    mpegh_uiop->max_working_size = MPEGH_MAX_INBUF_SIZE;
    mpegh_uiop->working_buf = aml_audio_calloc(1, mpegh_uiop->max_working_size);
    if (!mpegh_uiop->working_buf) {
        AM_LOGE("malloc working_buf size:%d, failed!", mpegh_uiop->max_working_size);
        goto fail;
    }

    ret = mpegh_uiop->open(mpegh_uiop);
    if (ret != 0) {
        AM_LOGE("mpegh_uiop.open, mpegh_uiop:%p, failed!", mpegh_uiop);
        goto fail;
    }

    AM_LOGI("handle:%p, mpegh_uiop:%p, open success!", mpegh_uiop->handle, mpegh_uiop);
    *ui_ops = mpegh_uiop;
    return ret;

fail:
    if (NULL != mpegh_uiop) {
        aml_mpegh_uimanager_close(mpegh_uiop);
        mpegh_uiop = NULL;
    }

    *ui_ops = NULL;
    return ret;
}

int aml_mpegh_uimanager_close(void *ui_ops)
{
    AM_LOGI("enter");
    int ret = 0;
    mpegh_uimanager_oper_t *mpegh_uiop = (mpegh_uimanager_oper_t *)ui_ops;
    if (!mpegh_uiop) {
        return 0;
    }

    if (mpegh_uiop->xmloutbuf) {
        aml_audio_free(mpegh_uiop->xmloutbuf);
        mpegh_uiop->xmloutbuf = NULL;
    }

    if (mpegh_uiop->working_buf) {
        aml_audio_free(mpegh_uiop->working_buf);
        mpegh_uiop->working_buf = NULL;
    }

    ret = mpegh_uiop->close(mpegh_uiop);
    if (ret != 0) {
        AM_LOGE("mpegh_uiop.close, mpegh_uiop:%p, failed!", mpegh_uiop);
    }
    unload_mpegh_uimanager_lib(mpegh_uiop);
    aml_audio_free(mpegh_uiop);
    return ret;
}

int aml_mpegh_uimanager_feedmhas(void *ui_ops, char *inbuf, int inlen)
{
    AM_LOGV("enter");
    mpegh_uimanager_oper_t *mpegh_uiop = (mpegh_uimanager_oper_t *)ui_ops;
    return mpegh_uiop->FeedMHAS(mpegh_uiop, inbuf, inlen);
}

int aml_mpegh_uimanager_updatemhas(void *ui_ops, char *inbuf, int inlen, void **outbuf, int *outlen)
{
    AM_LOGV("enter");
    mpegh_uimanager_oper_t *mpegh_uiop = (mpegh_uimanager_oper_t *)ui_ops;
    *outbuf = mpegh_uiop->working_buf;

    int ret = mpegh_uiop->UpdateMHAS(mpegh_uiop, inbuf, inlen, outlen);
    AM_LOGV("in:%d, out:%d", inlen, *outlen);
    return ret;
}

int aml_mpegh_uimanager_getxmlscenestate(void *ui_ops, int *isupdate)
{
    AM_LOGV("enter");
    mpegh_uimanager_oper_t *mpegh_uiop = (mpegh_uimanager_oper_t *)ui_ops;
    int outflag = 0, ret = 0, extcount = 0;
    char xmltmpbuf[MPEGH_UI_DEFAULT_XMLBUF_SIZE] = {0};
    int xmltmplen = 0, xmloutlen = 0, xmlbuframainspace = 0;
    int clean_buf_flag = 1;

    *isupdate = 0;

    if (!mpegh_uiop->xmloutbuf) {
        mpegh_uiop->xmloutsize = MPEGH_UI_DEFAULT_XMLBUF_SIZE;
        mpegh_uiop->xmloutbuf  = aml_audio_calloc(1, mpegh_uiop->xmloutsize);
        if (!mpegh_uiop->xmloutbuf) {
            AM_LOGE("malloc xmloutbuf size:%d, failed!", mpegh_uiop->xmloutsize);
            return -1;
        }
    }

    while (!(outflag & MPEGH_UI_NO_CHANGE))
    {
        extcount++;
        if (extcount > 20) {
            AM_LOGE("over flow error, extcount:%d, outflag:%d", extcount, outflag);
            break;
        }

        ret = mpegh_uiop->GetXmlSceneState(mpegh_uiop, xmltmpbuf, MPEGH_UI_DEFAULT_XMLBUF_SIZE, 0, &outflag);
        xmltmplen = strlen(xmltmpbuf);
        AM_LOGV("outflag:%d, tmplen:%d, outsize:%d", outflag, xmltmplen, mpegh_uiop->xmloutsize);
        if (ret != 0) {
            AM_LOGE("failed to get xml scene state, ret:%d", ret);
            break;
        }

        if (!(outflag & MPEGH_UI_NO_CHANGE)) {
            if (clean_buf_flag == 1) {
                memset(mpegh_uiop->xmloutbuf, 0x0, mpegh_uiop->xmloutsize);
                clean_buf_flag = 0;
            }
            //Todo: get AudioSceneConfig xml message.
            if (extcount == 1 && strstr(xmltmpbuf, "configChanged=\"true\"") != NULL)
                continue;

            xmlbuframainspace = mpegh_uiop->xmloutsize - xmloutlen;
            ALOGI("%s %d xmloutlen = %d, xmlbuframainspace = %d",__func__, __LINE__,xmloutlen, xmlbuframainspace);
            if (xmltmplen >= xmlbuframainspace) {
                mpegh_uiop->xmloutsize += MPEGH_UI_DEFAULT_XMLBUF_SIZE;
                mpegh_uiop->xmloutbuf = aml_audio_realloc(mpegh_uiop->xmloutbuf, mpegh_uiop->xmloutsize);
                if (!mpegh_uiop->xmloutbuf) {
                    AM_LOGE("failed to malloc xmloutbuf, size:%d", mpegh_uiop->xmloutsize);
                    return -1;
                }
                memset((mpegh_uiop->xmloutbuf + mpegh_uiop->xmloutsize - MPEGH_UI_DEFAULT_XMLBUF_SIZE - 1), 0x0, MPEGH_UI_DEFAULT_XMLBUF_SIZE);
            }
            xmloutlen += xmltmplen;
            strncat(mpegh_uiop->xmloutbuf, xmltmpbuf, xmltmplen);
            *isupdate = 1;
        }
    }

    return ret;
}

int aml_mpegh_uimanager_applyxmlaction(void *ui_ops, char *xmlinbuf, int xmlinsize, int *flagsout)
{
    AM_LOGI("enter");
    mpegh_uimanager_oper_t *mpegh_uiop = (mpegh_uimanager_oper_t *)ui_ops;

    int ret = mpegh_uiop->ApplyXmlAction(mpegh_uiop, xmlinbuf, xmlinsize, flagsout);
    AM_LOGI("out, ret:%d, xmlinsize:%d, flagsout:%d", ret, xmlinsize, *flagsout);
    return ret;
}


int aml_mpegh_uimanager_setpersistencememory(void *ui_ops, char *persistencemem, int persistencememsize)
{
    AM_LOGI("enter");
    mpegh_uimanager_oper_t *mpegh_uiop = (mpegh_uimanager_oper_t *)ui_ops;

    int ret = mpegh_uiop->SetPersistenceMemory(mpegh_uiop, persistencemem, persistencememsize);
    AM_LOGI("out, ret:%d, persistencememsize:%d", ret, persistencememsize);
    return ret;
}

int aml_mpegh_uimanager_getpersistencememory(void *ui_ops, void **persistencemem, int *persistencememsize)
{
    AM_LOGI("enter");
    mpegh_uimanager_oper_t *mpegh_uiop = (mpegh_uimanager_oper_t *)ui_ops;

    int ret = mpegh_uiop->GetPersistenceMemory(mpegh_uiop, persistencemem, persistencememsize);
    AM_LOGI("out, ret:%d, persistencememsize:%d", ret, *persistencememsize);
    return ret;
}

