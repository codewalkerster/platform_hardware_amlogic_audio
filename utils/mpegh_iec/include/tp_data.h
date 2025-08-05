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

#include <cstdint>

#ifndef TP_DATA_H
#define TP_DATA_H

typedef enum {
  MHA_PACTYP_NONE = -1,
  MHA_PACTYP_FILLDATA = 0,
  MHA_PACTYP_MPEGH3DACFG = 1,
  MHA_PACTYP_MPEGH3DAFRAME = 2,
  MHA_PACTYP_AUDIOSCENEINFO = 3,
  /* reserved for ISO use 4-5 */
  MHA_PACTYP_SYNC = 6,
  MHA_PACTYP_SYNCGAP = 7,
  MHA_PACTYP_MARKER = 8,
  MHA_PACTYP_CRC16 = 9,
  MHA_PACTYP_CRC32 = 10,
  MHA_PACTYP_DESCRIPTOR = 11,
  MHA_PACTYP_USERINTERACTION = 12,
  MHA_PACTYP_LOUDNESS_DRC = 13,
  MHA_PACTYP_BUFFERINFO = 14,
  MHA_PACTYP_GLOBAL_CRC16 = 15,    /* MPEG112 input */
  MHA_PACTYP_GLOBAL_CRC32 = 16,    /* MPEG112 input */
  MHA_PACTYP_AUDIOTRUNCATION = 17, /* MPEG112 input */
  MHA_PACTYPE_EARCON = 19,         /* ISO/IEC 23008-3:2015/FDAM 5:201x Amd 1*/
  MHA_PACTYPE_PCMCONFIG = 20,      /* ISO/IEC 23008-3:2015/FDAM 5:201x Amd 1*/
  MHA_PACTYPE_PCMDATA = 21,        /* ISO/IEC 23008-3:2015/FDAM 5:201x Amd 1*/
  MHA_PACTYP_LOUDNESS = 22,        /* 23008-3:2019/Amd 1 (Amendment 1 to 2nd edition of MPEG-H) */
  MHA_PACTYP_FRAMELENGTH = 129,
  MHA_PACTYP_UNKNOWN = 518 /* max(escapedValue(3,8,8)) + 1 */
} mha_pactyp_t;

static const uint32_t SamplingRateTable[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
                                             16000, 12000, 11025, 8000,  7350,  0,     0,     57600,
                                             51200, 40000, 38400, 34150, 28800, 25600, 20000, 19200,
                                             17075, 14400, 12800, 9600,  0,     0,     0,     0};

#endif /* TP_DATA_H */
