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

#ifndef BITBUFFER_H
#define BITBUFFER_H

/* leave 3 bits headroom so MAX_BUFSIZE can be represented in bits as well. */
#define MAX_BUFSIZE_BYTES (0x08000000)

typedef struct {
  int32_t ValidBits;
  uint32_t ReadOffset;
  uint32_t WriteOffset;
#ifdef BITCNT_ENABLE
  uint32_t BitCnt;
#endif
  uint32_t BitNdx;

  uint8_t* Buffer;  /* struct member offset:  5 */
  uint32_t bufSize; /* struct member offset:  6 */
  uint32_t bufBits; /* struct member offset:  7 */
} BITBUFFER;

typedef BITBUFFER* HANDLE_BITBUFFER;

#ifdef __cplusplus
extern "C" {
#endif

extern const uint32_t BitMask[32 + 1];

/**  The BitBuffer Functions are called straight from bitstream Interface.
     For Functions functional survey look there.
*/

void bitbuffer_create(HANDLE_BITBUFFER* hBitBuffer, uint8_t* pBuffer, uint32_t bufSize);

void bitbuffer_delete(HANDLE_BITBUFFER hBitBuffer);

void bitbuffer_init(HANDLE_BITBUFFER hBitBuffer, uint8_t* pBuffer, uint32_t bufSize,
                    uint32_t validBits);

void bitbuffer_reset(HANDLE_BITBUFFER hBitBuffer);

int32_t bitbuffer_get(HANDLE_BITBUFFER hBitBuffer, const uint32_t numberOfBits);

int32_t bitbuffer_get32(HANDLE_BITBUFFER hBitBuf);

int32_t bitbuffer_getBwd(HANDLE_BITBUFFER hBitBuffer, const uint32_t numberOfBits);

void bitbuffer_put(HANDLE_BITBUFFER hBitBuffer, uint32_t value, const uint32_t numberOfBits);

void bitbuffer_putBwd(HANDLE_BITBUFFER hBitBuffer, uint32_t value, const uint32_t numberOfBits);

void bitbuffer_pushBack(HANDLE_BITBUFFER hBitBuffer, const uint32_t numberOfBits, uint8_t config);

void bitbuffer_pushForward(HANDLE_BITBUFFER hBitBuffer, const uint32_t numberOfBits,
                           uint8_t config);

int32_t bitbuffer_getValidBits(HANDLE_BITBUFFER hBitBuffer);

int32_t bitbuffer_getFreeBits(HANDLE_BITBUFFER hBitBuffer);

#ifdef BITCNT_ENABLE
void bitbuffer_byteAlign(HANDLE_BITBUFFER hBitBuffer, uint8_t config);

void bitbuffer_setBitCnt(HANDLE_BITBUFFER hBitBuffer, const uint32_t value);

int32_t bitbuffer_getBitCnt(HANDLE_BITBUFFER hBitBuffer);
#endif

void bitbuffer_feed(HANDLE_BITBUFFER hBitBuffer, const uint8_t inputBuffer[],
                    const uint32_t bufferSize, uint32_t* bytesValid);

void bitbuffer_copy(HANDLE_BITBUFFER hBitBufDst, HANDLE_BITBUFFER hBitBufSrc, uint32_t* bytesValid);

void bitbuffer_fetch(HANDLE_BITBUFFER hBitBuffer, uint8_t outBuf[], uint32_t* writeBytes);

#ifdef __cplusplus
}
#endif

#endif
