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
#undef  LOG_TAG
#define LOG_TAG "audio_hw_utils_uevent"
//#define LOG_NDEBUG 0

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <cutils/log.h>
#include <aml_dump_debug.h>
#include <sys/epoll.h>
#include <cutils/uevent.h>
#include "aml_audio_uevent.h"

#define  EPOLL_MAX_EVENTS  16
#define  INPUT_MAX_EVENTS  128
#define  NO_TIMEOUT  -1

#define UEVENT_MSG_LEN 2048


typedef struct audio_uevent_handle {
    int epoll_fd;
    int uevent_fd;
    pthread_t  thread_id;
    bool  bexit;
    struct epoll_event pending_event_items[EPOLL_MAX_EVENTS];
    uevent_callback_t event_callback;

} audio_uevent_handle_t;

struct aml_uevent_item {
    int  type;
    char event_name[128];
};


static struct aml_uevent_item uevent_list[] = {
    { UEVENT_TYPE_VMODE_CHANGE, UEVENT_HDMITX_VMODE_CHANGE },
};


static audio_uevent_handle_t audio_uevent_listener = { 0 };


static void process_uevent(audio_uevent_handle_t * p_handle) {
    char msg[UEVENT_MSG_LEN + 2];
    char* cp;
    int n;
    int i = 0;
    n = uevent_kernel_multicast_recv(p_handle->uevent_fd, msg, UEVENT_MSG_LEN);
    if (n <= 0) return;
    if (n >= UEVENT_MSG_LEN) /* overflow -- discard */
        return;

    msg[n] = '\0';
    msg[n + 1] = '\0';

    for (i = 0; i < sizeof(uevent_list) / sizeof(struct aml_uevent_item); i++) {
        cp = msg;
        while (*cp) {
            //ALOGI("%s uevent =%s", __func__, cp);
            if (strstr(cp, uevent_list[i].event_name)) {
                if (p_handle->event_callback) {
                    p_handle->event_callback(uevent_list[i].type);
                }
            }
            /* advance to after the next \0 */
            while (*cp++);
        }
    }
    return;
}


static void *audio_uevent_thread(void *pArg)
{
    audio_uevent_handle_t * p_handle = (audio_uevent_handle_t *)pArg;
    ALOGI("enter %s", __FUNCTION__);
    while (!p_handle->bexit) {
        int eventNum = epoll_wait(p_handle->epoll_fd, p_handle->pending_event_items, EPOLL_MAX_EVENTS, NO_TIMEOUT);
        if (eventNum <= 0) {
            ALOGE("epoll_wait fails.");
            continue;
        }
        for (int i = 0; i < eventNum; i++) {
            if (p_handle->pending_event_items[i].events & EPOLLIN) {
                process_uevent(p_handle);
            }
        }
    }
    ALOGI("exit %s", __FUNCTION__);
    return ((void *)0);
}



void aml_audio_uevent_open(uevent_callback_t callback) {
    int epoll_fd = 0;
    int uevent_fd = 0;

    memset(&audio_uevent_listener, 0, sizeof(struct audio_uevent_handle));
    //uevent
    uevent_fd = uevent_open_socket(64 * 1024, true);
    if (uevent_fd < 0) {
        ALOGE("uevent_open_socket failed.");
        return;
    }
    if (fcntl(uevent_fd, F_SETFL, O_NONBLOCK) == -1) {
        ALOGE("fcntl mUeventFd failed.");
        return;
    }

    //epoll
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0) {
        ALOGE("epoll_create failed.");
        return;
    }
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = uevent_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, uevent_fd, &ev) == -1) {
        ALOGE("epoll_ctl failed.");
        return;
    }

    audio_uevent_listener.epoll_fd  = epoll_fd;
    audio_uevent_listener.uevent_fd = uevent_fd;
    audio_uevent_listener.event_callback = callback;

    if (pthread_create(&audio_uevent_listener.thread_id, NULL, &audio_uevent_thread, (void *)&audio_uevent_listener)) {
        ALOGE("%s create thread failed", __FUNCTION__);
        return;
    }


}



void aml_audio_uevent_close() {
    if (audio_uevent_listener.epoll_fd) {
        close(audio_uevent_listener.epoll_fd);
    }

    if (audio_uevent_listener.uevent_fd) {
        close(audio_uevent_listener.uevent_fd);
    }

    audio_uevent_listener.bexit = true;
    if (audio_uevent_listener.thread_id != 0) {
        pthread_join(audio_uevent_listener.thread_id, NULL);
    }
    return;
}

