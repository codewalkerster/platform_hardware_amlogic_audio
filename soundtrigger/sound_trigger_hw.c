/*
 * Copyright (C) 2011 The Android Open Source Project
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


/* This HAL simulates triggers from the DSP.
 * To send a trigger from the command line you can type:
 *
 * adb forward tcp:14035 tcp:14035
 *
 * telnet localhost 14035
 *
 * Commands include:
 * ls : Lists all models that have been loaded.
 * trig <uuid> : Sends a recognition event for the model at the given uuid
 * update <uuid> : Sends a model update event for the model at the given uuid.
 * close : Closes the network connection.
 *
 * To enable this file, you can make with command line parameter
 * SOUND_TRIGGER_USE_STUB_MODULE=1
 */
#define MAX_GENERIC_SOUND_MODELS    (9)
#define MAX_KEY_PHRASES             (1)
#define MAX_MODELS                  (MAX_GENERIC_SOUND_MODELS + MAX_KEY_PHRASES)

#define LOG_TAG "Amlogic_sound_trigger_hw"
#define LOG_NDEBUG 1
#define PARSE_BUF_LEN 1024  // Length of the parsing buffer.S

#define EVENT_RECOGNITION 1
#define EVENT_SOUND_MODEL 2

// The following commands work with the network port:
#define COMMAND_LS "ls"
#define COMMAND_RECOGNITION_TRIGGER "trig"  // Argument: model index.
#define COMMAND_RECOGNITION_ABORT "abort"  // Argument: model index.
#define COMMAND_RECOGNITION_FAILURE "fail"  // Argument: model index.
#define COMMAND_UPDATE "update"  // Argument: model index.
#define COMMAND_CLEAR "clear" // Removes all models from the list.
#define COMMAND_CLOSE "close" // Close just closes the network port, keeps thread running.
#define COMMAND_END "end" // Closes connection and stops the thread.

#define ERROR_BAD_COMMAND "Bad command"

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <log/log.h>

#include <hardware/hardware.h>
#include <system/sound_trigger.h>
#include <hardware/sound_trigger.h>

#include "audio_hw_dsp.h"

#define MAX_GENERIC_SOUND_MODELS    (9)
#define MAX_KEY_PHRASES             (1)
#define MAX_MODELS                  (MAX_GENERIC_SOUND_MODELS + MAX_KEY_PHRASES)

#define MAX_USERS                   (1)
#define MAX_BUFFER_MS               (3000)
#define POWER_CONSUMPTION           (0) // TBD

static struct sound_trigger_properties_extended_1_3 hw_properties_extended = {
    {
        SOUND_TRIGGER_DEVICE_API_VERSION_1_3, //ST version
        sizeof(struct sound_trigger_properties_extended_1_3)
    },
    {
        "Amlogic Audio", // implementor
        "Sound Trigger stub HAL", // description
        1, // version
        { 0x73d8a066, 0x29f9, 0x3744, 0xb206, { 0xa4, 0x44, 0xce, 0x4b, 0x45, 0xa6 } }, // uuid
        4, // max_sound_models
        1, // max_key_phrases
        1, // max_users
        RECOGNITION_MODE_VOICE_TRIGGER, // recognition_modes
        true, // capture_transition
        0, // max_buffer_ms
        true, // concurrent_capture
        true, // trigger_in_event
        0 // power_consumption_mw
    },
    "", //supported arch
    0,                                      // audio capability
};

struct recognition_context {
    // Sound Model information, added in method load_sound_model
    sound_model_handle_t model_handle;
    sound_trigger_uuid_t model_uuid;
    sound_trigger_sound_model_type_t model_type;
    sound_model_callback_t model_callback;
    void *model_cookie;

    // Sound Model information, added in start_recognition
    struct sound_trigger_recognition_config *config;
    recognition_callback_t recognition_callback;
    void *recognition_cookie;

    bool model_started;

    // Next recognition_context in the linked list
    struct recognition_context *next;
};

struct amlogic_sound_trigger_device {
    struct sound_trigger_hw_device device;
    pthread_mutex_t lock;

    // This thread opens a port that can be used to monitor and inject events
    // into the stub HAL.
    //pthread_t control_thread;

    // Recognition contexts are stored as a linked list
    struct recognition_context *root_model_context;
    pthread_t callback_thread;

    struct pcm_open_config open_config;
    int sound_trigger_handle;
    void* dsp_pcm_handles[10];
    int next_sound_model_id;
};

struct amlogic_sound_trigger_device a_stdev =  { .lock = PTHREAD_MUTEX_INITIALIZER };

static bool check_uuid_equality(sound_trigger_uuid_t uuid1, sound_trigger_uuid_t uuid2)
{
    if (uuid1.timeLow != uuid2.timeLow ||
        uuid1.timeMid != uuid2.timeMid ||
        uuid1.timeHiAndVersion != uuid2.timeHiAndVersion ||
        uuid1.clockSeq != uuid2.clockSeq) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (uuid1.node[i] != uuid2.node[i]) {
            return false;
        }
    }
    return true;
}

bool str_to_uuid(char* uuid_str, sound_trigger_uuid_t* uuid)
{
    if (uuid_str == NULL) {
        ALOGI("Invalid str_to_uuid input.");
        return false;
    }

    int tmp[10];
    if (sscanf(uuid_str, "%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
               tmp, tmp+1, tmp+2, tmp+3, tmp+4, tmp+5, tmp+6, tmp+7, tmp+8, tmp+9) < 10) {
        ALOGI("Invalid UUID, got: %s", uuid_str);
        return false;
    }
    uuid->timeLow = (unsigned int)tmp[0];
    uuid->timeMid = (unsigned short)tmp[1];
    uuid->timeHiAndVersion = (unsigned short)tmp[2];
    uuid->clockSeq = (unsigned short)tmp[3];
    uuid->node[0] = (unsigned char)tmp[4];
    uuid->node[1] = (unsigned char)tmp[5];
    uuid->node[2] = (unsigned char)tmp[6];
    uuid->node[3] = (unsigned char)tmp[7];
    uuid->node[4] = (unsigned char)tmp[8];
    uuid->node[5] = (unsigned char)tmp[9];
    return true;
}

/* Will reuse ids when overflow occurs */
static sound_model_handle_t generate_sound_model_handle(const struct sound_trigger_hw_device *dev)
{
    struct amlogic_sound_trigger_device *stdev = (struct amlogic_sound_trigger_device *)dev;
    int new_id = stdev->next_sound_model_id;
    ++stdev->next_sound_model_id;
    if (stdev->next_sound_model_id == 0) {
        stdev->next_sound_model_id = 1;
    }

    ALOGI("%s %d new_id handle=%d\n", __func__, __LINE__, new_id);
    return (sound_model_handle_t) new_id;
}

static bool recognition_callback_exists(struct amlogic_sound_trigger_device *stdev)
{
    bool callback_found = false;
    if (stdev->root_model_context) {
        struct recognition_context *current_model_context = stdev->root_model_context;
        while (current_model_context) {
            if (current_model_context->recognition_callback != NULL) {
                callback_found = true;
                break;
            }
            current_model_context = current_model_context->next;
        }
    }
    return callback_found;
}

static struct recognition_context * get_model_context(struct amlogic_sound_trigger_device *stdev,
            sound_model_handle_t handle)
{
    struct recognition_context *model_context = NULL;
    if (stdev->root_model_context) {
        struct recognition_context *current_model_context = stdev->root_model_context;
        while (current_model_context) {
            if (current_model_context->model_handle == handle) {
                model_context = current_model_context;
                break;
            }
            current_model_context = current_model_context->next;
        }
    }
    return model_context;
}

static int stdev_get_properties(const struct sound_trigger_hw_device *dev,
                                struct sound_trigger_properties *properties)
{
    struct amlogic_sound_trigger_device *stdev = (struct amlogic_sound_trigger_device *)dev;

    ALOGI("%s", __func__);
    if (properties == NULL) {
        ALOGE("%s properties is NULL", __func__);
        return -EINVAL;
    }
    memcpy(properties, &hw_properties_extended.base, sizeof(struct sound_trigger_properties));
    return 0;
}

static int stdev_load_sound_model(const struct sound_trigger_hw_device *dev,
                                  struct sound_trigger_sound_model *sound_model,
                                  sound_model_callback_t callback,
                                  void *cookie,
                                  sound_model_handle_t *handle)
{
    struct amlogic_sound_trigger_device *stdev = (struct amlogic_sound_trigger_device *)dev;
    int ret = 0;
    pthread_mutex_lock(&stdev->lock);
    if (handle == NULL || sound_model == NULL) {
        ALOGE("%s NULL pointer", __func__);
        ret = -EINVAL;
        goto exit;
    }
    if (sound_model->data_size == 0 ||
            sound_model->data_offset < sizeof(struct sound_trigger_sound_model)) {
        ALOGE("%s sound_model is error", __func__);
        ret = -EINVAL;
        goto exit;
    }

    if (sound_model->type != SOUND_MODEL_TYPE_KEYPHRASE) {
        ALOGE("Unsupported sound model type: %d", sound_model->type);
        ret = -EINVAL;
        goto exit;
    }

    struct recognition_context *model_context;
    model_context = malloc(sizeof(struct recognition_context));
    if (!model_context) {
        ALOGE("Could not allocate recognition_context");
        ret = -ENOMEM;
        goto exit;
    }

    // Add the new model context to the recognition_context linked list
    if (stdev->root_model_context) {
        // Find the tail
        struct recognition_context *current_model_context = stdev->root_model_context;
        unsigned int model_count = 0;
        while (current_model_context->next) {
            current_model_context = current_model_context->next;
            model_count++;
            if (model_count >= hw_properties_extended.base.max_sound_models) {
                ALOGW("Can't load model: reached max sound model limit");
                free(model_context);
                ret = -EINVAL;
                goto exit;
            }
        }
        current_model_context->next = model_context;
    } else {
        stdev->root_model_context = model_context;
    }

    model_context->model_handle = generate_sound_model_handle(dev);
    *handle = model_context->model_handle;
    model_context->model_type = sound_model->type;

    char *data = (char *)sound_model + sound_model->data_offset;
    ALOGI("%s data size %d data %d - %d", __func__,
          sound_model->data_size, data[0], data[sound_model->data_size - 1]);
    model_context->model_uuid = sound_model->uuid;
    model_context->model_callback = callback;
    model_context->model_cookie = cookie;
    model_context->config = NULL;
    model_context->recognition_callback = NULL;
    model_context->recognition_cookie = NULL;
    model_context->next = NULL;
    model_context->model_started = false;
    ALOGI("Sound model loaded: Handle %d ", *handle);

exit:
    pthread_mutex_unlock(&stdev->lock);
    return ret;
}

static int stdev_unload_sound_model(const struct sound_trigger_hw_device *dev,
                                    sound_model_handle_t handle)
{
    // If recognizing, stop_recognition must be called for a sound model before unload_sound_model
    struct amlogic_sound_trigger_device *stdev = (struct amlogic_sound_trigger_device *)dev;
    int status = 0;
    ALOGI("unload_sound_model:%d", handle);
    pthread_mutex_lock(&stdev->lock);

    struct recognition_context *model_context = NULL;
    struct recognition_context *previous_model_context = NULL;
    if (stdev->root_model_context) {
        struct recognition_context *current_model_context = stdev->root_model_context;
        while (current_model_context) {
            if (current_model_context->model_handle == handle) {
                model_context = current_model_context;
                break;
            }
            previous_model_context = current_model_context;
            current_model_context = current_model_context->next;
        }
    }
    if (!model_context) {
        ALOGW("Can't find sound model handle %d in registered list", handle);
        status = -ENOSYS;
        goto exit;
    }
    if (previous_model_context) {
        previous_model_context->next = model_context->next;
    } else {
        stdev->root_model_context = model_context->next;
    }
    free(model_context->config);
    free(model_context);

exit:
    pthread_mutex_unlock(&stdev->lock);
    return status;
}

static char *sound_trigger_event_alloc(struct amlogic_sound_trigger_device *
                                       stdev)
{
    char *data;
    struct sound_trigger_phrase_recognition_event *event;
    data = (char *)calloc(1,
                    sizeof(struct sound_trigger_phrase_recognition_event));
    if (!data)
        return NULL;
    event = (struct sound_trigger_phrase_recognition_event *)data;
    event->common.status = RECOGNITION_STATUS_SUCCESS;
    event->common.type = SOUND_MODEL_TYPE_KEYPHRASE;
    event->common.model = stdev->root_model_context->model_handle;
    if (stdev->root_model_context->config) {
        unsigned int i;
        event->num_phrases = stdev->root_model_context->config->num_phrases;
        if (event->num_phrases > SOUND_TRIGGER_MAX_PHRASES)
            event->num_phrases = SOUND_TRIGGER_MAX_PHRASES;
        for (i=0; i < event->num_phrases; i++)
            memcpy(&event->phrase_extras[i], &stdev->root_model_context->config->phrases[i],
                   sizeof(struct sound_trigger_phrase_recognition_extra));
    }
    event->num_phrases = 1;
    event->phrase_extras[0].confidence_level = 100;
    event->phrase_extras[0].num_levels = 1;
    event->phrase_extras[0].levels[0].level = 100;
    event->phrase_extras[0].levels[0].user_id = 0;
    // Signify that all the data is coming through streaming, not through the
    // buffer.
    event->common.capture_available = true;
    event->common.audio_config = AUDIO_CONFIG_INITIALIZER;
    event->common.audio_config.sample_rate = 16000;
    event->common.audio_config.channel_mask = AUDIO_CHANNEL_IN_STEREO;
    event->common.audio_config.format = AUDIO_FORMAT_PCM_16_BIT;
    return data;
}

void callback_wakeup_event(void)
{
    struct amlogic_sound_trigger_device *stdev = &a_stdev;

    pthread_mutex_lock(&stdev->lock);
    if (stdev->root_model_context->recognition_callback == NULL) {
        ALOGE("%s recognition_callback is NULL", __func__);
        goto exit;
    }
    struct sound_trigger_phrase_recognition_event *event;
    event = (struct sound_trigger_phrase_recognition_event *)
                sound_trigger_event_alloc(stdev);
    if (!event) {
        goto exit;
    }
    ALOGI("%s send callback model %d", __func__,
            stdev->root_model_context->model_handle);

    stdev->root_model_context->recognition_callback(&event->common,
                                stdev->root_model_context->recognition_cookie);

    free(event);

exit:
    /* Leave the device open for streaming. */
    pthread_mutex_unlock(&stdev->lock);
    stdev->root_model_context->recognition_callback = NULL;
    return;
}

static int stdev_start_recognition(const struct sound_trigger_hw_device *dev,
                                   sound_model_handle_t handle,
                                   const struct sound_trigger_recognition_config *config,
                                   recognition_callback_t callback,
                                   void *cookie)
{
    struct amlogic_sound_trigger_device *stdev = (struct amlogic_sound_trigger_device *)dev;
    pthread_mutex_lock(&stdev->lock);
    /* If other models running with callbacks, don't start trigger thread */
    bool other_callbacks_found = recognition_callback_exists(stdev);
    int ret = 0;

    if (get_sound_trigger_cmd() != SOUND_TRIGGER_WAKEUP_KEYWORD)
        set_sound_trigger_cmd(SOUND_TRIGGER_DEFAULT);
    stdev->root_model_context = get_model_context(stdev, handle);
    if (!stdev->root_model_context) {
        ALOGW("Can't find sound model handle %d in registered list", handle);
        ret = -ENOSYS;
        goto exit;
    }

    free(stdev->root_model_context->config);
    stdev->root_model_context->config = NULL;
    if (config) {
        stdev->root_model_context->config = malloc(sizeof(*config));
        if (!stdev->root_model_context->config) {
            ALOGW("Can't find sound model handle %d in registered list", handle);
            ret = -ENOMEM;
            goto exit;
        }
        memcpy(stdev->root_model_context->config, config, sizeof(*config));
    }
    stdev->root_model_context->recognition_callback = callback;
    stdev->root_model_context->recognition_cookie = cookie;
    stdev->root_model_context->model_started = true;
    ALOGI("%s done for handle %d", __func__, handle);

exit:
    pthread_mutex_unlock(&stdev->lock);
    return ret;
}

static int stdev_stop_recognition(const struct sound_trigger_hw_device *dev,
            sound_model_handle_t handle)
{
    int status = 0;
    struct amlogic_sound_trigger_device *stdev = (struct amlogic_sound_trigger_device *)dev;
    ALOGI("%s", __func__);
    pthread_mutex_lock(&stdev->lock);
    set_sound_trigger_cmd(SOUND_TRIGGER_CLOSE_DEVICE);

    struct recognition_context *model_context = get_model_context(stdev, handle);
    if (!model_context) {
        ALOGW("Can't find sound model handle %d in registered list", handle);
        status = -ENOSYS;
        goto exit;
    }

    free(model_context->config);
    model_context->config = NULL;
    model_context->recognition_callback = NULL;
    model_context->recognition_cookie = NULL;
    model_context->model_started = false;

    ALOGI("%s done for handle %d", __func__, handle);

exit:
    pthread_mutex_unlock(&stdev->lock);
    return status;
}

static int stdev_stop_all_recognitions(const struct sound_trigger_hw_device *dev)
{
    struct amlogic_sound_trigger_device *stdev = (struct amlogic_sound_trigger_device *)dev;
    ALOGI("%s", __func__);
    pthread_mutex_lock(&stdev->lock);
    struct recognition_context *model_context = stdev->root_model_context;
    while (model_context) {
        free(model_context->config);
        model_context->config = NULL;
        model_context->recognition_callback = NULL;
        model_context->recognition_cookie = NULL;
        model_context->model_started = false;
        ALOGI("%s stopped handle %d", __func__, model_context->model_handle);

        model_context = model_context->next;
    }

    pthread_mutex_unlock(&stdev->lock);

    return 0;
}

static int stdev_get_model_state(const struct sound_trigger_hw_device *dev,
                                 sound_model_handle_t handle)
{
    int ret = 0;
    struct amlogic_sound_trigger_device *stdev = (struct amlogic_sound_trigger_device *)dev;
    ALOGI("%s", __func__);
    pthread_mutex_lock(&stdev->lock);

    struct recognition_context *model_context = get_model_context(stdev, handle);
    if (!model_context) {
        ALOGW("Can't find sound model handle %d in registered list", handle);
        ret = -ENOSYS;
        goto exit;
    }

    if (!model_context->model_started) {
        ALOGW("Sound model %d not started", handle);
        ret = -ENOSYS;
        goto exit;
    }

    if (model_context->recognition_callback == NULL) {
        ALOGW("Sound model %d not initialized", handle);
        ret = -ENOSYS;
        goto exit;
    }

    // TODO(mdooley): trigger recognition event

exit:
    pthread_mutex_unlock(&stdev->lock);
    ALOGI("%s done for handle %d", __func__, handle);

    return ret;
}

__attribute__ ((visibility ("default")))
int sound_trigger_open_for_streaming()
{
    struct amlogic_sound_trigger_device *stdev = &a_stdev;
    struct pcm_open_config* sound_trigger_config = get_pcm_open_config_sound_trigger();
    stdev->open_config.card = sound_trigger_config->card;
    stdev->open_config.device = sound_trigger_config->device;
    stdev->open_config.flags = sound_trigger_config->flags;

    stdev->open_config.config = sound_trigger_config->config;

    ALOGI("%s, %d, card=%u device=%u flags=%x \n", __func__, __LINE__, stdev->open_config.card, stdev->open_config.device, stdev->open_config.flags);
    if (stdev->sound_trigger_handle < MAX_MODELS) {
        stdev->sound_trigger_handle++;
    } else {
        stdev->sound_trigger_handle = 1;
    }

    stdev->dsp_pcm_handles[stdev->sound_trigger_handle] = pcm_open_dsp(stdev->open_config.card, stdev->open_config.device, stdev->open_config.flags, stdev->open_config.config);
    if (stdev->dsp_pcm_handles[stdev->sound_trigger_handle] == NULL) {
        ALOGE("%s %d pcm_open failed\n", __func__, __LINE__);
        stdev->sound_trigger_handle--;
        return -EINVAL;
    }
    sound_trigger_config->dsp_pcm_handles[stdev->sound_trigger_handle] = stdev->dsp_pcm_handles[stdev->sound_trigger_handle];
    return stdev->sound_trigger_handle;
}

__attribute__ ((visibility ("default")))
size_t sound_trigger_read_samples(int audio_handle, void *buffer, size_t  buffer_len)
{
    struct amlogic_sound_trigger_device *stdev = &a_stdev;
    size_t ret = pcm_read_dsp(stdev->dsp_pcm_handles[audio_handle], buffer, buffer_len);
    return ret;
}

__attribute__ ((visibility ("default")))
int sound_trigger_close_for_streaming(int audio_handle __unused)
{
    struct amlogic_sound_trigger_device *stdev = &a_stdev;
    if (stdev->dsp_pcm_handles[audio_handle])
        pcm_close_dsp(stdev->dsp_pcm_handles[audio_handle]);
    return 0;
}

static int stdev_close(hw_device_t *device)
{
    // TODO: Implement the ability to stop the control thread. Since this is a
    // test hal, we have skipped implementing this for now. A possible method
    // would register a signal handler for the control thread so that any
    // blocking socket calls can be interrupted. We would send that signal here
    // to interrupt and quit the thread.
    ALOGI("%s", __func__);
    free(device);
    return 0;
}

static const struct sound_trigger_properties_header* stdev_get_properties_extended(
                            const struct sound_trigger_hw_device *dev __unused)
{
    ALOGI("%s", __func__);
    return &hw_properties_extended.header;
}

static int stdev_query_parameter(
                    const struct sound_trigger_hw_device *dev __unused,
                    sound_model_handle_t sound_model_handle __unused,
                    sound_trigger_model_parameter_t model_param __unused,
                    sound_trigger_model_parameter_range_t* param_range)
{
    ALOGW("%s: NOT SUPPORTED", __func__);
    param_range->is_supported = false;
    return 0;
}

static int stdev_set_parameter(
                           const struct sound_trigger_hw_device *dev __unused,
                           sound_model_handle_t sound_model_handle __unused,
                           sound_trigger_model_parameter_t model_param __unused,
                           int32_t value __unused)
{
    ALOGW("%s: NOT SUPPORTED", __func__);
    return 0;
}

static int stdev_get_parameter(
                           const struct sound_trigger_hw_device *dev __unused,
                           sound_model_handle_t sound_model_handle __unused,
                           sound_trigger_model_parameter_t model_param __unused,
                           int32_t* value __unused)
{
    ALOGW("%s: NOT SUPPORTED", __func__);
    return 0;
}

static int stdev_start_recognition_extended(
                        const struct sound_trigger_hw_device *dev,
                        sound_model_handle_t sound_model_handle,
                        const struct sound_trigger_recognition_config_header *header,
                        recognition_callback_t callback,
                        void *cookie)
{
    struct sound_trigger_recognition_config_extended_1_3 *config_1_3 =
                 (struct sound_trigger_recognition_config_extended_1_3 *)header;
    int status = 0;

    if (header->version >= SOUND_TRIGGER_DEVICE_API_VERSION_1_3) {
        /* Use old version before we have real usecase */
        ALOGD("%s: Running 2_3", __func__);
        status = stdev_start_recognition(dev, sound_model_handle,
                                        &config_1_3->base,
                                        callback,
                                        cookie);
    } else {
        /* Rollback into old start recognition */
        ALOGD("%s: Running 2_1", __func__);
        status = stdev_start_recognition(dev, sound_model_handle,
                                        &config_1_3->base,
                                        callback,
                                        cookie);
    }

    return status;
}

static int stdev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct amlogic_sound_trigger_device *stdev;
    int ret;

    ALOGI("%s %d HARDWARE_MODULE_TAG=%x SOUND_TRIGGER_MODULE_API_VERSION_1_0=%x\n HARDWARE_HAL_API_VERSION=%x SOUND_TRIGGER_HARDWARE_MODULE_ID=%s",
    __func__, __LINE__, HARDWARE_MODULE_TAG, SOUND_TRIGGER_MODULE_API_VERSION_1_0, HARDWARE_HAL_API_VERSION, SOUND_TRIGGER_HARDWARE_MODULE_ID);

    stdev = &a_stdev;
    if (strcmp(name, SOUND_TRIGGER_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    stdev->next_sound_model_id = 1;
    stdev->root_model_context = NULL;

    stdev->device.common.tag = HARDWARE_DEVICE_TAG;
    stdev->device.common.version = SOUND_TRIGGER_DEVICE_API_VERSION_1_3;
    stdev->device.common.module = (struct hw_module_t *) module;
    stdev->device.common.close = stdev_close;
    stdev->device.get_properties = stdev_get_properties;
    stdev->device.get_properties_extended = stdev_get_properties_extended;
    stdev->device.load_sound_model = stdev_load_sound_model;
    stdev->device.unload_sound_model = stdev_unload_sound_model;
    stdev->device.start_recognition = stdev_start_recognition;
    stdev->device.start_recognition_extended = stdev_start_recognition_extended;
    stdev->device.stop_recognition = stdev_stop_recognition;
    stdev->device.stop_all_recognitions = stdev_stop_all_recognitions;
    stdev->device.get_model_state = stdev_get_model_state;
    stdev->device.query_parameter = stdev_query_parameter;
    stdev->device.set_parameter = stdev_set_parameter;
    stdev->device.get_parameter = stdev_get_parameter;

    stdev->sound_trigger_handle = 0;
    pthread_mutex_init(&stdev->lock, (const pthread_mutexattr_t *) NULL);
    *device = &stdev->device.common;

    ALOGI("Starting control thread for the stub hal.");

    return 0;
}

static struct hw_module_methods_t sound_trigger_hal_module_methods = {
    .open = stdev_open,
};

struct sound_trigger_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = SOUND_TRIGGER_MODULE_API_VERSION_1_0,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = SOUND_TRIGGER_HARDWARE_MODULE_ID,
        .name = "Amlogic sound trigger HAL",
        .author = "Amlogic audio",
        .methods = &sound_trigger_hal_module_methods,
    },
};