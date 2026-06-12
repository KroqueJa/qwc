#include <immintrin.h>

#include "avx2_util.h"
#include "chars.h"

// AVX2 character counter -- the vectorized counterpart of chars_scalar.cpp and
// the x86 sibling of chars_neon.cpp.
//
// A UTF-8 code point is one leading byte followed by zero or more continuation
// bytes (10xxxxxx), so the character count is the total byte count minus the
// number of continuation bytes. Counting continuation bytes is the same shape
// as count_avx2 (count bytes matching a predicate), so this mirrors it: mask
// off the top two bits with one _mm256_and_si256 and compare to 0x80, then
// accumulate. Each byte is classified on its own, so the result is independent
// of where the buffer is split -- no cross-chunk stitching needed.
usize chars( const char* buffer, const usize length )
{
  const __m256i contBits = _mm256_set1_epi8( static_cast<char>( 0xC0 ) );
  const __m256i contTag = _mm256_set1_epi8( static_cast<char>( 0x80 ) );

  usize cont = 0;  // number of UTF-8 continuation bytes
  const char* tmp = buffer;
  usize processedBytes = 0;

  // Align to a 32-byte boundary with a scalar prologue.
  while ( processedBytes < length &&
          ( reinterpret_cast<usize>( tmp ) % 32 != 0 ) ) {
    if ( ( static_cast<unsigned char>( *tmp ) & 0xC0 ) == 0x80 ) ++cont;
    ++tmp;
    ++processedBytes;
  }

  // Main loop: 128 bytes per iteration across 4 byte-accumulators.
  // _mm256_cmpeq_epi8 yields 0xFF (== -1) on a continuation byte, so
  // _mm256_sub_epi8 adds 1 per hit straight into the accumulator -- no
  // per-iteration horizontal reduction. Each lane gains at most 1 per iteration
  // and saturates at 255, so we drain the accumulators only once every 255
  // iterations.
  while ( processedBytes + 128 <= length ) {
    const usize remIters = ( length - processedBytes ) / 128;
    const usize block = remIters < 255 ? remIters : 255;

    __m256i acc0 = _mm256_setzero_si256();
    __m256i acc1 = _mm256_setzero_si256();
    __m256i acc2 = _mm256_setzero_si256();
    __m256i acc3 = _mm256_setzero_si256();

    for ( usize b = 0; b < block; ++b ) {
      const __m256i v0 =
          _mm256_loadu_si256( reinterpret_cast<const __m256i*>( tmp ) );
      const __m256i v1 =
          _mm256_loadu_si256( reinterpret_cast<const __m256i*>( tmp + 32 ) );
      const __m256i v2 =
          _mm256_loadu_si256( reinterpret_cast<const __m256i*>( tmp + 64 ) );
      const __m256i v3 =
          _mm256_loadu_si256( reinterpret_cast<const __m256i*>( tmp + 96 ) );
      acc0 = _mm256_sub_epi8(
          acc0, _mm256_cmpeq_epi8( _mm256_and_si256( v0, contBits ), contTag )
      );
      acc1 = _mm256_sub_epi8(
          acc1, _mm256_cmpeq_epi8( _mm256_and_si256( v1, contBits ), contTag )
      );
      acc2 = _mm256_sub_epi8(
          acc2, _mm256_cmpeq_epi8( _mm256_and_si256( v2, contBits ), contTag )
      );
      acc3 = _mm256_sub_epi8(
          acc3, _mm256_cmpeq_epi8( _mm256_and_si256( v3, contBits ), contTag )
      );
      tmp += 128;
      processedBytes += 128;
    }

    cont += hsumBytes( acc0 );
    cont += hsumBytes( acc1 );
    cont += hsumBytes( acc2 );
    cont += hsumBytes( acc3 );
  }

  // Scalar epilogue for the remaining < 128 bytes.
  while ( processedBytes < length ) {
    if ( ( static_cast<unsigned char>( *tmp ) & 0xC0 ) == 0x80 ) ++cont;
    ++tmp;
    ++processedBytes;
  }

  // Characters = total bytes that are not continuation bytes.
  return length - cont;
}
