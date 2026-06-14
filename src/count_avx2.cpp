#include <immintrin.h>

#include "avx2_util.h"
#include "countlines.h"

usize count( const char* buffer, const usize length, const char target )
{
  const __m256i vec_target = _mm256_set1_epi8( target );

  usize lines = 0;
  const char* tmp = buffer;
  usize processedBytes = 0;

  // Align to 32-byte boundary with a scalar prologue.
  while ( processedBytes < length &&
          ( reinterpret_cast<usize>( tmp ) % 32 != 0 ) ) {
    if ( *tmp == target ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  // Main loop: process 128 bytes per iteration across 4 byte-accumulators.
  // _mm256_cmpeq_epi8 yields 0xFF (== -1) on a match, so _mm256_sub_epi8 adds
  // 1 per match straight into the accumulator -- no per-iteration horizontal
  // reduction. Each lane can absorb at most 255 hits before it overflows, and
  // every iteration adds at most 1 to a given lane, so we only drain the
  // accumulators into `lines` once every 255 iterations.
  while ( processedBytes + 128 <= length ) {
    const usize remIters = ( length - processedBytes ) / 128;
    const usize block = remIters < 255 ? remIters : 255;

    __m256i acc0 = _mm256_setzero_si256();
    __m256i acc1 = _mm256_setzero_si256();
    __m256i acc2 = _mm256_setzero_si256();
    __m256i acc3 = _mm256_setzero_si256();

    for ( usize b = 0; b < block; ++b ) {
      acc0 = _mm256_sub_epi8(
          acc0,
          _mm256_cmpeq_epi8(
              _mm256_loadu_si256( reinterpret_cast<const __m256i*>( tmp ) ),
              vec_target
          )
      );
      acc1 = _mm256_sub_epi8(
          acc1, _mm256_cmpeq_epi8(
                    _mm256_loadu_si256(
                        reinterpret_cast<const __m256i*>( tmp + 32 )
                    ),
                    vec_target
                )
      );
      acc2 = _mm256_sub_epi8(
          acc2, _mm256_cmpeq_epi8(
                    _mm256_loadu_si256(
                        reinterpret_cast<const __m256i*>( tmp + 64 )
                    ),
                    vec_target
                )
      );
      acc3 = _mm256_sub_epi8(
          acc3, _mm256_cmpeq_epi8(
                    _mm256_loadu_si256(
                        reinterpret_cast<const __m256i*>( tmp + 96 )
                    ),
                    vec_target
                )
      );
      tmp += 128;
      processedBytes += 128;
    }

    lines += hsumBytes( acc0 );
    lines += hsumBytes( acc1 );
    lines += hsumBytes( acc2 );
    lines += hsumBytes( acc3 );
  }

  // Scalar epilogue for the remaining < 128 bytes.
  while ( processedBytes < length ) {
    if ( *tmp == target ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  return lines;
}
