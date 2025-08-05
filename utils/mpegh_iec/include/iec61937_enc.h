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

#include <stdint.h>
#include <stdbool.h>

#if !defined(IEC61937_ENC_H)
#define IEC61937_ENC_H

/**
 * @file   iec61937_enc.h
 * @brief  IEC61937-13 encoder library interface header file.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define IEC61937_AUDIOFRAME_LENGTH 1024
#define IEC61937_MAX_SAMPLERATE_FACTOR 16
#define IEC60958_FRAME_SIZE_BYTES 4

#define MAX_IEC61937_FRAME_SIZE_BYTES \
  (IEC61937_AUDIOFRAME_LENGTH) * (IEC61937_MAX_SAMPLERATE_FACTOR) * (IEC60958_FRAME_SIZE_BYTES)

typedef enum IECENC_RESULT {
  IECENC_OK = 0,         /*!< Ok, no error */
  IECENC_BUFFER_ERROR,   /*!< Working buffer full or output buffer size too small */
  IECENC_NULLPTR_ERROR,  /*!< A nullptr was used */
  IECENC_DURATION_ERROR, /*!< The provided frame duration exceeds the maximum allowed duration */
} IECENC_RESULT;

/* IEC61937-13 encoder state structure */
typedef struct iec61937_encoder_state* HANDLE_IEC61937_ENCODER;

/**
 * @brief Encode one IEC61937-13 MPEG-H frame.
 * @param[in] h encoder handle
 * @param[in] inputBuffer pointer to data buffer where one MPEG-H frame is read from
 * @param[in] inputBufferLength size in bytes of the data in inputBuffer
 * @param[out] fInputBufferProcessed flag set to true if data from inputBuffer was read or false if
 * it had to be postponed, i.e. the inputBuffer needs to be passed in again
 * @param[in] duration the amount of audio samples according to PTS difference of consecutive MPEG-H
 * frames
 * @param[out] outputBuffer pointer to an output data buffer into which one resulting IEC61937-13
 * frame will be written
 * @param[in,out] pOutputBufferLength pointer to the capacity of the outputBuffer on input and the
 * number of bytes written into outputBuffer on output
 * @returns IECENC_OK in case of success, IECDEC_BUFFER_ERROR in case the internal buffer is full or
 * the output buffer size is too small and IECENC_NULLPTR_ERROR if a nullptr was used as an input
 * argument.
 */
IECENC_RESULT iec61937_encode_process(HANDLE_IEC61937_ENCODER h, const uint8_t* inputBuffer,
                                      uint32_t inputBufferLength, bool* fInputBufferProcessed,
                                      uint32_t duration, uint8_t* outputBuffer,
                                      uint32_t* pOutputBufferLength);

/**
 * @brief Create a IEC61937-13 encoder instance.
 * @param[in] rateFactor bit rate factor for IEC frame rate. The rate factors are defined in
 * specification IEC 61937-13 subclause 5.3.2. Supported rate factors are 4 and 16.
 * @return HANDLE_IEC61937_ENCODER in case of success, NULL in case of error.
 */
HANDLE_IEC61937_ENCODER iec61937_encode_open(uint8_t rateFactor);

/**
 * @brief Close a IEC61937-13 encoder instance.
 * @param[in] h encoder handle to be closed
 */
void iec61937_encode_close(HANDLE_IEC61937_ENCODER h);

#ifdef __cplusplus
}
#endif

#endif /* !defined(IEC61937_ENC_H) */
