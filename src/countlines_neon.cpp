#include "countlines.h"
#include <arm_neon.h>
#include <stdint.h>

size_t countLines( const char* buffer, size_t length, char target )
{
  const uint8x16_t vec_target = vdupq_n_u8( (uint8_t)target );

  size_t         lines = 0;
  const uint8_t* tmp   = (const uint8_t*)buffer;
  size_t         processedBytes = 0;

  // Align to 16-byte boundary with a scalar prologue.
  while ( processedBytes < length && ( (uintptr_t)tmp % 16 != 0 ) ) {
    if ( *tmp == (uint8_t)target ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  // Main loop: process 64 bytes per iteration across 4 byte-accumulators.
  // vceqq_u8 yields 0xFF (== -1) on a match, so vsubq_u8 adds 1 per match
  // straight into the accumulator -- no per-iteration horizontal reduction.
  // Each lane can absorb at most 255 hits before it overflows, and every
  // iteration adds at most 1 to a given lane, so we only need to drain the
  // accumulators into `lines` once every 255 iterations.
  while ( processedBytes + 64 <= length ) {
    size_t remIters = ( length - processedBytes ) / 64;
    size_t block    = remIters < 255 ? remIters : 255;

    uint8x16_t acc0 = vdupq_n_u8( 0 );
    uint8x16_t acc1 = vdupq_n_u8( 0 );
    uint8x16_t acc2 = vdupq_n_u8( 0 );
    uint8x16_t acc3 = vdupq_n_u8( 0 );

    for ( size_t b = 0; b < block; ++b ) {
      acc0 = vsubq_u8( acc0, vceqq_u8( vld1q_u8( tmp      ), vec_target ) );
      acc1 = vsubq_u8( acc1, vceqq_u8( vld1q_u8( tmp + 16 ), vec_target ) );
      acc2 = vsubq_u8( acc2, vceqq_u8( vld1q_u8( tmp + 32 ), vec_target ) );
      acc3 = vsubq_u8( acc3, vceqq_u8( vld1q_u8( tmp + 48 ), vec_target ) );
      tmp            += 64;
      processedBytes += 64;
    }

    // vaddlvq_u8 widens to u16 while summing all 16 lanes (max 16*255=4080).
    lines += vaddlvq_u8( acc0 );
    lines += vaddlvq_u8( acc1 );
    lines += vaddlvq_u8( acc2 );
    lines += vaddlvq_u8( acc3 );
  }

  // Scalar epilogue for the remaining < 64 bytes.
  while ( processedBytes < length ) {
    if ( *tmp == (uint8_t)target ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  return lines;
}
