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

#define LOG_TAG  "audio_hw_hal_bt"
//#define LOG_NDEBUG 0
#include <system/audio.h>
#include <cinttypes>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <shared_mutex>

#include <android-base/strings.h>
#include <audio_utils/primitives.h>


#include "audio_bt_hal.h"
#include "audio_bt_hw.h"
#include "aml_audio_resampler.h"


extern "C" {
#include "audio_hw_utils.h"
#include "aml_audio_stream.h"
#include "aml_audio_timer.h"
}
using namespace std;
using android::bluetooth::audio::aidl::BluetoothAudioPortAidlOut;

#define A2DP_RING_BUFFER_DELAY_TIME_MS              (64)
#define A2DP_SEND_DATA_TIMEOUT_RESET_MS             (300)
#define A2DP_WAIT_STATE_DELAY_TIME_US               (5000)
#define A2DP_WRITE_DATE_TIME_OUT_MS                 (64)
#define A2DP_LATENCY_INVALID_NS                     (NSEC_PER_SEC)
#define DEFAULT_A2DP_LATENCY_NS                     (100 * NSEC_PER_MSEC) // Default delay to use when BT device does not report a delay
#define A2DP_STATIC_DELAY_MS                        (0) // Additional device-specific delay
#define A2DP_TEST_AUDIO_FILE_PATH                  "/data/a2dp_test.wav"
#define A2DP_TEST_AUDIO_FILE_PROP                  "vendor.media.audiohal.a2dp.test"
#define A2DP_TEST_AUDIO_CHECK_MUTE_PROP            "vendor.media.audiohal.a2dp.checkmute"


struct aml_a2dp_hal {
    BluetoothAudioPortAidlOut a2dphw;
    audio_config config;
    aml_audio_resample_t *resample;
    uint64_t last_write_time_us;
    char * buff_conv_format;
    size_t buff_size_conv_format;
    BluetoothStreamState state;
    uint64_t a2dp_latency;
    bool is_sending_data;
    bool exit_out_monitor_thread;
    pthread_t out_monitor_thread_id;
    pthread_mutex_t out_monitor_thread_mutex;
    pthread_cond_t out_monitor_thread_cond;
};

static shared_mutex g_a2dp_hal_lock;
static shared_mutex g_a2dp_state_lock;
static int a2dp_out_standby(struct aml_audio_device *adev);
static void a2dp_notify_monitor(aml_a2dp_hal *hal, bool is_sending);

std::unordered_map<std::string, std::string> ParseAudioParams(const std::string& params) {
    std::vector<std::string> segments = android::base::Split(params, ";");
    std::unordered_map<std::string, std::string> params_map;
    for (const auto& segment : segments) {
        if (segment.length() == 0) {
            continue;
        }
        std::vector<std::string> kv = android::base::Split(segment, "=");
        if (kv[0].empty()) {
            //AM_LOGD("Invalid audio parameter: ", segment.char());
            continue;
        }
        params_map[kv[0]] = (kv.size() > 1 ? kv[1] : "");
    }
    return params_map;
}

static bool a2dp_wait_status(const char *caller, struct aml_a2dp_hal *hal) {
    hal->state = hal->a2dphw.GetState();
    int retry = 0;
    // max timeout 1s.
    while (retry < 200) {
        if ((hal->state != BluetoothStreamState::STARTING) && (hal->state != BluetoothStreamState::SUSPENDING)) {
            if (retry > 0) {
                AM_LOGI("(%s) wait for state change to successd. waited: %d ms cur state:%s", caller,
                    retry * A2DP_WAIT_STATE_DELAY_TIME_US / 1000, streamState2String(hal->state));
            }
            return true;
        }
        usleep(A2DP_WAIT_STATE_DELAY_TIME_US);
        a2dp_notify_monitor(hal, false);
        retry++;
        // Warning log once every 200ms after timeout.
        if (retry % 40 == 0) {
            AM_LOGW("(%s) timeout: %d ms, cur state:%s, contine waiting >>>>>>", caller,
                retry * A2DP_WAIT_STATE_DELAY_TIME_US / 1000, streamState2String(hal->state));
        }
        hal->state = hal->a2dphw.GetState();
    }
    AM_LOGE("(%s) waiting for state change failed, timeout: %d ms, cur state:%s", caller,
        retry * A2DP_WAIT_STATE_DELAY_TIME_US / 1000, streamState2String(hal->state));
    return false;
}

static void dump_a2dp_output_data(aml_a2dp_hal *hal, const void *buffer, size_t size) {
    if (get_debug_value(AML_DUMP_AUDIOHAL_A2DP)) {
        char acFilePathStr[ENUM_TYPE_STR_MAX_LEN];
        size_t out_per_sample_byte = audio_bytes_per_sample(hal->config.format);
        size_t out_channel_byte = audio_channel_count_from_out_mask(hal->config.channel_mask);
        sprintf(acFilePathStr, "/data/audio/a2dp_%d_%zuC_%zuB.pcm", hal->config.sample_rate, out_channel_byte, out_per_sample_byte);
        aml_dump_audio_bitstreams(acFilePathStr, buffer, size);
    }
}

static void a2dp_notify_monitor(aml_a2dp_hal *hal, bool is_sending = false) {
    pthread_mutex_lock(&hal->out_monitor_thread_mutex);
    hal->is_sending_data = is_sending;
    pthread_cond_signal(&hal->out_monitor_thread_cond);
    pthread_mutex_unlock(&hal->out_monitor_thread_mutex);
}

static void *a2dp_out_monitor_thread(void *arg) {
    struct aml_audio_device *adev = (struct aml_audio_device *)arg;
    aml_a2dp_hal *hal = (struct aml_a2dp_hal *)adev->a2dp_hal;
    struct timespec next_time;
    uint32_t timeout_ms = 0;
    bool is_standby = true;
    int ret = 0;
    AM_LOGI("Start monitoring the write rate+++");
    uint64_t time_ns;
    while (hal->exit_out_monitor_thread == false) {
        pthread_mutex_lock(&hal->out_monitor_thread_mutex);
        if (is_standby) {
            ret = pthread_cond_wait(&hal->out_monitor_thread_cond, &hal->out_monitor_thread_mutex);
        } else {
            timeout_ms = A2DP_RING_BUFFER_DELAY_TIME_MS; // 64 ms
            if (hal->is_sending_data) {
                timeout_ms = A2DP_SEND_DATA_TIMEOUT_RESET_MS; // 300ms
            }
            /* Here is an empirical value 64ms, when each write interval is greater than this value, we think standby BT,
             * needed to reduce power consumption.
             */
            time_ns = aml_audio_get_systime_ns();
            next_time = aml_audio_ns_to_time(time_ns + timeout_ms * NSEC_PER_MSEC);
            ret = pthread_cond_timedwait(&hal->out_monitor_thread_cond, &hal->out_monitor_thread_mutex, &next_time);
        }

        if (timeout_ms == A2DP_SEND_DATA_TIMEOUT_RESET_MS) {
            AM_LOGV("send bt elapsed time: %" PRIu64 " ms", (aml_audio_get_systime_ns() - time_ns) / NSEC_PER_MSEC);
        }
        pthread_mutex_unlock(&hal->out_monitor_thread_mutex);
        if (ret == ETIMEDOUT && hal->exit_out_monitor_thread == false) {
            if (timeout_ms == A2DP_SEND_DATA_TIMEOUT_RESET_MS) {
                AM_LOGW("send BT stack timeout %dms, need standby, cur_state:%s", timeout_ms, streamState2String(hal->state));
            } else {
                AM_LOGI("audio write timeout %dms, need standby, cur_state:%s", timeout_ms, streamState2String(hal->state));
            }
            a2dp_out_standby(adev);
            is_standby = true;
        } else {
            is_standby = false;
        }
    }
    AM_LOGI("Exit the monitor---");
    return NULL;
}

int a2dp_out_open(struct aml_audio_device *adev) {
    struct aml_a2dp_hal *hal = NULL;
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 0};
    unique_lock<shared_mutex> l(g_a2dp_hal_lock);

    if (adev->a2dp_hal != NULL) {
        AM_LOGW("already open");
        return 0;
    }
    hal = new aml_a2dp_hal;
    if (hal == NULL) {
        AM_LOGE("new BluetoothAudioPortAidlOut fail");
        return -1;
    }
    hal->resample = NULL;
    hal->buff_conv_format = NULL;
    hal->buff_size_conv_format = 0;
    hal->state = BluetoothStreamState::UNKNOWN;
    hal->a2dp_latency = A2DP_LATENCY_INVALID_NS;
    if (!hal->a2dphw.SetUp(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP)) {
        AM_LOGE("BluetoothAudioPortAidlOut setup fail");
        delete hal;
        return -1;
    }
    if (!hal->a2dphw.LoadAudioConfig(&hal->config)) {
        AM_LOGE("LoadAudioConfig fail");
    }
    adev->a2dp_hal = (void*)hal;

    pthread_condattr_t condattr;
    pthread_mutex_init(&hal->out_monitor_thread_mutex, NULL);
    pthread_condattr_init(&condattr);
    pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
    pthread_cond_init(&hal->out_monitor_thread_cond, &condattr);
    pthread_condattr_destroy(&condattr);
    hal->exit_out_monitor_thread = false;
    hal->is_sending_data = false;
    int pthread_ret = pthread_create(&hal->out_monitor_thread_id, NULL, &a2dp_out_monitor_thread, adev);
    if (pthread_ret != 0) {
        AM_LOGE("pthread_create fail");
        return -1;
    }
    AM_LOGI("Rx param rate:%d, format:%s, ch:%d", hal->config.sample_rate,
        audioFormat2Str(hal->config.format), audio_channel_count_from_out_mask(hal->config.channel_mask));
    return 0;
}

int a2dp_out_close(struct aml_audio_device *adev) {
    unique_lock<shared_mutex> l(g_a2dp_hal_lock);
    struct aml_a2dp_hal *hal = (struct aml_a2dp_hal *)adev->a2dp_hal;
    if (hal == NULL) {
        AM_LOGW("a2dp hw is already closed.");
        return -1;
    }

    /*coverity[sleep]*/
    a2dp_wait_status(__func__, hal);
    hal->exit_out_monitor_thread = true;
    a2dp_notify_monitor(hal);
    pthread_join(hal->out_monitor_thread_id, NULL);
    pthread_cond_destroy(&hal->out_monitor_thread_cond);
    pthread_mutex_destroy(&hal->out_monitor_thread_mutex);
    adev->a2dp_hal = NULL;
    AM_LOGI("");
    hal->a2dphw.Stop();
    hal->a2dphw.TearDown();
    if (hal->resample) {
        aml_audio_resample_close(hal->resample);
        hal->resample = NULL;
    }
    if (hal->buff_conv_format) {
        aml_audio_free(hal->buff_conv_format);
    }
    delete hal;
    return 0;
}

static int a2dp_out_resume(struct aml_audio_device *adev) {
    struct aml_a2dp_hal *hal = (struct aml_a2dp_hal *)adev->a2dp_hal;
    if (hal == NULL) {
        AM_LOGW("a2dp has been released.");
        return -1;
    }

    AM_LOGI("start resume... cur status:%s", streamState2String(hal->state));
    a2dp_wait_status(__func__, hal);
    if (hal->state == BluetoothStreamState::STARTED) {
        AM_LOGI("A2dp already resumed. status:%s", streamState2String(hal->state));
        return 0;
    } else if (hal->state == BluetoothStreamState::STANDBY) {
        unique_lock<shared_mutex> l(g_a2dp_state_lock);
        if (hal->a2dphw.Start()) {
            BluetoothStreamState cur_status = hal->a2dphw.GetState();
            AM_LOGI("status: %s -> %s Resume %s", streamState2String(hal->state), streamState2String(cur_status),
                (cur_status == BluetoothStreamState::STARTED) ? "success." : "in progress...");
            hal->state = cur_status;
            return 0;
        } else {
            AM_LOGW("Start fail. state:%s", streamState2String(hal->a2dphw.GetState()));
            return -1;
        }
    } else {
        AM_LOGW("cur state:%s error, can't resume", streamState2String(hal->state));
        return -1;
    }
}

static int a2dp_out_standby(struct aml_audio_device *adev) {
    struct aml_a2dp_hal *hal = (struct aml_a2dp_hal *)adev->a2dp_hal;
    int32_t ret = 0;
    if (hal == NULL) {
        AM_LOGW("a2dp has been released.");
        return -1;
    }

    AM_LOGI("start standby... cur status:%s", streamState2String(hal->state));
    a2dp_wait_status(__func__, hal);
    if (hal->state == BluetoothStreamState::STANDBY) {
        AM_LOGI("A2dp already standby. status:%s", streamState2String(hal->state));
    } else if (hal->state == BluetoothStreamState::STARTED) {
        unique_lock<shared_mutex> l(g_a2dp_state_lock);
        if (hal->a2dphw.Suspend()) {
            BluetoothStreamState cur_status = hal->a2dphw.GetState();
            AM_LOGI("status: %s -> %s Standby %s", streamState2String(hal->state), streamState2String(cur_status),
                (cur_status == BluetoothStreamState::STANDBY) ? "success." : "in progress...");
            hal->state = cur_status;
        } else {
            AM_LOGW("Suspend fail. state:%s", streamState2String(hal->a2dphw.GetState()));
            ret = -1;
        }
    } else {
        AM_LOGW("cur state:%s error, can't standby", streamState2String(hal->state));
        ret = -1;
    }
    return ret;
}

static bool a2dp_state_process(struct aml_audio_device *adev, audio_config_base_t *config, size_t in_frames) {
    aml_a2dp_hal            *hal = (struct aml_a2dp_hal *)adev->a2dp_hal;
    BluetoothStreamState    cur_state = hal->a2dphw.GetState();
    static uint64_t         frame_write_sum = 0;
    static uint64_t         write_start_time_us = 0;
    const uint64_t          cur_write_time_us = aml_audio_get_systime();
    bool                    prepared = false;

    const int64_t write_delta_time_us = cur_write_time_us - hal->last_write_time_us;
    int64_t data_delta_time_us = (int64_t)in_frames * USEC_PER_SEC / config->sample_rate - write_delta_time_us;
    hal->last_write_time_us = cur_write_time_us;

    auto update_presentation_position = [&]() {
        frame_write_sum = 0;
        write_start_time_us = cur_write_time_us;
        ALOGI("[a2dp_state_process:%d] starting success, start sending data to bt stack----------->", __LINE__);
    };
    if (hal->state != cur_state) {
        AM_LOGI("a2dp state changed: %s -> %s",  streamState2String(hal->state), streamState2String(cur_state));
        if (cur_state == BluetoothStreamState::STARTED) {
            update_presentation_position();
        }
        hal->state = cur_state;
    }

    if (adev->debug_flag) {
        const int64_t frame_write_sum_time_ms = (frame_write_sum * MSEC_PER_SEC) / config->sample_rate;
        const int64_t write_data_jitter_ms = frame_write_sum_time_ms - (cur_write_time_us - write_start_time_us) / USEC_PER_MSEC;
        AM_LOGD("frames:%zu interval:%" PRId64 " ms jitter:%" PRId64 " ms count:%" PRIu64 " ms(%" PRIu64 ") ",
            in_frames, write_delta_time_us / USEC_PER_MSEC, write_data_jitter_ms, frame_write_sum_time_ms, frame_write_sum);
    }

    if (cur_state == BluetoothStreamState::STARTING) {
        AM_LOGI("blocking the write thread %" PRId64 " ms, cur a2dp state is %s",
            data_delta_time_us / USEC_PER_MSEC, streamState2String(cur_state));
        if (data_delta_time_us > 0) {
            //After every sleeping 2ms, monitor the state of BT_stack
            while (data_delta_time_us > 0) {
                usleep((data_delta_time_us < 2000) ?  data_delta_time_us : 2000);
                hal->state = hal->a2dphw.GetState();
                if (hal->state == BluetoothStreamState::STARTED) {
                    AM_LOGI("end for wait state changed: %s -> %s",  streamState2String(cur_state), streamState2String(hal->state));
                    update_presentation_position();
                    frame_write_sum += in_frames;
                    return true;
                }
                data_delta_time_us -= 2000;
            }
        }
    } else if (cur_state == BluetoothStreamState::STARTED) {
        frame_write_sum += in_frames;
        prepared = true;
    } else if (cur_state == BluetoothStreamState::DISABLED) {
        // TODO: A2DP is disconnected. do nothing.
    } else {
        a2dp_out_resume(adev);
        hal->last_write_time_us = aml_audio_get_systime();
    }
    return prepared;
}

static ssize_t a2dp_data_out_process(aml_a2dp_hal *hal, audio_config_base_t *config, const void *buffer, size_t bytes) {
    audio_channel_mask_t in_channel_mask = config->channel_mask;
    audio_channel_mask_t out_channel_mask = hal->config.channel_mask;
    size_t in_channels = audio_channel_count_from_out_mask(config->channel_mask);
    size_t out_channels = audio_channel_count_from_out_mask(hal->config.channel_mask);
    audio_format_t in_format = config->format;
    audio_format_t out_format = hal->config.format;
    size_t in_sample_bytes = audio_bytes_per_sample(config->format);
    size_t out_sample_bytes = audio_bytes_per_sample(hal->config.format);
    size_t in_frames = bytes / (in_sample_bytes * in_channels);
    size_t out_bytes = in_frames * out_sample_bytes * out_channels;
    int realloc_ret = 0;
    ssize_t realloc_size = out_bytes;

    if (bytes > out_bytes) {
        realloc_size = bytes;
    }
    realloc_ret = aml_audio_check_and_realloc((void **)&hal->buff_conv_format, &hal->buff_size_conv_format, realloc_size);
    R_CHECK_RET(realloc_ret, "alloc memory size:%zu fail", realloc_size);
    // only support 32bit->16bit 24bit, 16bit->32bit 24bit.
    if (in_format != out_format) {
        if (in_format == AUDIO_FORMAT_PCM_16_BIT) {
            if (out_format == AUDIO_FORMAT_PCM_32_BIT) {
                memcpy_to_i32_from_i16((int32_t *)hal->buff_conv_format, (int16_t *)buffer, in_frames * in_channels);
            } else if (out_format == AUDIO_FORMAT_PCM_24_BIT_PACKED) {
                memcpy_to_p24_from_i16((uint8_t *)hal->buff_conv_format, (int16_t *)buffer, in_frames * in_channels);
            } else {
                AM_LOGW("not support out_format:%#x", out_format);
                return out_bytes;
            }
        } else if (in_format == AUDIO_FORMAT_PCM_32_BIT) {
            if (out_format == AUDIO_FORMAT_PCM_16_BIT) {
                memcpy_to_i16_from_i32((int16_t *)hal->buff_conv_format, (int32_t *)buffer, in_frames * in_channels);
            } else if (out_format == AUDIO_FORMAT_PCM_24_BIT_PACKED) {
                memcpy_to_p24_from_i32((uint8_t *)hal->buff_conv_format, (int32_t *)buffer, in_frames * in_channels);
            } else {
                AM_LOGW("not support out_format:%#x", out_format);
                return out_bytes;
            }
        } else {
            AM_LOGW("not support in_format:%#x", in_format);
            return out_bytes;
        }
    } else {
        memcpy(hal->buff_conv_format, buffer, bytes);
    }

    // only support 2 channel -> 1 channel.
    if (in_channel_mask != out_channel_mask) {
        if (in_channel_mask != AUDIO_CHANNEL_OUT_STEREO) {
            AM_LOGW("not support in_channel_mask:%#x", in_channel_mask);
            return out_bytes;
        }
        if (out_channel_mask == AUDIO_CHANNEL_OUT_MONO) {
            if (out_format == AUDIO_FORMAT_PCM_16_BIT) {
                downmix_to_mono_i16_from_stereo_i16((int16_t*)hal->buff_conv_format, (int16_t*)hal->buff_conv_format, in_frames);
            } else if (out_format == AUDIO_FORMAT_PCM_32_BIT) {
                auto downmix_to_mono_i32_from_stereo_i32 = [](int32_t *dst, const int32_t *src, size_t count) {
                    for (; count > 0; --count) {
                        *dst++ = (int32_t)(((int64_t)src[0] + (int64_t)src[1]) >> 1);
                        src += 2;
                    }
                };
                downmix_to_mono_i32_from_stereo_i32((int32_t*)hal->buff_conv_format, (int32_t*)hal->buff_conv_format, in_frames);
            } else {
                AM_LOGW("not support out_format:%#x for down mix from stereo to mono.", out_format);
            }
        } else {
            AM_LOGW("not support out_channel_mask:%#x", out_channel_mask);
        }
    }
    return out_bytes;
}

static ssize_t a2dp_data_resample_process(aml_a2dp_hal *hal, audio_config_base_t *input_cfg,
    const void *buffer, size_t in_frames, const void **output_buffer) {
    size_t in_frame_size = audio_channel_count_from_out_mask(input_cfg->channel_mask) * audio_bytes_per_sample(input_cfg->format);
    ssize_t out_bytes = in_frames * in_frame_size;
    *output_buffer = buffer;
    if (input_cfg->sample_rate != hal->config.sample_rate) {
        if (hal->resample == NULL || hal->resample->resample_config.input_sr != input_cfg->sample_rate) {
            audio_resample_config_t resample_cfg;
            resample_cfg.aformat   = input_cfg->format;
            resample_cfg.channels  = audio_channel_count_from_out_mask(input_cfg->channel_mask);
            resample_cfg.input_sr  = input_cfg->sample_rate;
            resample_cfg.output_sr = hal->config.sample_rate;
            if (hal->resample == NULL) {
                AM_LOGI("resample init, rate: %d -> %d, format:%s, ch:%d", resample_cfg.input_sr, resample_cfg.output_sr,
                    audioFormat2Str((audio_format_t)resample_cfg.aformat), resample_cfg.channels);
                int ret = aml_audio_resample_init(&hal->resample, AML_AUDIO_SIMPLE_RESAMPLE, &resample_cfg);
                R_CHECK_RET(ret, "Resampler is failed initialization !!!");
            } else {
                AM_LOGI("resample reconfig, input_sr changed %d -> %d", hal->resample->resample_config.input_sr, input_cfg->sample_rate);
                memcpy(&hal->resample->resample_config, &resample_cfg, sizeof(audio_resample_config_t));
                aml_audio_resample_reset(hal->resample);
            }
        }
        aml_audio_resample_process(hal->resample, (void *)buffer, in_frames * in_frame_size);
        if (in_frame_size > 0) {
            out_bytes = hal->resample->resample_size;
        }
        *output_buffer = hal->resample->resample_buffer;
    }
    return out_bytes;
}

static ssize_t a2dp_out_write_l(struct aml_audio_device *adev, audio_config_base_t *config, const void* buffer, size_t bytes) {
    aml_a2dp_hal *hal = (struct aml_a2dp_hal *)adev->a2dp_hal;
    static long int test_file_position = 0;
    const void *wr_buff = NULL;
    size_t bytes_written = 0;
    size_t sent = 0;
    size_t in_channels = audio_channel_count_from_out_mask(config->channel_mask);
    size_t in_sample_bytes = audio_bytes_per_sample(config->format);
    size_t in_frames = bytes / (in_sample_bytes * in_channels);

    if (adev->a2dp_hal == NULL) {
        if (adev->debug_flag) {
            AM_LOGW("a2dp_hal is null pointer");
        }
        return bytes;
    }

    if (!a2dp_state_process(adev, config, in_frames)) {
        a2dp_notify_monitor(hal);
        return bytes;
    }

    // For debug a2dp data.
    aml_audio_read_audio_data_by_file( A2DP_TEST_AUDIO_FILE_PATH, A2DP_TEST_AUDIO_FILE_PROP, (char *)buffer, bytes, &test_file_position);

    ssize_t resample_bytes = a2dp_data_resample_process(hal, config, buffer, in_frames, &wr_buff);
    if (resample_bytes < 0) {
        return bytes;
    }

    ssize_t out_bytes = a2dp_data_out_process(hal, config, wr_buff, resample_bytes);
    if (out_bytes < 0) {
        return bytes;
    }

    dump_a2dp_output_data(hal, hal->buff_conv_format, out_bytes);
    uint64_t write_enter_time_us = aml_audio_get_systime();
    while (bytes_written < out_bytes) {
        size_t need_write = out_bytes - bytes_written;
        if (getprop_bool(A2DP_TEST_AUDIO_CHECK_MUTE_PROP)) {
            check_audio_level("a2dp_check", (char *)hal->buff_conv_format + bytes_written, need_write);
        }
        a2dp_notify_monitor(hal, true);
        uint64_t write_start_time_us = aml_audio_get_systime();
        sent = hal->a2dphw.WriteData((char *)hal->buff_conv_format + bytes_written, need_write);
        uint64_t write_stop_time_us = aml_audio_get_systime();
        uint64_t write_data_time_ms = (write_stop_time_us - write_start_time_us) / USEC_PER_MSEC;
        if ((adev->debug_flag && write_data_time_ms > 1) || write_data_time_ms > 40) {
            /* Debug the time of write data to policy and the time of the write_data */
            uint64_t data_time_ms = need_write / (hal->config.sample_rate * audio_bytes_per_sample(hal->config.format)
                * audio_channel_count_from_out_mask(hal->config.channel_mask) / MSEC_PER_SEC);
            AM_LOGD("write:%zu sent:%zu total:%zu write_time: %" PRIu64 " ms data_time: %" PRIu64 " ms",
                need_write, sent, out_bytes, write_data_time_ms, data_time_ms);
        }
        a2dp_notify_monitor(hal);
        bytes_written += sent;
        /* The cache of BT stack is about 40ms data, and exit from writing data
         * after timeout of 64ms here. */
        if (bytes_written < out_bytes &&
            (write_stop_time_us - write_enter_time_us) > A2DP_WRITE_DATE_TIME_OUT_MS * USEC_PER_MSEC) {
            AM_LOGW("WriteData timeout: %" PRIu64 " ms, quit now.", (write_stop_time_us - write_enter_time_us) / USEC_PER_MSEC);
            break;
        }
    }
    return bytes;
}

ssize_t a2dp_out_write(struct aml_audio_device *adev, audio_config_base_t *config, const void* buffer, size_t bytes) {
    size_t in_frame_size = audio_channel_count_from_out_mask(config->channel_mask) * audio_bytes_per_sample(config->format);
    uint32_t one_ms_data = in_frame_size * config->sample_rate / MSEC_PER_SEC;
    uint32_t date_len_ms = bytes / one_ms_data;
    const uint32_t period_time_ms = 32;
    const uint32_t period_time_size = one_ms_data * period_time_ms;

    if (bytes == 0) {
        AM_LOGW("bytes is 0");
        return -1;
    }
    R_CHECK_POINTER_LEGAL(-1, config, "");
    R_CHECK_POINTER_LEGAL(-1, buffer, "");

    uint32_t written_size = 0;
    shared_lock<shared_mutex> l(g_a2dp_hal_lock);
    while (bytes > written_size) {
        uint32_t remain_size = bytes - written_size;
        size_t sent = remain_size;
        if (remain_size > period_time_ms * one_ms_data) {
            sent = period_time_size;
        }
        /*coverity[sleep]*/
        a2dp_out_write_l(adev, config, (char *)buffer + written_size, sent);
        AM_LOGV("written_size:%d, remain_size:%d, sent:%zu", written_size, remain_size, sent);
        written_size += sent;
    }
    return written_size;
}

uint32_t a2dp_out_get_latency(struct aml_audio_device *adev) {
    uint64_t remote_delay_report_ns = 0;
    shared_lock<shared_mutex> l(g_a2dp_hal_lock);
    struct aml_a2dp_hal * hal = (struct aml_a2dp_hal *)adev->a2dp_hal;
    if (!hal) {
        return DEFAULT_A2DP_LATENCY_NS / NSEC_PER_MSEC;
    }
    /* Some BT devices(eg: Xiaomi Air2) will change the latency after the connection is successful,
     * causing Youtube playback fail. */
    if (hal->a2dp_latency == A2DP_LATENCY_INVALID_NS) {
        uint64_t absorbed_bytes = 0;
        struct timespec absorbed_timestamp = {};
        bool success = hal->a2dphw.GetPresentationPosition(&remote_delay_report_ns, &absorbed_bytes, &absorbed_timestamp);
        if (!success || remote_delay_report_ns == 0) {
            hal->a2dp_latency = DEFAULT_A2DP_LATENCY_NS;
        } else {
            hal->a2dp_latency = remote_delay_report_ns;
        }
        AM_LOGI("success:%d report_latency:%" PRIu64" ms, latency:%" PRIu64" ms", success,
            remote_delay_report_ns / NSEC_PER_MSEC, hal->a2dp_latency / NSEC_PER_MSEC);
    }
    remote_delay_report_ns = hal->a2dp_latency;
    return static_cast<uint32_t>(remote_delay_report_ns / NSEC_PER_MSEC + A2DP_STATIC_DELAY_MS);
}

int a2dp_out_get_status(struct aml_audio_device *adev) {
    struct aml_a2dp_hal * hal = (struct aml_a2dp_hal *)adev->a2dp_hal;
    if (!hal) {
        AM_LOGW("a2dp_hal is null");
        return -1;
    }
    return (int)hal->state;
}

int a2dp_out_set_parameters(struct aml_audio_device *adev, const char *kvpairs) {
    struct aml_a2dp_hal * hal = (struct aml_a2dp_hal *)adev->a2dp_hal;
    R_CHECK_POINTER_LEGAL(-1, hal, "a2dp hw is released");

    std::unordered_map<std::string, std::string> params = ParseAudioParams(kvpairs);
    if (params.empty())
        return 0;

    if (params.find("A2dpSuspended") != params.end()) {
        if (params["A2dpSuspended"] == "true") {
            if (hal->a2dphw.GetState() != BluetoothStreamState::DISABLED)
                hal->a2dphw.Stop();
        } else {
            if (hal->a2dphw.GetState() == BluetoothStreamState::DISABLED)
                hal->a2dphw.SetState(BluetoothStreamState::STANDBY);
        }
    }
    if (params["closing"] == "true") {
        if (hal->a2dphw.GetState() != BluetoothStreamState::DISABLED)
            hal->a2dphw.Stop();
    }
    return 0;
}

int a2dp_hal_dump(struct aml_audio_device *adev, int fd) {
    shared_lock<shared_mutex> l(g_a2dp_hal_lock);
    struct aml_a2dp_hal *hal = (struct aml_a2dp_hal *)adev->a2dp_hal;
    if (hal) {
        dprintf(fd, "------------ [AM_HAL][A2DP] -------------------------------------\n");
        dprintf(fd, "-[AML_HAL]      out_rate      : %10d     | out_ch    :%10d\n", hal->config.sample_rate, audio_channel_count_from_out_mask(hal->config.channel_mask));
        dprintf(fd, "-[AML_HAL]      out_format    : %10s     | cur_state :%10s\n", audioFormat2Str(hal->config.format), streamState2String(hal->a2dphw.GetState()));
        /*coverity[missing_lock]*/
        dprintf(fd, "-[AML_HAL]      a2dp_latency  : %" PRIu64" ms\n", hal->a2dp_latency / NSEC_PER_MSEC);
        aml_audio_resample_t *resample = hal->resample;
        if (resample) {
            audio_resample_config_t *config = &resample->resample_config;
            dprintf(fd, "-[AML_HAL] resample in_sr     : %10d     | out_sr    :%10d\n", config->input_sr, config->output_sr);
            dprintf(fd, "-[AML_HAL] resample ch        : %10d     | type      :%10d\n", config->channels, resample->resample_type);
        }
    }
    return 0;
}


