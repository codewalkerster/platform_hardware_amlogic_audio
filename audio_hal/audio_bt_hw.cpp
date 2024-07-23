/*
 * Copyright 2024 The Android Open Source Project
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

#define LOG_TAG "audio_hw_hal_bt"

#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <audio_utils/primitives.h>
#include <inttypes.h>
#include <log/log.h>
#include <stdlib.h>

#include "BluetoothAudioSessionControl.h"
#include "audio_bt_hw.h"

extern "C" {
#include "audio_hw_utils.h"
#include "audio_hal_debug.h"
}

const char* streamState2String(BluetoothStreamState type)
{
    ENUM_TYPE_TO_STR_START("BluetoothStreamState::");
    ENUM_TYPE_TO_STR(BluetoothStreamState::DISABLED)
    ENUM_TYPE_TO_STR(BluetoothStreamState::STANDBY)
    ENUM_TYPE_TO_STR(BluetoothStreamState::STARTING)
    ENUM_TYPE_TO_STR(BluetoothStreamState::STARTED)
    ENUM_TYPE_TO_STR(BluetoothStreamState::SUSPENDING)
    ENUM_TYPE_TO_STR(BluetoothStreamState::UNKNOWN)
    ENUM_TYPE_TO_STR_END
}

using ::aidl::android::hardware::bluetooth::audio::SessionType;
const char* sessionType2String(SessionType type)
{
    switch (type) {
    case SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH:
        return "A2DP_OUT";
    case SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
        return "A2DP_OFFLOAD_OUT";
    case SessionType::A2DP_SOFTWARE_DECODING_DATAPATH:
        return "A2DP_IN";
    case SessionType::A2DP_HARDWARE_OFFLOAD_DECODING_DATAPATH:
        return "A2DP_OFFLOAD_IN";
    case SessionType::HEARING_AID_SOFTWARE_ENCODING_DATAPATH:
        return "HEARING_AID_OUT";
    case SessionType::LE_AUDIO_SOFTWARE_ENCODING_DATAPATH:
        return "LE_AUDIO_OUT";
    case SessionType::LE_AUDIO_SOFTWARE_DECODING_DATAPATH:
        return "LE_AUDIO_IN";
    case SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
        return "LE_AUDIO_OFFLOAD_OUT";
    case SessionType::LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH:
        return "LE_AUDIO_OFFLOAD_IN";
    case SessionType::LE_AUDIO_BROADCAST_SOFTWARE_ENCODING_DATAPATH:
        return "LE_AUDIO_BROADCAST_OUT";
    case SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
        return "LE_AUDIO_BROADCAST_OFFLOAD_OUT";
    case SessionType::UNKNOWN:
        return "UNKNOWN";
    default:
        return "INVALID TYPE";
    }
}

// code transplant from system/bt/audio_bluetooth_hw/device_port_proxy.cc
namespace android {
namespace bluetooth {
namespace audio {
namespace aidl {
using ::aidl::android::hardware::bluetooth::audio::AudioConfiguration;
using ::aidl::android::hardware::bluetooth::audio::BluetoothAudioSessionControl;
using ::aidl::android::hardware::bluetooth::audio::ChannelMode;
using ::aidl::android::hardware::bluetooth::audio::PcmConfiguration;
using ::aidl::android::hardware::bluetooth::audio::PortStatusCallbacks;
using ::aidl::android::hardware::bluetooth::audio::PresentationPosition;


using ::android::base::StringPrintf;
using ControlResultCallback = std::function<void(
    uint16_t cookie, bool start_resp, const BluetoothAudioStatus& status)>;
using SessionChangedCallback = std::function<void(uint16_t cookie)>;

namespace {

audio_channel_mask_t OutputChannelModeToAudioFormat(ChannelMode channel_mode) {
  switch (channel_mode) {
    case ChannelMode::MONO:
      return AUDIO_CHANNEL_OUT_MONO;
    case ChannelMode::STEREO:
      return AUDIO_CHANNEL_OUT_STEREO;
    default:
      return kBluetoothDefaultOutputChannelModeMask;
  }
}

audio_channel_mask_t InputChannelModeToAudioFormat(ChannelMode channel_mode) {
  switch (channel_mode) {
    case ChannelMode::MONO:
      return AUDIO_CHANNEL_IN_MONO;
    case ChannelMode::STEREO:
      return AUDIO_CHANNEL_IN_STEREO;
    default:
      return kBluetoothDefaultInputChannelModeMask;
  }
}

audio_format_t BitsPerSampleToAudioFormat(uint8_t bits_per_sample,
                                          const SessionType& session_type) {
  switch (bits_per_sample) {
    case 16:
      return AUDIO_FORMAT_PCM_16_BIT;
    case 24:
      /* Now we use knowledge that Classic sessions used packed, and LE Audio
       * LC3 encoder uses unpacked as input. This should be passed as parameter
       * from BT stack through AIDL, but it would require new interface version,
       * so sticking with this workaround for now. */
      if (session_type ==
              SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH ||
          session_type == SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH) {
        return AUDIO_FORMAT_PCM_24_BIT_PACKED;
      } else {
        return AUDIO_FORMAT_PCM_8_24_BIT;
      }
    case 32:
      return AUDIO_FORMAT_PCM_32_BIT;
    default:
      return kBluetoothDefaultAudioFormatBitsPerSample;
  }
}

// The maximum time to wait in std::condition_variable::wait_for()
constexpr unsigned int kMaxWaitingTimeMs = 4500;

}  // namespace

BluetoothAudioPortAidl::BluetoothAudioPortAidl()
    : cookie_(::aidl::android::hardware::bluetooth::audio::
                    kObserversCookieUndefined),
      state_(BluetoothStreamState::DISABLED),
      session_type_(SessionType::UNKNOWN) {}

BluetoothAudioPortAidlOut::~BluetoothAudioPortAidlOut() {
  if (in_use()) TearDown();
}

BluetoothAudioPortAidlIn::~BluetoothAudioPortAidlIn() {
  if (in_use()) TearDown();
}

bool BluetoothAudioPortAidl::SetUp(audio_devices_t devices) {
  if (!init_session_type(devices)) return false;

  state_ = BluetoothStreamState::STANDBY;

  auto control_result_cb = [port = this](uint16_t cookie, bool start_resp __unused,
                                         const BluetoothAudioStatus& status) {
    if (!port->in_use()) {
      AM_LOGE("control_result_cb: BluetoothAudioPortAidl is not in use");
      return;
    }
    if (port->cookie_ != cookie) {
      AM_LOGE("control_result_cb: proxy of device port (cookie=%#x) is corrupted", cookie);
      return;
    }
    port->ControlResultHandler(status);
  };
  auto session_changed_cb = [port = this](uint16_t cookie) {
    if (!port->in_use()) {
      AM_LOGE("session_changed_cb: BluetoothAudioPortAidl is not in use");
      return;
    }
    if (port->cookie_ != cookie) {
      AM_LOGE("session_changed_cb: proxy of device port (cookie=%#x) is corrupted", cookie);
      return;
    }
    port->SessionChangedHandler();
  };
  // TODO: Add audio_config_changed_cb
  PortStatusCallbacks cbacks = {
      .control_result_cb_ = control_result_cb,
      .session_changed_cb_ = session_changed_cb,
  };
  cookie_ = BluetoothAudioSessionControl::RegisterControlResultCback(
      session_type_, cbacks);
  AM_LOGI("session_type=%s, cookie=%#x", sessionType2String(session_type_), cookie_);
  return (
      cookie_ != ::aidl::android::hardware::bluetooth::audio::kObserversCookieUndefined);
}

bool BluetoothAudioPortAidl::init_session_type(audio_devices_t device) {
  switch (device) {
    case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP:
    case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES:
    case AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER:
      session_type_ = SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH;
      break;
    case AUDIO_DEVICE_OUT_HEARING_AID:
      session_type_ = SessionType::HEARING_AID_SOFTWARE_ENCODING_DATAPATH;
      break;
    case AUDIO_DEVICE_OUT_BLE_HEADSET:
      session_type_ = SessionType::LE_AUDIO_SOFTWARE_ENCODING_DATAPATH;
      break;
    case AUDIO_DEVICE_OUT_BLE_SPEAKER:
      session_type_ = SessionType::LE_AUDIO_SOFTWARE_ENCODING_DATAPATH;
      break;
    case AUDIO_DEVICE_IN_BLE_HEADSET:
      session_type_ = SessionType::LE_AUDIO_SOFTWARE_DECODING_DATAPATH;
      break;
    case AUDIO_DEVICE_OUT_BLE_BROADCAST:
      session_type_ =
          SessionType::LE_AUDIO_BROADCAST_SOFTWARE_ENCODING_DATAPATH;
      break;
    default:
      AM_LOGE("unknown device=device:%s(%#x)", audioDevType2Str(device), device);
      return false;
  }
  AM_LOGI("device:%s(%#x), session:%s", audioDevType2Str(device), device, sessionType2String(session_type_));

  if (!BluetoothAudioSessionControl::IsSessionReady(session_type_)) {
    AM_LOGE("device:%s(%#x), session:%s is not ready", audioDevType2Str(device),
        device, sessionType2String(session_type_));
    return false;
  }
  return true;
}

void BluetoothAudioPortAidl::TearDown() {
  if (!in_use()) {
    AM_LOGE("session:%s, cookie=%#x unknown monitor", sessionType2String(session_type_), cookie_);
    return;
  }

  AM_LOGI("session:%s, cookie=%#x", sessionType2String(session_type_), cookie_);
  BluetoothAudioSessionControl::UnregisterControlResultCback(session_type_,
                                                             cookie_);
  cookie_ =
    ::aidl::android::hardware::bluetooth::audio::kObserversCookieUndefined;
}

void BluetoothAudioPortAidl::ControlResultHandler(
    const BluetoothAudioStatus& status) {
  if (!in_use()) {
    AM_LOGE("BluetoothAudioPortAidlis not in use");
    return;
  }
  std::unique_lock<std::mutex> port_lock(cv_mutex_);
  BluetoothStreamState previous_state = state_;

  switch (previous_state) {
    case BluetoothStreamState::STARTED:
      /* Only Suspend signal can be send in STARTED state*/
      if (status == BluetoothAudioStatus::RECONFIGURATION ||
          status == BluetoothAudioStatus::SUCCESS) {
        state_ = BluetoothStreamState::STANDBY;
      } else {
        // Set to standby since the stack may be busy switching between outputs
      }
      break;
    case BluetoothStreamState::STARTING:
      if (status == BluetoothAudioStatus::SUCCESS) {
        state_ = BluetoothStreamState::STARTED;
      } else {
        // Set to standby since the stack may be busy switching between outputs
        state_ = BluetoothStreamState::STANDBY;
      }
      break;
    case BluetoothStreamState::SUSPENDING:
      if (status == BluetoothAudioStatus::SUCCESS) {
        state_ = BluetoothStreamState::STANDBY;
      } else {
        // It will be failed if the headset is disconnecting, and set to disable
        // to wait for re-init again
        state_ = BluetoothStreamState::DISABLED;
      }
      break;
    default:
      AM_LOGE("unexpected status=%s for session_type=%s, cookie=%#x, previous_state=%s",
          toString(status).c_str(), sessionType2String(session_type_), cookie_, streamState2String(previous_state));
      return;
  }
  if (status == BluetoothAudioStatus::SUCCESS ||
        (previous_state == BluetoothStreamState::STARTED && status == BluetoothAudioStatus::RECONFIGURATION)) {
      AM_LOGI("session:%s state: %s -> %s cookie=%#x",
        sessionType2String(session_type_), streamState2String(previous_state), streamState2String(state_), cookie_);
  } else {
      AM_LOGW("status:%s failure for session:%s, control changed: %s -> %s cookie=%#x", toString(status).c_str(),
        sessionType2String(session_type_), streamState2String(previous_state), streamState2String(state_), cookie_);
  }
  port_lock.unlock();
  internal_cv_.notify_all();
}

void BluetoothAudioPortAidl::SessionChangedHandler() {
  if (!in_use()) {
    AM_LOGE("BluetoothAudioPortAidlis not in use");
    return;
  }
  std::unique_lock<std::mutex> port_lock(cv_mutex_);
  BluetoothStreamState previous_state = state_;
  AM_LOGI("session_type:%s, cookie=%#x, previous_state=%s",
    sessionType2String(session_type_), cookie_, streamState2String(previous_state));
  state_ = BluetoothStreamState::DISABLED;
  port_lock.unlock();
  internal_cv_.notify_all();
}

bool BluetoothAudioPortAidl::in_use() const {
  return (
      cookie_ !=
      ::aidl::android::hardware::bluetooth::audio::kObserversCookieUndefined);
}

bool BluetoothAudioPortAidl::GetPreferredDataIntervalUs(
    size_t* interval_us) const {
  if (!in_use()) {
    return false;
  }

  const AudioConfiguration& hal_audio_cfg =
      BluetoothAudioSessionControl::GetAudioConfig(session_type_);
  if (hal_audio_cfg.getTag() != AudioConfiguration::pcmConfig) {
    return false;
  }

  const PcmConfiguration& pcm_cfg =
      hal_audio_cfg.get<AudioConfiguration::pcmConfig>();
  *interval_us = pcm_cfg.dataIntervalUs;
  return true;
}

bool BluetoothAudioPortAidlOut::LoadAudioConfig(
    audio_config_t* audio_cfg) const {
  if (!in_use()) {
    AM_LOGE("BluetoothAudioPortAidlOut not in use");
    audio_cfg->sample_rate = kBluetoothDefaultSampleRate;
    audio_cfg->channel_mask = kBluetoothDefaultOutputChannelModeMask;
    audio_cfg->format = kBluetoothDefaultAudioFormatBitsPerSample;
    return false;
  }

  const AudioConfiguration& hal_audio_cfg =
      BluetoothAudioSessionControl::GetAudioConfig(session_type_);
  if (hal_audio_cfg.getTag() != AudioConfiguration::pcmConfig) {
    audio_cfg->sample_rate = kBluetoothDefaultSampleRate;
    audio_cfg->channel_mask = kBluetoothDefaultOutputChannelModeMask;
    audio_cfg->format = kBluetoothDefaultAudioFormatBitsPerSample;
    return false;
  }
  const PcmConfiguration& pcm_cfg =
      hal_audio_cfg.get<AudioConfiguration::pcmConfig>();
  AM_LOGD("session_type:%s, cookie:%#x, state=%s\nPcmConfig=[%s]",
    sessionType2String(session_type_), cookie_, streamState2String(state_), pcm_cfg.toString().c_str());
  if (pcm_cfg.channelMode == ChannelMode::UNKNOWN) {
    return false;
  }
  audio_cfg->sample_rate = pcm_cfg.sampleRateHz;
  audio_cfg->channel_mask =
      (is_stereo_to_mono_
           ? AUDIO_CHANNEL_OUT_STEREO
           : OutputChannelModeToAudioFormat(pcm_cfg.channelMode));
  audio_cfg->format =
      BitsPerSampleToAudioFormat(pcm_cfg.bitsPerSample, session_type_);
  return true;
}

bool BluetoothAudioPortAidlIn::LoadAudioConfig(
    audio_config_t* audio_cfg) const {
  if (!in_use()) {
    AM_LOGE("BluetoothAudioPortAidlIn not in use");
    audio_cfg->sample_rate = kBluetoothDefaultSampleRate;
    audio_cfg->channel_mask = kBluetoothDefaultInputChannelModeMask;
    audio_cfg->format = kBluetoothDefaultAudioFormatBitsPerSample;
    return false;
  }

  const AudioConfiguration& hal_audio_cfg =
      BluetoothAudioSessionControl::GetAudioConfig(session_type_);
  if (hal_audio_cfg.getTag() != AudioConfiguration::pcmConfig) {
    audio_cfg->sample_rate = kBluetoothDefaultSampleRate;
    audio_cfg->channel_mask = kBluetoothDefaultInputChannelModeMask;
    audio_cfg->format = kBluetoothDefaultAudioFormatBitsPerSample;
    return false;
  }
  const PcmConfiguration& pcm_cfg =
      hal_audio_cfg.get<AudioConfiguration::pcmConfig>();

  AM_LOGD("session_type:%s, cookie:%#x, state=%s, PcmConfig=[%s]",
    sessionType2String(session_type_), cookie_, streamState2String(state_), pcm_cfg.toString().c_str());
  if (pcm_cfg.channelMode == ChannelMode::UNKNOWN) {
    return false;
  }

  audio_cfg->sample_rate = pcm_cfg.sampleRateHz;
  audio_cfg->channel_mask = InputChannelModeToAudioFormat(pcm_cfg.channelMode);
  audio_cfg->format =
      BitsPerSampleToAudioFormat(pcm_cfg.bitsPerSample, session_type_);
  return true;
}

bool BluetoothAudioPortAidl::CondwaitState(BluetoothStreamState state) {
  if (session_type_ == SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH) {
    // In order to be compatible with Bluetooth devices that have been started for a long time.
    return true;
  }
  bool retval;
  std::unique_lock<std::mutex> port_lock(cv_mutex_);
  switch (state) {
    case BluetoothStreamState::STARTING:
      AM_LOGD("session:%s, cookie:%#x waiting for STARTED", sessionType2String(session_type_), cookie_);
      retval = internal_cv_.wait_for(
          port_lock, std::chrono::milliseconds(kMaxWaitingTimeMs),
          [this] { return this->state_ != BluetoothStreamState::STARTING; });
      retval = retval && state_ == BluetoothStreamState::STARTED;
      break;
    case BluetoothStreamState::SUSPENDING:
      AM_LOGD("session:%s, cookie:%#x waiting for SUSPENDED", sessionType2String(session_type_), cookie_);
      retval = internal_cv_.wait_for(
          port_lock, std::chrono::milliseconds(kMaxWaitingTimeMs),
          [this] { return this->state_ != BluetoothStreamState::SUSPENDING; });
      retval = retval && state_ == BluetoothStreamState::STANDBY;
      break;
    default:
      AM_LOGW("session:%s, cookie:%#x waiting for KNOWN", sessionType2String(session_type_), cookie_);
      return false;
  }

  return retval;  // false if any failure like timeout
}

bool BluetoothAudioPortAidl::Start() {
  if (!in_use()) {
    AM_LOGE("BluetoothAudioPortAidl not in use");
    return false;
  }

  AM_LOGI("session:%s, cookie:%#x, state:%s, mono:%s request", sessionType2String(session_type_), cookie_,
    streamState2String(state_), (is_stereo_to_mono_ ? "true" : "false"));
  bool retval = false;
  if (state_ == BluetoothStreamState::STANDBY) {
    state_ = BluetoothStreamState::STARTING;
    if (BluetoothAudioSessionControl::StartStream(session_type_)) {
      retval = CondwaitState(BluetoothStreamState::STARTING);
    } else {
      AM_LOGE("session:%s, cookie:%#x, state:%s Hal fails", sessionType2String(session_type_), cookie_,
        streamState2String(state_));
    }
  }

  if (retval) {
//    AM_LOGI("session:%s, cookie:%#x, state:%s, mono:%s done", sessionType2String(session_type_), cookie_,
//      streamState2String(state_), (is_stereo_to_mono_ ? "true" : "false"));
  } else {
    AM_LOGE("session:%s, cookie:%#x, state:%s, failure", sessionType2String(session_type_), cookie_,
      streamState2String(state_));
  }

  return retval;  // false if any failure like timeout
}

bool BluetoothAudioPortAidl::Suspend() {
  if (!in_use()) {
    AM_LOGE("BluetoothAudioPortAidl not in use");
    return false;
  }

  AM_LOGI("session:%s, cookie:%#x, state:%s request", sessionType2String(session_type_), cookie_,
    streamState2String(state_));
  bool retval = false;
  if (state_ == BluetoothStreamState::STARTED) {
    state_ = BluetoothStreamState::SUSPENDING;
    if (BluetoothAudioSessionControl::SuspendStream(session_type_)) {
      retval = CondwaitState(BluetoothStreamState::SUSPENDING);
    } else {
      AM_LOGE("session:%s, cookie:%#x, state:%s Hal fails", sessionType2String(session_type_), cookie_,
        streamState2String(state_));
    }
  }

  if (retval) {
//    AM_LOGI("session:%s, cookie:%#x, state:%s done", sessionType2String(session_type_), cookie_,
//      streamState2String(state_));
  } else {
    AM_LOGE("session:%s, cookie:%#x, state:%s, failure", sessionType2String(session_type_), cookie_,
      streamState2String(state_));
  }

  return retval;  // false if any failure like timeout
}

void BluetoothAudioPortAidl::Stop() {
  if (!in_use()) {
    AM_LOGE("BluetoothAudioPortAidl not in use");
    return;
  }
//  AM_LOGI("session:%s, cookie:%#x, state:%s request", sessionType2String(session_type_), cookie_,
//    streamState2String(state_));
  state_ = BluetoothStreamState::DISABLED;
  BluetoothAudioSessionControl::StopStream(session_type_);
  AM_LOGI("session:%s, cookie:%#x, state:%s done", sessionType2String(session_type_), cookie_,
    streamState2String(state_));
}

size_t BluetoothAudioPortAidlOut::WriteData(const void* buffer,
                                            size_t bytes) const {
  if (!in_use()) return 0;
  if (!is_stereo_to_mono_) {
    return BluetoothAudioSessionControl::OutWritePcmData(session_type_, buffer,
                                                         bytes);
  }

  // WAR to mix the stereo into Mono (16 bits per sample)
  const size_t write_frames = bytes >> 2;
  if (write_frames == 0) return 0;
  auto src = static_cast<const int16_t*>(buffer);
  std::unique_ptr<int16_t[]> dst{new int16_t[write_frames]};
  downmix_to_mono_i16_from_stereo_i16(dst.get(), src, write_frames);
  // a frame is 16 bits, and the size of a mono frame is equal to half a stereo.
  return BluetoothAudioSessionControl::OutWritePcmData(session_type_, dst.get(),
                                                       write_frames * 2) *
         2;
}

size_t BluetoothAudioPortAidlIn::ReadData(void* buffer, size_t bytes) const {
  if (!in_use()) return 0;
  return BluetoothAudioSessionControl::InReadPcmData(session_type_, buffer,
                                                     bytes);
}

bool BluetoothAudioPortAidl::GetPresentationPosition(
    uint64_t* delay_ns, uint64_t* bytes, timespec* timestamp) const {
  if (!in_use()) {
    AM_LOGE("BluetoothAudioPortAidl not in use");
    return false;
  }
  PresentationPosition presentation_position;
  bool retval = BluetoothAudioSessionControl::GetPresentationPosition(
      session_type_, presentation_position);
  *delay_ns = presentation_position.remoteDeviceAudioDelayNanos;
  *bytes = presentation_position.transmittedOctets;
  *timestamp = {.tv_sec = static_cast<__kernel_old_time_t>(
                    presentation_position.transmittedOctetsTimestamp.tvSec),
                .tv_nsec = static_cast<long>(
                    presentation_position.transmittedOctetsTimestamp.tvNSec)};
  AM_LOGV("session:%s, cookie:%#x, state:%s delay:%" PRId64 " ns, data:%" PRId64 " bytes, timestamp:%ld.%ld s",
    sessionType2String(session_type_), cookie_, streamState2String(state_), *delay_ns,
    *bytes, timestamp->tv_sec, timestamp->tv_nsec);

  return retval;
}

void BluetoothAudioPortAidl::UpdateSourceMetadata(
    const source_metadata* source_metadata) const {
  if (!in_use()) {
    AM_LOGE("BluetoothAudioPortAidl not in use");
    return;
  }
  AM_LOGD("session:%s, cookie:%#x, state:%s, %zu track(s)", sessionType2String(session_type_), cookie_,
    streamState2String(state_), source_metadata->track_count);
  if (source_metadata->track_count == 0) return;
  BluetoothAudioSessionControl::UpdateSourceMetadata(session_type_,
                                                     *source_metadata);
}

void BluetoothAudioPortAidl::UpdateSinkMetadata(
    const sink_metadata* sink_metadata) const {
  if (!in_use()) {
    AM_LOGE("BluetoothAudioPortAidl not in use");
    return;
  }
  AM_LOGD("session:%s, cookie:%#x, state:%s, %zu track(s)", sessionType2String(session_type_), cookie_,
    streamState2String(state_), sink_metadata->track_count);
  if (sink_metadata->track_count == 0) return;
  BluetoothAudioSessionControl::UpdateSinkMetadata(session_type_,
                                                   *sink_metadata);
}

BluetoothStreamState BluetoothAudioPortAidl::GetState() const { return state_; }

void BluetoothAudioPortAidl::SetState(BluetoothStreamState state) {
  state_ = state;
}

}  // namespace aidl
}  // namespace audio
}  // namespace bluetooth
}  // namespace android
