#pragma once
#include <immintrin.h>

#include "typedef.h"

// Shared helpers for the AVX2 counter implementations (count/chars/words/
// maxlinelen). Header-only and inline so each translation unit gets its own
// copy under -mavx2 with no link-time duplication.

// Horizontally sum the 32 unsigned bytes of an accumulator. _mm256_sad_epu8
// against zero reduces each 8-byte group to a 64-bit sum (max 8*255 = 2040),
// leaving four partial sums to add up.
inline u64 hsumBytes( const __m256i v )
{
  const __m256i sad = _mm256_sad_epu8( v, _mm256_setzero_si256() );
  return static_cast<u64>( _mm256_extract_epi64( sad, 0 ) ) +
         static_cast<u64>( _mm256_extract_epi64( sad, 1 ) ) +
         static_cast<u64>( _mm256_extract_epi64( sad, 2 ) ) +
         static_cast<u64>( _mm256_extract_epi64( sad, 3 ) );
}
