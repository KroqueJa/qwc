#include "countlines.h"
#include <arm_neon.h>
#include <stdint.h>

usize countLines( const char* buffer, usize length, char target )
{
  const uint8x16_t vec_target = vdupq_n_u8( (u8)target );

  usize     lines = 0;
  const u8* tmp   = (const u8*)buffer;
  usize     processedBytes = 0;

  // Align to 16-byte boundary with a scalar prologue.
  while ( processedBytes < length && ( (usize)tmp % 16 != 0 ) ) {
    if ( *tmp == (u8)target ) ++lines;
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
    usize remIters = ( length - processedBytes ) / 64;
    usize block    = remIters < 255 ? remIters : 255;

    uint8x16_t acc0 = vdupq_n_u8( 0 );
    uint8x16_t acc1 = vdupq_n_u8( 0 );
    uint8x16_t acc2 = vdupq_n_u8( 0 );
    uint8x16_t acc3 = vdupq_n_u8( 0 );

    for ( usize b = 0; b < block; ++b ) {
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
    if ( *tmp == (u8)target ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  return lines;
}
