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

#include "bitbuffer.h"

#ifndef BITSTREAM_H
#define BITSTREAM_H

#define CACHE_BITS 32

typedef enum { BS_READER, BS_WRITER } BS_CFG;

typedef struct {
  uint32_t CacheWord;
  uint32_t BitsInCache;
  BITBUFFER hBitBuf;
  uint32_t ConfigCache;
} BITSTREAM;

typedef BITSTREAM* HANDLE_BITSTREAM;

/**
 * \brief CreateBitStream Function.
 *
 * Create and initialize bitstream with extern allocated buffer.
 *
 * \param pBuffer  Pointer to BitBuffer array.
 * \param bufSize  Length of BitBuffer array. (awaits size 2^n and <= MAX_BUFSIZE_BYTES)
 * \param config   Initialize BitStream as Reader or Writer.
 */
HANDLE_BITSTREAM bitstream_create(uint8_t* pBuffer, uint32_t bufSize, BS_CFG config = BS_READER) {
  HANDLE_BITSTREAM hBitStream = (HANDLE_BITSTREAM)calloc(1, sizeof(BITSTREAM));
  if (hBitStream == nullptr) return nullptr;
  bitbuffer_init(&hBitStream->hBitBuf, pBuffer, bufSize, 0);

  /* init cache */
  hBitStream->CacheWord = hBitStream->BitsInCache = 0;
  hBitStream->ConfigCache = config;

  return hBitStream;
}

/**
 * \brief Initialize BistreamBuffer. BitBuffer can point to filled BitBuffer array .
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param pBuffer    Pointer to BitBuffer array.
 * \param bufSize    Length of BitBuffer array in bytes. (awaits size 2^n and <= MAX_BUFSIZE_BYTES)
 * \param validBits  Number of valid BitBuffer filled Bits.
 * \param config     Initialize BitStream as Reader or Writer.
 * \return void
 */
void bitstream_init(HANDLE_BITSTREAM hBitStream, uint8_t* pBuffer, uint32_t bufSize,
                    uint32_t validBits, BS_CFG config = BS_READER) {
  bitbuffer_init(&hBitStream->hBitBuf, pBuffer, bufSize, validBits);

  /* init cache */
  hBitStream->CacheWord = hBitStream->BitsInCache = 0;
  hBitStream->ConfigCache = config;
}

/**
 * \brief ResetBitbuffer Function. Reset states in BitBuffer and Cache.
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param config     Initialize BitStream as Reader or Writer.
 * \return void
 */
void bitstream_reset(HANDLE_BITSTREAM hBitStream, BS_CFG config = BS_READER) {
  bitbuffer_reset(&hBitStream->hBitBuf);

  /* init cache */
  hBitStream->CacheWord = hBitStream->BitsInCache = 0;
  hBitStream->ConfigCache = config;
}

/** DeleteBitStream.

    Deletes the in Create Bitstream allocated BitStream and BitBuffer.
*/
void bitstream_delete(HANDLE_BITSTREAM hBitStream) {
  bitbuffer_delete(&hBitStream->hBitBuf);
  free(hBitStream);
}

/**
 * \brief ReadBits Function (forward). This function returns a number of sequential
 *        bits from the input bitstream.
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param numberOfBits  The number of bits to be retrieved. ( (0),1 <= numberOfBits <= 32)
 * \return the requested bits, right aligned
 * \return
 */
#if defined(__clang__) && (__clang_major__ >= 12)
__attribute__((no_sanitize("unsigned-shift-base")))
#endif
uint32_t
bitstream_readBits(HANDLE_BITSTREAM hBitStream, const uint32_t numberOfBits) {
  uint32_t bits = 0;
  int32_t missingBits = (int32_t)numberOfBits - (int32_t)hBitStream->BitsInCache;

  // ASSERT(numberOfBits <= 32);
  if (missingBits > 0) {
    if (missingBits != 32) bits = hBitStream->CacheWord << missingBits;
    hBitStream->CacheWord = bitbuffer_get32(&hBitStream->hBitBuf);
    hBitStream->BitsInCache += CACHE_BITS;
  }

  hBitStream->BitsInCache -= numberOfBits;

  return (bits | (hBitStream->CacheWord >> hBitStream->BitsInCache)) & BitMask[numberOfBits];
}

uint32_t bitstream_readBit(HANDLE_BITSTREAM hBitStream) {
  if (!hBitStream->BitsInCache) {
    hBitStream->CacheWord = bitbuffer_get32(&hBitStream->hBitBuf);
    hBitStream->BitsInCache = CACHE_BITS - 1;
    return hBitStream->CacheWord >> 31;
  }
  hBitStream->BitsInCache--;

  return (hBitStream->CacheWord >> hBitStream->BitsInCache) & 1;
}

/**
 * \brief Read2Bits Function (forward). This function reads 2 sequential
 *        bits from the input bitstream. It is the optimized version
          of bitstream_readBits() for reading 2 bits.
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \return the requested bits, right aligned
 * \return
 */
#if defined(__clang__) && (__clang_major__ >= 12)
__attribute__((no_sanitize("unsigned-shift-base")))
#endif
uint32_t
bitstream_read2Bits(HANDLE_BITSTREAM hBitStream) {
  /*
  ** Version corresponds to optimized bitstream_readBits implementation
  ** calling get32, that keeps read pointer aligned.
  */
  uint32_t bits = 0;
  int32_t missingBits = (int32_t)2 - (int32_t)hBitStream->BitsInCache;
  if (missingBits > 0) {
    bits = hBitStream->CacheWord << missingBits;
    hBitStream->CacheWord = bitbuffer_get32(&hBitStream->hBitBuf);
    hBitStream->BitsInCache += CACHE_BITS;
  }

  hBitStream->BitsInCache -= 2;

  return (bits | (hBitStream->CacheWord >> hBitStream->BitsInCache)) & 0x3;
}

/**
 * \brief ReadBits Function (backward). This function returns a number of sequential bits
 *        from the input bitstream.
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param numberOfBits  The number of bits to be retrieved.
 * \return the requested bits, right aligned
 */
uint32_t bitstream_readBitsBwd(HANDLE_BITSTREAM hBitStream, const uint32_t numberOfBits) {
  const uint32_t validMask = BitMask[numberOfBits];

  if (hBitStream->BitsInCache <= numberOfBits) {
    const int32_t freeBits = (CACHE_BITS - 1) - hBitStream->BitsInCache;

    hBitStream->CacheWord =
        (hBitStream->CacheWord << freeBits) | bitbuffer_getBwd(&hBitStream->hBitBuf, freeBits);
    hBitStream->BitsInCache += freeBits;
  }

  hBitStream->BitsInCache -= numberOfBits;

  return (hBitStream->CacheWord >> hBitStream->BitsInCache) & validMask;
}

/**
 * \brief read an integer value using a varying number of bits from the bitstream
 *
 *        q.v. ISO/IEC FDIS 23003-3  Table 16
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param nBits1  number of bits to read for a small integer value or escape value
 * \param nBits2  number of bits to read for a medium sized integer value or escape value
 * \param nBits3  number of bits to read for a large integer value
 * \return integer value read from bitstream
 */
uint32_t bitstream_readEscapedValue(HANDLE_BITSTREAM hBitStream, uint32_t nBits1, uint32_t nBits2,
                                    uint32_t nBits3) {
  uint32_t value = bitstream_readBits(hBitStream, nBits1);

  if (value == (uint32_t)(1 << nBits1) - 1) {
    uint32_t valueAdd = bitstream_readBits(hBitStream, nBits2);
    value += valueAdd;
    if (valueAdd == (uint32_t)(1 << nBits2) - 1) {
#ifdef UBSAN_FIX
      uint32_t tempDiff = (uint32_t)0xFFFFFFFF - value;
      uint32_t tempValue = bitstream_readBits(hBitStream, nBits3);
      if (tempValue > tempDiff) {
        return (uint32_t)0xFFFFFFFF;
      }
      value += tempValue;
#else
      value += bitstream_readBits(hBitStream, nBits3);
#endif
    }
  }

  return value;
}

/**
 * \brief return a number of bits from the bitBuffer.
 *        You have to know what you do! Cache has to be synchronized before using this
 *        function.
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param numBits The number of bits to be retrieved.
 * \return the requested bits, right aligned
 */
uint32_t bitstream_getBits(HANDLE_BITSTREAM hBitStream, uint32_t numBits) {
  return bitbuffer_get(&hBitStream->hBitBuf, numBits);
}

/**
 * \brief WriteBits Function. This function writes numberOfBits of value into bitstream.
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param value         The data to be written
 * \param numberOfBits  The number of bits to be written
 * \return              Number of bits written
 */
uint8_t bitstream_writeBits(HANDLE_BITSTREAM hBitStream, uint32_t value,
                            const uint32_t numberOfBits) {
  const uint32_t validMask = BitMask[numberOfBits];

  if (hBitStream == nullptr) {
    return numberOfBits;
  }

  if ((hBitStream->BitsInCache + numberOfBits) < CACHE_BITS) {
    hBitStream->BitsInCache += numberOfBits;
    hBitStream->CacheWord = (hBitStream->CacheWord << numberOfBits) | (value & validMask);
  } else {
    /* Put always 32 bits into memory             */
    /* - fill cache's LSBits with MSBits of value */
    /* - store 32 bits in memory using subroutine */
    /* - fill remaining bits into cache's LSBits  */
    /* - upper bits in cache are don't care       */

    /* Compute number of bits to be filled into cache */
    int missing_bits = CACHE_BITS - hBitStream->BitsInCache;
    int remaining_bits = numberOfBits - missing_bits;
    value = value & validMask;
    /* Avoid shift left by 32 positions */
    uint32_t CacheWord = (missing_bits == 32) ? 0 : (hBitStream->CacheWord << missing_bits);
    CacheWord |= (value >> (remaining_bits));
    bitbuffer_put(&hBitStream->hBitBuf, CacheWord, 32);

    hBitStream->CacheWord = value;
    hBitStream->BitsInCache = remaining_bits;
  }

  return numberOfBits;
}

/**
 * \brief WriteBits Function (backward). This function writes numberOfBits of value into bitstream.
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param value         Variable holds data to be written.
 * \param numberOfBits  The number of bits to be written.
 * \return number of bits written
 */
uint8_t bitstream_writeBitsBwd(HANDLE_BITSTREAM hBitStream, uint32_t value,
                               const uint32_t numberOfBits) {
  const uint32_t validMask = BitMask[numberOfBits];

  if ((hBitStream->BitsInCache + numberOfBits) <= CACHE_BITS) {
    hBitStream->BitsInCache += numberOfBits;
    hBitStream->CacheWord = (hBitStream->CacheWord << numberOfBits) | (value & validMask);
  } else {
    bitbuffer_putBwd(&hBitStream->hBitBuf, hBitStream->CacheWord, hBitStream->BitsInCache);
    hBitStream->BitsInCache = numberOfBits;
    hBitStream->CacheWord = (value & validMask);
  }

  return numberOfBits;
}

/**
 * \brief write an integer value using a varying number of bits from the bitstream
 *
 *        q.v. ISO/IEC FDIS 23003-3  Table 16
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param value   the data to be written
 * \param nBits1  number of bits to write for a small integer value or escape value
 * \param nBits2  number of bits to write for a medium sized integer value or escape value
 * \param nBits3  number of bits to write for a large integer value
 * \return number of bits written
 */
uint8_t bitstream_writeEscapedValue(HANDLE_BITSTREAM hBitStream, uint32_t value, uint32_t nBits1,
                                    uint32_t nBits2, uint32_t nBits3) {
  uint8_t nbits = 0;
  uint32_t tmp = (1 << nBits1) - 1;

  if (value < tmp) {
    nbits += bitstream_writeBits(hBitStream, value, nBits1);
  } else {
    nbits += bitstream_writeBits(hBitStream, tmp, nBits1);
    value -= tmp;
    tmp = (1 << nBits2) - 1;

    if (value < tmp) {
      nbits += bitstream_writeBits(hBitStream, value, nBits2);
    } else {
      nbits += bitstream_writeBits(hBitStream, tmp, nBits2);
      value -= tmp;

      nbits += bitstream_writeBits(hBitStream, value, nBits3);
    }
  }

  return nbits;
}

/**
 * \brief SyncCache Function. Clear cache after read forward.
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \return void
 */
void bitstream_syncCache(HANDLE_BITSTREAM hBitStream) {
  if (hBitStream->ConfigCache == BS_READER)
    bitbuffer_pushBack(&hBitStream->hBitBuf, hBitStream->BitsInCache, hBitStream->ConfigCache);
  else if (hBitStream->BitsInCache) /* BS_WRITER */
    bitbuffer_put(&hBitStream->hBitBuf, hBitStream->CacheWord, hBitStream->BitsInCache);

  hBitStream->BitsInCache = 0;
  hBitStream->CacheWord = 0;
}

/**
 * \brief SyncCache Function. Clear cache after read backwards.
 *
 * \param  hBitStream HANDLE_BITSTREAM handle
 * \return void
 */
void bitstream_syncCacheBwd(HANDLE_BITSTREAM hBitStream) {
  if (hBitStream->ConfigCache == BS_READER) {
    bitbuffer_pushForward(&hBitStream->hBitBuf, hBitStream->BitsInCache, hBitStream->ConfigCache);
  } else { /* BS_WRITER */
    bitbuffer_putBwd(&hBitStream->hBitBuf, hBitStream->CacheWord, hBitStream->BitsInCache);
  }

  hBitStream->BitsInCache = 0;
  hBitStream->CacheWord = 0;
}

#ifdef BITCNT_ENABLE
/**
 * \brief Byte Alignment Function.
 *        This function performs the byte_alignment() syntactic function on the input stream,
 *        i.e. some bits will be discarded/padded so that the next bits to be read/written will
 *        be aligned on a byte boundary with respect to the bit position 0.
 *
 * \param  hBitStream HANDLE_BITSTREAM handle
 * \return void
 */
void bitstream_byteAlign(HANDLE_BITSTREAM hBitStream) {
  bitstream_syncCache(hBitStream);
  bitbuffer_byteAlign(&hBitStream->hBitBuf, (uint8_t)hBitStream->ConfigCache);
}
#endif

/**
 * \brief Byte Alignment Function with anchor
 *        This function performs the byte_alignment() syntactic function on the input stream,
 *        i.e. some bits will be discarded so that the next bits to be read/written would be aligned
 *        on a byte boundary with respect to the given alignment anchor.
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param alignmentAnchor bit position to be considered as origin for byte alignment
 * \return void
 */
void bitstream_byteAlign(HANDLE_BITSTREAM hBitStream, uint32_t alignmentAnchor) {
  bitstream_syncCache(hBitStream);
  if (hBitStream->ConfigCache == BS_READER) {
    bitbuffer_pushForward(
        &hBitStream->hBitBuf,
        (8 - (((int32_t)alignmentAnchor - bitbuffer_getValidBits(&hBitStream->hBitBuf)) & 0x07)) &
            0x07,
        hBitStream->ConfigCache);
  } else {
    bitbuffer_put(
        &hBitStream->hBitBuf, 0,
        (8 - ((bitbuffer_getValidBits(&hBitStream->hBitBuf) - (int32_t)alignmentAnchor) & 0x07)) &
            0x07);
  }
}

/**
 * \brief Push Back(Cache) / For / BiDirectional Function.
 *        PushBackCache function ungets a number of bits erroneously read/written by the last Get()
 * call. NB: The number of bits to be stuffed back into the stream may never exceed the number of
 * bits returned by the immediately preceding Get() call.
 *
 *       PushBack function ungets a number of bits (combines cache and bitbuffer indices)
 *       PushFor  function gets a number of bits (combines cache and bitbuffer indices)
 *       PushBiDirectional gets/ungets number of bits as defined in PusBack/For function
 *       NB: The sign of bits is not known, so the function checks direction and calls
 *        appropriate function. (positive sign pushFor, negative sign pushBack )
 *
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param numberOfBits  The number of bits to be pushed back/for.
 * \return void
 */
void bitstream_pushBackCache(HANDLE_BITSTREAM hBitStream, const uint32_t numberOfBits) {
  // ASSERT ((hBitStream->BitsInCache+numberOfBits)<=CACHE_BITS);
  hBitStream->BitsInCache += numberOfBits;
}

void bitstream_pushBack(HANDLE_BITSTREAM hBitStream, const uint32_t numberOfBits) {
  if ((hBitStream->BitsInCache + numberOfBits) < CACHE_BITS &&
      (hBitStream->ConfigCache == BS_READER)) {
    hBitStream->BitsInCache += numberOfBits;
    bitstream_syncCache(hBitStream); /* sync cache to avoid invalid cache */
  } else {
    bitstream_syncCache(hBitStream);
    bitbuffer_pushBack(&hBitStream->hBitBuf, numberOfBits, hBitStream->ConfigCache);
  }
}

void bitstream_pushFor(HANDLE_BITSTREAM hBitStream, const uint32_t numberOfBits) {
  if ((hBitStream->BitsInCache > numberOfBits) && (hBitStream->ConfigCache == BS_READER)) {
    hBitStream->BitsInCache -= numberOfBits;
  } else {
    bitstream_syncCache(hBitStream);
    bitbuffer_pushForward(&hBitStream->hBitBuf, numberOfBits, hBitStream->ConfigCache);
  }
}

void bitstream_pushBiDirectional(HANDLE_BITSTREAM hBitStream, const int32_t numberOfBits) {
  if (numberOfBits >= 0)
    bitstream_pushFor(hBitStream, numberOfBits);
  else
    bitstream_pushBack(hBitStream, -numberOfBits);
}

/**
 * \brief GetValidBits Function.  Clear cache and return valid Bits from Bitbuffer.
 * \param hBitStream HANDLE_BITSTREAM handle
 * \return amount of valid bits that still can be read or were already written. Negative value means
 *         that too many bits were read.
 *
 */
int32_t bitstream_getValidBits(HANDLE_BITSTREAM hBitStream) {
  bitstream_syncCache(hBitStream);
  return bitbuffer_getValidBits(&hBitStream->hBitBuf);
}

/**
 * \brief return amount of unused Bits from Bitbuffer.
 * \param hBitStream HANDLE_BITSTREAM handle
 * \return amount of free bits that still can be written into the bitstream
 */
int32_t bitstream_getFreeBits(HANDLE_BITSTREAM hBitStream) {
  return bitbuffer_getFreeBits(&hBitStream->hBitBuf);
}

#ifdef BITCNT_ENABLE
/**
 * \brief reset bitcounter in bitBuffer to zero.
 * \param hBitStream HANDLE_BITSTREAM handle
 * \return void
 */
void bitstream_resetBitCnt(HANDLE_BITSTREAM hBitStream) {
  bitstream_syncCache(hBitStream);
  bitbuffer_setBitCnt(&hBitStream->hBitBuf, 0);
}

/**
 * \brief set bitcoutner in bitBuffer to given value.
 * \param hBitStream HANDLE_BITSTREAM handle
 * \param value new value to be assigned to the bit counter
 * \return void
 */
void bitstream_setBitCnt(HANDLE_BITSTREAM hBitStream, uint32_t value) {
  bitstream_syncCache(hBitStream);
  bitbuffer_setBitCnt(&hBitStream->hBitBuf, value);
}
/**
 * \brief get bitcounter state from bitBuffer.
 * \param hBitStream HANDLE_BITSTREAM handle
 * \return current bit counter value
 */
int32_t bitstream_getBitCnt(HANDLE_BITSTREAM hBitStream) {
  bitstream_syncCache(hBitStream);
  return bitbuffer_getBitCnt(&hBitStream->hBitBuf);
}
#endif

/**
 * \brief Fill the BitBuffer with a number of input bytes from  external source.
 *        The bytesValid variable returns the number of remaining valid bytes in extern inputBuffer.
 *
 * \param hBitStream  HANDLE_BITSTREAM handle
 * \param inputBuffer Pointer to input buffer with bitstream data.
 * \param bufferSize  Total size of inputBuffer array.
 * \param bytesValid  Input: number of valid bytes in inputBuffer. Output: bytes still left unread
 * in inputBuffer.
 * \return void
 */
void bitstream_feedBuffer(HANDLE_BITSTREAM hBitStream, const uint8_t inputBuffer[],
                          const uint32_t bufferSize, uint32_t* bytesValid) {
  bitstream_syncCache(hBitStream);
  bitbuffer_feed(&hBitStream->hBitBuf, inputBuffer, bufferSize, bytesValid);
}

/**
 * \brief fill destination BitBuffer with a number of bytes from source BitBuffer. The
 *        bytesValid variable returns the number of remaining valid bytes in source BitBuffer.
 *
 * \param hBSDst            HANDLE_BITSTREAM handle to write data into
 * \param hBSSrc            HANDLE_BITSTREAM handle to read data from
 * \param bytesValid        Input: number of valid bytes in inputBuffer. Output: bytes still left
 * unread in inputBuffer.
 * \return void
 */
void bitstream_copyBuffer(HANDLE_BITSTREAM hBSDst, HANDLE_BITSTREAM hBSSrc, uint32_t* bytesValid) {
  bitstream_syncCache(hBSSrc);
  bitbuffer_copy(&hBSDst->hBitBuf, &hBSSrc->hBitBuf, bytesValid);
}

/**
 * \brief fill the outputBuffer with all valid bytes hold in BitBuffer. The WriteBytes
 *        variable returns the number of written Bytes.
 *
 * \param hBitStream    HANDLE_BITSTREAM handle
 * \param outputBuffer  Pointer to output buffer.
 * \param writeBytes    Number of bytes write to output buffer.
 * \return void
 */
void bitstream_fetchBuffer(HANDLE_BITSTREAM hBitStream, uint8_t* outputBuffer,
                           uint32_t* writeBytes) {
  bitstream_syncCache(hBitStream);
  bitbuffer_fetch(&hBitStream->hBitBuf, outputBuffer, writeBytes);
}

#endif
