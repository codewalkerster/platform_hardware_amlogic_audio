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

#if !defined(IEC61937_DEC_H)
#define IEC61937_DEC_H

/**
 * @file   iec61937_dec.h
 * @brief  IEC61937-13 decoder library interface header file.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Buffer size in bytes to hold one MPEG-H frame (sequence of MHAS packages) + overhead
// for MPEG-H Level 4
#define MAX_MPEGH_FRAME_SIZE 65536

#define MAX_AUDIOFRAME_LENGTH 4096
#define IEC61937_MAX_SAMPLERATE_FACTOR 16
#define IEC60958_FRAME_SIZE_BYTES 4

#define MAX_IEC61937_FRAME_SIZE_BYTES \
  (MAX_AUDIOFRAME_LENGTH) * (IEC61937_MAX_SAMPLERATE_FACTOR) * (IEC60958_FRAME_SIZE_BYTES)

#define WORKBUFFER_SIZE_BYTES (MAX_IEC61937_FRAME_SIZE_BYTES) * 3

typedef enum IECDEC_RESULT {
  IECDEC_OK = 0,            /*!< Ok, no error */
  IECDEC_FEED_MORE_DATA,    /*!< Ok, but more input data needs to be fed */
  IECDEC_PENDINGDATA_ERROR, /*!< The pending data could not be completed (e.g. data offset mismatch)
                                 or the available data exceeds the pending data limit */
  IECDEC_BUFFER_ERROR,      /*!< Working buffer full or output buffer size too small */
  IECDEC_NULLPTR_ERROR,     /*!< A nullptr was used */
} IECDEC_RESULT;

/* IEC61937-13 decoder state structure */
typedef struct iec61937_decoder_state* HANDLE_IEC61937_DECODER;

/**
 * @brief Open an IEC61937-13 decoder instance.
 * @return HANDLE_IEC61937_DECODER on success or NULL in case of error
 */
HANDLE_IEC61937_DECODER iec61937_decode_open(void);

/**
 * @brief Close an IEC61937-13 decoder instance.
 * @param[in] h decoder handle to be closed.
 */
void iec61937_decode_close(HANDLE_IEC61937_DECODER h);

/**
 * @brief Feed IEC frames/data chunks to the IEC61937-13 decoder.
 * @param[in] h decoder handle
 * @param[in] inputBuffer pointer to a data buffer to read the input data from
 * @param[in] inputBufferLength length in bytes of the provided input data
 * @returns IECDEC_OK in case of success, IECDEC_BUFFER_ERROR if the size of the input data is too
 * big to fit into the internal working buffer and IECDEC_NULLPTR_ERROR if a nullptr was used as an
 * input argument
 */
IECDEC_RESULT iec61937_decode_feed(HANDLE_IEC61937_DECODER h, const uint8_t* inputBuffer,
                                   uint32_t inputBufferLength);

/**
 * @brief Decode the IEC61937-13 frame and obtain one MPEG-H frame.
 * @param[in] h decoder handle
 * @param[out] outputBuffer pointer to an output data buffer into which the MPEG-H frame is written
 * @param[in,out] pOutputBufferLength pointer to the capacity of the outputBuffer on input and the
 * number of bytes written into outputBuffer on output
 * @param[out] pPcmOffset pointer to where the PCM offset of the MPEG-H frame written to
 * outputBuffer is stored into; can be used to recreate the PTS of the obtained MPEG-H frame
 * @param[out] pIecFrameLength pointer where the frame length of the current IEC frame is stored
 * into; can be used to recreate the PTS of the obtained MPEG-H frame
 * @param[out] pIecFrameProcessed pointer where the info about having completed the processing of
 * the IEC frame is stored into; can be used to recreate the PTS of the obtained MPEG-H frame
 * @return IECDEC_OK on success, IECDEC_FEED_MORE_DATA if new data needs to fed into the decoder,
 * IECDEC_BUFFER_ERROR if the provided output buffer has not enough space to hold the output MPEG-H
 * frame and IECDEC_NULLPTR_ERROR if a nullptr was used as an input argument
 */
IECDEC_RESULT iec61937_decode_process(HANDLE_IEC61937_DECODER h, uint8_t* outputBuffer,
                                      uint32_t* pOutputBufferLength, int32_t* pPcmOffset,
                                      uint32_t* pIecFrameLength, bool* pIecFrameProcessed);

#ifdef __cplusplus
}
#endif

#endif /* !defined(IEC61937_DEC_H) */
