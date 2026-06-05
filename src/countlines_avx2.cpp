// DEPRECATED: This vectorized implementation is temporarily disabled pending a rewrite
// to improve the vectorization strategy. Use countlines_scalar.cpp for now.

#include "countlines.h"
#include <immintrin.h>
#include <stdint.h>

size_t countLines( const char* buffer, size_t length )
{
  __m256i vec_target = _mm256_set1_epi8( '\n' );
  __m256i chunk1, chunk2, chunk3, chunk4;
  __m256i res1,   res2,   res3,   res4;
  int     mask1,  mask2,  mask3,  mask4;

  size_t lines = 0;
  const char* tmp = buffer;
  size_t processedBytes = 0;

  while ( processedBytes < length && ( (uintptr_t)tmp % 32 != 0 ) ) {
    if ( *tmp == '\n' ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  while ( processedBytes + 127 < length ) {
    chunk1 = _mm256_loadu_si256( (__m256i*)tmp        );
    chunk2 = _mm256_loadu_si256( (__m256i*)( tmp + 32 ) );
    chunk3 = _mm256_loadu_si256( (__m256i*)( tmp + 64 ) );
    chunk4 = _mm256_loadu_si256( (__m256i*)( tmp + 96 ) );
    res1   = _mm256_cmpeq_epi8( chunk1, vec_target );
    res2   = _mm256_cmpeq_epi8( chunk2, vec_target );
    res3   = _mm256_cmpeq_epi8( chunk3, vec_target );
    res4   = _mm256_cmpeq_epi8( chunk4, vec_target );
    mask1  = _mm256_movemask_epi8( res1 );
    mask2  = _mm256_movemask_epi8( res2 );
    mask3  = _mm256_movemask_epi8( res3 );
    mask4  = _mm256_movemask_epi8( res4 );
    lines += _mm_popcnt_u32( mask1 ) + _mm_popcnt_u32( mask2 )
           + _mm_popcnt_u32( mask3 ) + _mm_popcnt_u32( mask4 );
    tmp            += 128;
    processedBytes += 128;
  }

  while ( processedBytes < length ) {
    if ( *tmp == '\n' ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  return lines;
}