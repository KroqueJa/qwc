// DEPRECATED: This vectorized implementation is temporarily disabled pending a rewrite
// to improve the vectorization strategy. Use countlines_scalar.cpp for now.

#include "countlines.h"
#include <arm_neon.h>
#include <stdint.h>

size_t countLines( const char* buffer, size_t length )
{
  uint8x16_t vec_target = vdupq_n_u8( '\n' );
  uint8x16_t chunk1, chunk2, chunk3, chunk4;
  uint8x16_t res1,   res2,   res3,   res4;

  size_t lines = 0;
  const uint8_t* tmp = (const uint8_t*)buffer;
  size_t processedBytes = 0;

  // Align to 16-byte boundary
  while ( processedBytes < length && ( (uintptr_t)tmp % 16 != 0 ) ) {
    if ( *tmp == '\n' ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  // Process in 64-byte chunks (4x 16-byte NEON registers)
  while ( processedBytes + 63 < length ) {
    chunk1 = vld1q_u8( tmp      );
    chunk2 = vld1q_u8( tmp + 16 );
    chunk3 = vld1q_u8( tmp + 32 );
    chunk4 = vld1q_u8( tmp + 48 );

    res1 = vceqq_u8( chunk1, vec_target );
    res2 = vceqq_u8( chunk2, vec_target );
    res3 = vceqq_u8( chunk3, vec_target );
    res4 = vceqq_u8( chunk4, vec_target );

    // vceqq_u8 produces 0xFF on match, 0x00 on no match
    // shift right by 7 to get 0x01/0x00, then horizontal add across lanes
    lines += vaddvq_u8( vshrq_n_u8( res1, 7 ) );
    lines += vaddvq_u8( vshrq_n_u8( res2, 7 ) );
    lines += vaddvq_u8( vshrq_n_u8( res3, 7 ) );
    lines += vaddvq_u8( vshrq_n_u8( res4, 7 ) );

    tmp            += 64;
    processedBytes += 64;
  }

  // Remaining bytes
  while ( processedBytes < length ) {
    if ( *tmp == '\n' ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  return lines;
}