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

//#include <cstdint>
//#include <cstdbool>
#include <stdint.h>
#include <stdbool.h>

#ifndef EARCON_H
#define EARCON_H

typedef enum { EARCON_OK = 0, EARCON_ERR = 1 } EARCON_ERROR;

typedef struct Earcon* HANDLE_EARCON;

/**
 * @brief                           Open an Earcon writer instance.
 *
 * @param[in,out] hEarcon           Earcon writer handle
 * @return                          Error code.
 */
EARCON_ERROR Earcon_open(HANDLE_EARCON* hEarcon);

/**
 * @brief                           De-allocate all resources of an Earcon writer instance.
 *
 * @param[in,out] hEarcon           Earcon writer handle
 * @return                          Error code.
 */
EARCON_ERROR Earcon_close(HANDLE_EARCON* hEarcon);

/**
 * @brief                           Initialize the parameters for the Earcon writer instance.
 *
 * @param[in] hEarcon               Earcon writer handle
 * @param[in] MonoOrStereoFlag      Flag signalizing if the PCM audio signal is mono or stereo.
 *                                  0 - mono, 1 - stereo
 * @param[in] EarconLoudness        Earcon PCM Loudness input. Range: [0, 255]
 *                                  Defines the loudness value for a PCM audio signal.
 *                                  Actual value calculated by L = -57.75 + 0.25 * v [LKFS]
 * @param[in] EarconAttenuation     MPEGH attenuation gain input. Range: [0, 255]
 *                                  Defines an attenuation gain which shall be applied to all active
 *                                  audio elements during playback of a PCM audio signal.
 *                                  Actual value calculated by Att = -0.25 * v [dB]
 * @return                          Error code.
 */
EARCON_ERROR Earcon_init(HANDLE_EARCON hEarcon, bool MonoOrStereoFlag, uint8_t EarconLoudness,
                         uint8_t EarconAttenuation);

/**
 * @brief                           Feed MHAS input into the Earcon writer.
 *
 * @param[in] hEarcon               Earcon writer handle
 * @param[in] mhas_frame_buf        Input buffer containing all MHAS packets for exactly one audio
 *                                  frame.
 * @param[in] mhas_frame_buf_len    Size of MHAS packets in bytes.
 * @param[out] duration             Audio frame duration in samples.
 * @return                          Error code.
 */
EARCON_ERROR Earcon_feedMHAS(HANDLE_EARCON hEarcon, uint8_t* mhas_frame_buf,
                             uint32_t mhas_frame_buf_len, uint32_t* duration);

/**
 * @brief                           Add a system sound to the Earcon writer to be embedded into the
 *                                  bitstream.
 *
 * @param[in] hEarcon               Earcon writer handle
 * @param[in] EarconBuffer          Buffer with Earcon data holding 16bit PCM samples
 * @param[in] EarconBuffer_Length   Length of Earcon data buffer
 * @return                          Number of consumed samples from the Earcon data buffer; negative
 *                                  value in case of error
 */
int32_t Earcon_addSystemSound(HANDLE_EARCON hEarcon, int16_t* EarconBuffer,
                              uint32_t EarconBuffer_Length);

/**
 * @brief                           Update MHAS buffer, inserting Earcon, PcmConfig and PcmData
 *                                  packets. A previous call of Earcon_feedMHAS() is required.
 *
 * @param[in] hEarcon               Earcon writer handle
 * @param[in,out] mhas_frame_buf    Input/output buffer, modified in-place. As input has to contain
 *                                  exactly the same data as passed to previous call of
 *                                  Earcon_feedMHAS(). Upon return the buffer is updated with
 *                                  Earcon, PcmConfig and PcmData packets inserted.
 * @param[in] mhas_frame_buf_len    Size of buffer in bytes. This must be large enough to allow the
 *                                  embedding of the Earcon, PcmConfig and PcmData packets.
 * @param[in,out] mhas_frame_len    Pointer to variable receiving total size of updated MHAS packet
 *                                  buffer in bytes.
 * @return                          Error code.
 */
EARCON_ERROR Earcon_updateMHAS(HANDLE_EARCON hEarcon, uint8_t* mhas_frame_buf,
                               uint32_t mhas_frame_buf_len, uint32_t* mhas_frame_len);

#endif
