/**************************************************************************
*               (C) copyright Fraunhofer - IIS (2024)
*                        All Rights Reserved
*
*   This software and/or program is protected by copyright law and
*   international treaties. Any unauthorized reproduction or distribution
*   of this software and/or program, or any portion of it, may result in
*   severe civil and criminal penalties, and will be prosecuted to the
*   maximum extent possible under law.
*
\*************************************************************************/

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "bitstream.h"
#include "tp_data.h"

extern "C" {

#include "EarconPacketWriter_lib.h"

#define EARCON_BUFFER_SIZE 8096

struct EarconData {
  int32_t bsNumEarcons;
  int32_t earconActive;
  int32_t bsPcmLoudnessValue;
  int32_t pcmHasAttenuationGain;
  int32_t bsPcmAttenuationGain;
  int16_t EarconDataBuffer[EARCON_BUFFER_SIZE];
  int32_t endPacket_flag;
  int32_t FrameLength;
};

struct ParsedParams {
  uint32_t config_frame_present_flag;
  uint32_t parsed_frame_length;
  uint32_t truncation_present;
  uint32_t truncationFromBegin_flag;
  uint32_t truncatedSamples;
  int32_t insert_offset;
  uint32_t frame_duration_samples;
};

struct EarconControl {
  uint32_t FrameLength;
  uint32_t NumberOfEarconSignals;
  uint32_t EarconLoudness;
  uint32_t AttGain;
};

struct Earcon {
  EarconData Data;
  ParsedParams Params;
  EarconControl Control;
};

static uint32_t nextPow2(uint32_t x) {
  uint32_t y = 1;
  while (y < x) y <<= 1;
  return y;
}

static int32_t getSampleRate(HANDLE_BITSTREAM bs, uint8_t* index, uint32_t nBits) {
  int32_t sampleRate;
  uint32_t idx;

  idx = bitstream_readBits(bs, nBits);
  if (idx == (1 << nBits) - 1) {
    if (bitstream_getValidBits(bs) < 24) {
      return 0;
    }
    sampleRate = bitstream_readBits(bs, 24);
  } else {
    sampleRate = SamplingRateTable[idx];
  }

  *index = idx;

  return sampleRate;
}

static int32_t WriteEarconInfo(HANDLE_BITSTREAM hBs, EarconData* Data) {
  int32_t bsNumEarcons = Data->bsNumEarcons;
  int32_t packetLengthInBits = bitstream_writeBits(hBs, bsNumEarcons, 7);
  for (int32_t i = 0; i < bsNumEarcons + 1; i++) {
    packetLengthInBits += bitstream_writeBits(hBs, 1, 1);  // earconIsIndependent = 1
    packetLengthInBits += bitstream_writeBits(hBs, i, 7);  // earconID (set at the counter)
    packetLengthInBits += bitstream_writeBits(hBs, 5, 4);  // earconType = 5
    packetLengthInBits += bitstream_writeBits(hBs, Data->earconActive, 1);  // earconActive
    packetLengthInBits += bitstream_writeBits(hBs, 0, 2);  // earconPositionType = 0

    int32_t earcon_CICPspeakerIdx = 2;
    if (bsNumEarcons == 1) {
      earcon_CICPspeakerIdx = i;
    }
    packetLengthInBits +=
        bitstream_writeBits(hBs, earcon_CICPspeakerIdx, 7);  // earcon_CICPspeakerIdx

    packetLengthInBits += bitstream_writeBits(hBs, 0, 1);  // earconHasGain = 0
    packetLengthInBits += bitstream_writeBits(hBs, 0, 1);  // earconHasTextLabel = 0
  }

  int32_t rest_bits = packetLengthInBits % 8;
  if (rest_bits) {
    packetLengthInBits += bitstream_writeBits(hBs, 0, 8 - rest_bits);
  }
  return packetLengthInBits;
}

static int32_t WritePcmDataConfig(HANDLE_BITSTREAM hBs, EarconData* Data, int32_t frame_size) {
  int32_t bsNumEarcons = Data->bsNumEarcons;
  int32_t packetLengthInBits =
      bitstream_writeBits(hBs, bsNumEarcons, 7);         // bsNumPcmSignals = bsNumEarcons
  packetLengthInBits += bitstream_writeBits(hBs, 0, 1);  // pcmAlignAudioFlag = 0
  packetLengthInBits += bitstream_writeBits(hBs, 3, 5);  // pcmSamplingRateIndex = 0x03
  packetLengthInBits += bitstream_writeBits(hBs, 0, 3);  // pcmBitsPerSampleIndex = 0

  int32_t value = 6;
#if 0
    if (frame_size == 1024) {
        value = 2;
    }
    else if (frame_size == 2048) {
        value = 3;
    }
    else {
        value = 5;
    }
#endif
  packetLengthInBits += bitstream_writeBits(hBs, value, 3);  // pcmFrameSizeIndex
  if (value == 5) {
    packetLengthInBits += bitstream_writeBits(hBs, frame_size, 16);  // pcmFixFrameSize
  }

  for (int32_t i = 0; i < bsNumEarcons + 1; i++) {
    packetLengthInBits += bitstream_writeBits(hBs, i, 7);  // pcmSignal_ID (set at the counter)
  }

  packetLengthInBits +=
      bitstream_writeBits(hBs, Data->bsPcmLoudnessValue, 8);  // bsPcmLoudnessValue
  packetLengthInBits +=
      bitstream_writeBits(hBs, Data->pcmHasAttenuationGain, 2);  // pcmHasAttenuationGain
  if (Data->pcmHasAttenuationGain == 1) {
    packetLengthInBits +=
        bitstream_writeBits(hBs, Data->bsPcmAttenuationGain, 8);  // bsPcmAttenuationGain
  }

  int32_t rest_bits = packetLengthInBits % 8;
  if (rest_bits) {
    packetLengthInBits += bitstream_writeBits(hBs, 0, 8 - rest_bits);
  }
  return packetLengthInBits;
}

static int32_t WritePcmDataPayload(HANDLE_BITSTREAM hBs, EarconData* Data, int32_t frame_size) {
  int32_t bsNumEarcons = Data->bsNumEarcons;
  int32_t packetLengthInBits =
      bitstream_writeBits(hBs, bsNumEarcons, 7);  // bsNumPcmSignalsInFrame = bsNumEarcons
  for (int32_t i = 0; i < bsNumEarcons + 1; i++) {
    packetLengthInBits += bitstream_writeBits(hBs, i, 7);  // pcmSignal_ID (set at the counter)
  }

  if (Data->pcmHasAttenuationGain == 2) {
    packetLengthInBits +=
        bitstream_writeBits(hBs, Data->bsPcmAttenuationGain, 8);  // bsPcmAttenuationGain
  }

  // Write always.
  packetLengthInBits += bitstream_writeBits(hBs, frame_size, 16);  // pcmVarFrameSize

  int16_t* p2buf = &Data->EarconDataBuffer[0];
  for (int32_t i = 0; i < (frame_size); i++) {
    for (int32_t j = 0; j < bsNumEarcons + 1; j++) {
      packetLengthInBits += bitstream_writeBits(hBs, *p2buf++, 16);  // pcmSample; 16 bits each
    }
  }

  int32_t rest_bits = packetLengthInBits % 8;
  if (rest_bits) {
    packetLengthInBits += bitstream_writeBits(hBs, 0, 8 - rest_bits);
  }
  return packetLengthInBits;
}

static int32_t WriteEarconData(HANDLE_BITSTREAM hBs, EarconData* Data) {
  uint32_t packetLength, accumulated_bits;

  int32_t frame_size = Data->FrameLength;

  accumulated_bits = 0;

  // Write earconInfo struct.
  accumulated_bits += bitstream_writeEscapedValue(hBs, MHA_PACTYPE_EARCON, 3, 8,
                                                  8);  // packetType = MHA_PACTYPE_EARCON
  accumulated_bits += bitstream_writeEscapedValue(hBs, 2049, 2, 8, 32);  // packetLabel = 2049
  // Calculate the length of the packet in bits.
  packetLength = WriteEarconInfo(nullptr, Data);
  // Write packet length.
  accumulated_bits +=
      bitstream_writeEscapedValue(hBs, (packetLength >> 3), 11, 24, 24);  // packetLength
  // Write earconInfo structure.
  accumulated_bits += WriteEarconInfo(hBs, Data);

  // Write pcmDataConfig struct.
  accumulated_bits += bitstream_writeEscapedValue(hBs, MHA_PACTYPE_PCMCONFIG, 3, 8,
                                                  8);  // packetType = MHA_PACTYPE_PCMCONFIG
  accumulated_bits += bitstream_writeEscapedValue(hBs, 2049, 2, 8, 32);  // packetLabel = 2049
  // Calculate the length  of the packet in bits.
  packetLength = WritePcmDataConfig(nullptr, Data, frame_size);
  // Write packet length.
  accumulated_bits +=
      bitstream_writeEscapedValue(hBs, (packetLength >> 3), 11, 24, 24);  // packetLength
  // Write pcmDataConfig structure.
  accumulated_bits += WritePcmDataConfig(hBs, Data, frame_size);

  // Write an audio truncation frame.
  if (Data->endPacket_flag) {
    accumulated_bits += bitstream_writeEscapedValue(hBs, MHA_PACTYP_AUDIOTRUNCATION, 3, 8,
                                                    8);  // packetType = MHA_PACTYP_AUDIOTRUNCATION
    accumulated_bits += bitstream_writeEscapedValue(hBs, 2049, 2, 8, 32);  // packetLabel = 2049
    accumulated_bits += bitstream_writeEscapedValue(hBs, 2, 11, 24, 24);   // packetLength = 2 bytes
    accumulated_bits += bitstream_writeBits(hBs, 1, 1);                    // isActive = 1
    accumulated_bits += bitstream_writeBits(hBs, 1, 1);                    // reserved
    accumulated_bits += bitstream_writeBits(hBs, 0, 1);   // truncation from begin = 0
    accumulated_bits += bitstream_writeBits(hBs, 0, 13);  // truncation samples = 0
  }

  // Write pcmDataPayload struct.
  accumulated_bits += bitstream_writeEscapedValue(hBs, MHA_PACTYPE_PCMDATA, 3, 8,
                                                  8);  // packetType = MHA_PACTYPE_PCMDATA
  accumulated_bits += bitstream_writeEscapedValue(hBs, 2049, 2, 8, 32);  // packetLabel = 2049
  // Calculate the length  of the packet in bits.
  packetLength = WritePcmDataPayload(nullptr, Data, frame_size);
  // Write packet length.
  accumulated_bits +=
      bitstream_writeEscapedValue(hBs, (packetLength >> 3), 11, 24, 24);  // packetLength
  // Write pcmDataPayload structure.
  accumulated_bits += WritePcmDataPayload(hBs, Data, frame_size);

  // Return number of used bytes.
  return accumulated_bits >> 3;
}

EARCON_ERROR Earcon_open(HANDLE_EARCON* hEarcon) {
  *hEarcon = (HANDLE_EARCON)calloc(1, sizeof(struct Earcon));
  if (*hEarcon == nullptr) {
    return EARCON_ERR;
  }
  return EARCON_OK;
}

EARCON_ERROR Earcon_close(HANDLE_EARCON* hEarcon) {
  if (*hEarcon == nullptr) {
    return EARCON_ERR;
  }
  free(*hEarcon);
  *hEarcon = nullptr;
  return EARCON_OK;
}

EARCON_ERROR Earcon_init(HANDLE_EARCON hEarcon, bool MonoOrStereoFlag, uint8_t EarconLoudness,
                         uint8_t EarconAttenuation) {
  if (hEarcon == nullptr) {
    return EARCON_ERR;
  }
  EarconControl* ControlStruct = &hEarcon->Control;

  ControlStruct->FrameLength = 0;
  ControlStruct->NumberOfEarconSignals = MonoOrStereoFlag + 1;
  ControlStruct->EarconLoudness = EarconLoudness;
  ControlStruct->AttGain = EarconAttenuation;
  return EARCON_OK;
}

/* Parse Bitstream data and get frame length. */
EARCON_ERROR Earcon_feedMHAS(HANDLE_EARCON hEarcon, uint8_t* mhas_frame_buf,
                             uint32_t mhas_frame_buf_len, uint32_t* duration) {
  if (hEarcon == nullptr || mhas_frame_buf == nullptr) {
    return EARCON_ERR;
  }
  int32_t nBitsIns = 0;
  int32_t frameFound = 0;
  BITSTREAM bs;
  HANDLE_BITSTREAM hBs = &bs;

  ParsedParams* Params = &hEarcon->Params;
  EarconControl* ControlStruct = &hEarcon->Control;

  // Clear the parameters.
  memset(Params, 0, sizeof(*Params));

  bitstream_init(hBs, mhas_frame_buf, nextPow2(mhas_frame_buf_len), mhas_frame_buf_len * 8);

  nBitsIns = bitstream_getValidBits(hBs);

  if (nBitsIns <= 0) {
    return EARCON_ERR;
  }
  if ((nBitsIns & 3) != 0) {
    return EARCON_ERR;
  }

  while (1) {
    mha_pactyp_t packetType = MHA_PACTYP_NONE;
    uint32_t packetLength;
    int32_t nBits, nBits1;

    // Parse MHAS packet header.
    packetType = (mha_pactyp_t)bitstream_readEscapedValue(hBs, 3, 8, 8);
    bitstream_readEscapedValue(hBs, 2, 8, 32);
    packetLength = bitstream_readEscapedValue(hBs, 11, 24, 24);

    nBits = (int32_t)bitstream_getValidBits(hBs);
    if (nBits <= 0) {
      bitstream_pushBack(hBs, (nBitsIns - nBits));
      return EARCON_ERR;
    }

    switch (packetType) {
      case MHA_PACTYP_SYNC:
        bitstream_pushFor(hBs, 8);
        break;

      case MHA_PACTYP_MPEGH3DACFG:
        // Profile Level Indication
        bitstream_pushFor(hBs, 8);
        {
          uint32_t bit_packetLength;
          uint8_t samplingFrequencyIndex;
          int32_t usacSamplingFrequency;

          bit_packetLength = packetLength * 8 - 8;

          usacSamplingFrequency = getSampleRate(hBs, &samplingFrequencyIndex, 5);
          bit_packetLength -= 5;
          if (usacSamplingFrequency == 0x1f) {
            usacSamplingFrequency = getSampleRate(hBs, &samplingFrequencyIndex, 24);
            bit_packetLength -= 24;
          }

          if ((usacSamplingFrequency != 48000) && (usacSamplingFrequency != 32000) &&
              (usacSamplingFrequency != 24000) && (usacSamplingFrequency != 16000)) {
            return EARCON_ERR;
          }

          switch (usacSamplingFrequency) {
            case 48000:
              Params->parsed_frame_length = 1024;
              break;
            case 32000:
              Params->parsed_frame_length = 1536;
              break;
            case 24000:
              Params->parsed_frame_length = 2048;
              break;
            case 16000:
              Params->parsed_frame_length = 3072;
              break;
          }

          Params->config_frame_present_flag = 1;

          bitstream_pushFor(hBs, bit_packetLength);
        }
        break;

      case MHA_PACTYP_AUDIOTRUNCATION:
        Params->truncation_present = bitstream_readBits(hBs, 1);        // isActive
        bitstream_readBits(hBs, 1);                                     // reserved
        Params->truncationFromBegin_flag = bitstream_readBits(hBs, 1);  // truncFromBegin
        Params->truncatedSamples = bitstream_readBits(hBs, 13);         // nTruncSamples
        bitstream_pushFor(hBs, packetLength * 8 - 16);
        break;

      case MHA_PACTYP_MPEGH3DAFRAME:
        bitstream_pushFor(hBs, packetLength * 8);

        // frame found
        frameFound = 1;
        break;

      default:
        bitstream_pushFor(hBs, packetLength * 8);
        break;
    }

    // Skip remaining bits.
    nBits = 8 * packetLength - (nBits - bitstream_getValidBits(hBs));
    if (nBits >= 0) {
      bitstream_pushFor(hBs, nBits);
    }

    // Update position to insert Earcon packets.
    if (packetType != MHA_PACTYP_MPEGH3DAFRAME) {
      Params->insert_offset = ((nBitsIns - bitstream_getValidBits(hBs)) >> 3);
    }

    nBits1 = (int32_t)bitstream_getValidBits(hBs);
    if (nBits1 < 0) {
      bitstream_pushBack(hBs, (nBitsIns - nBits1));
      return EARCON_ERR;
    }
    if ((nBits1 & 3) != 0) {
      return EARCON_ERR;
    }

    if (frameFound) {
      break;
    }
  }

  if (Params->config_frame_present_flag) {
    ControlStruct->FrameLength = Params->parsed_frame_length;
  }

  if (ControlStruct->FrameLength > 0) {
    Params->frame_duration_samples = ControlStruct->FrameLength;
    if (Params->truncation_present) {
      Params->frame_duration_samples -= Params->truncatedSamples;
    }
  }

  // Export AU duration.
  if (duration != nullptr) {
    *duration = Params->frame_duration_samples;
  }
  return EARCON_OK;
}

int32_t Earcon_addSystemSound(HANDLE_EARCON hEarcon, int16_t* EarconBuffer,
                              uint32_t EarconBuffer_Length) {
  if (hEarcon == nullptr || EarconBuffer == nullptr) {
    return -1;
  }
  EarconControl* ControlStruct = &hEarcon->Control;
  ParsedParams* Params = &hEarcon->Params;
  EarconData* Data = &hEarcon->Data;

  int32_t number_of_samples_consumed = 0;
  int32_t EarconBuffer_Samples = EarconBuffer_Length / ControlStruct->NumberOfEarconSignals;
  if (EarconBuffer_Samples > 0) {
    Data->endPacket_flag = 0;
    if (EarconBuffer_Samples > Params->frame_duration_samples) {
      // Write a whole frame from buffer.
      Data->FrameLength = Params->frame_duration_samples;
    } else {
      // Write remaining Earcon data from buffer and set endPacket_flag.
      Data->FrameLength = EarconBuffer_Samples;
      Data->endPacket_flag = 1;
    }
    number_of_samples_consumed = Data->FrameLength * ControlStruct->NumberOfEarconSignals;
    memcpy(Data->EarconDataBuffer, EarconBuffer, number_of_samples_consumed * sizeof(int16_t));
    Data->bsNumEarcons = ControlStruct->NumberOfEarconSignals - 1;
    Data->earconActive = 1;
    Data->bsPcmLoudnessValue = ControlStruct->EarconLoudness;
    Data->pcmHasAttenuationGain = 0;
    if (ControlStruct->AttGain != 0) {
      Data->pcmHasAttenuationGain = 1;
      Data->bsPcmAttenuationGain = ControlStruct->AttGain;
    }
  } else {
    // Reset written data.
    memset(Data, 0, sizeof(*Data));
  }

  return number_of_samples_consumed;
}

EARCON_ERROR Earcon_updateMHAS(HANDLE_EARCON hEarcon, uint8_t* mhas_frame_buf,
                               uint32_t mhas_frame_buf_len, uint32_t* mhas_frame_len) {
  if (hEarcon == nullptr || mhas_frame_buf == nullptr || mhas_frame_len == nullptr) {
    return EARCON_ERR;
  }
  BITSTREAM bs;
  HANDLE_BITSTREAM hBs = &bs;

  ParsedParams* Params = &hEarcon->Params;
  EarconData* Data = &hEarcon->Data;

  if (Data->FrameLength <= 0) {
    return EARCON_OK;
  }

  // Calculate Earcon packet length.
  int Earcon_packet_length = WriteEarconData(nullptr, Data);

  if ((*mhas_frame_len + Earcon_packet_length) > mhas_frame_buf_len) {
    return EARCON_ERR;
  }

  // Prepare writing of packets.
  memmove(mhas_frame_buf + Params->insert_offset + Earcon_packet_length,
          mhas_frame_buf + Params->insert_offset, *mhas_frame_len - Params->insert_offset);
  bitstream_init(hBs, mhas_frame_buf + Params->insert_offset, nextPow2(Earcon_packet_length), 0,
                 BS_WRITER);

  // Write Earcon packets.
  WriteEarconData(hBs, Data);
  bitstream_syncCache(hBs);
  std::cout << "Total.size: " << *mhas_frame_len+Earcon_packet_length << ", mhas_frame_len: " << *mhas_frame_len<< ", Earcon_packet_length: " << Earcon_packet_length<< ", insert_offset: " << Params->insert_offset << "\r" << std::flush;

  *mhas_frame_len += Earcon_packet_length;

  return EARCON_OK;
}

}//extern "C"
