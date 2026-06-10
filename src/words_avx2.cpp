#include <immintrin.h>

#include "avx2_util.h"
#include "words.h"

// AVX2 word counter -- the vectorized counterpart of words_scalar.cpp and the
// x86 sibling of words_neon.cpp. A word is a maximal run of non-whitespace, so
// the count is the number of whitespace-to-word transitions: positions where
// the current byte is non-whitespace and the previous byte is whitespace (the
// byte before the buffer is "whitespace" exactly when `inWord` is false). Each
// 32-byte vector computes that transition mask in parallel; the only cross-lane
// dependency is the single "was the previous byte whitespace" bit, threaded
// through with the permute2x128 + alignr carry below.

// Whitespace per the C locale: ' ' (0x20) plus the contiguous control range
// '\t'..'\r' (0x09..0x0D). Returns 0xFF for whitespace lanes, 0x00 otherwise.
// AVX2 has only signed byte compares, but every whitespace value lies in
// [0x09, 0x20] -- well inside the positive signed range -- and any byte >= 0x80
// (e.g. a UTF-8 lead/continuation byte) is signed-negative, so it falls below
// 0x09 and is never misclassified as whitespace. So signed compares suffice.
static inline __m256i spaceMask( const __m256i v )
{
  const __m256i isSpace = _mm256_cmpeq_epi8( v, _mm256_set1_epi8( 0x20 ) );
  // 0x09 <= v <= 0x0D  ==  (v > 0x08) & (v < 0x0E), as signed compares.
  const __m256i geTab = _mm256_cmpgt_epi8( v, _mm256_set1_epi8( 0x08 ) );
  const __m256i leCr = _mm256_cmpgt_epi8( _mm256_set1_epi8( 0x0E ), v );
  return _mm256_or_si256( isSpace, _mm256_and_si256( geTab, leCr ) );
}

usize words( const char* buffer, const usize length, bool& inWord )
{
  const u8* tmp = reinterpret_cast<const u8*>( buffer );
  usize count = 0;
  usize processedBytes = 0;

  // Scalar prologue to a 32-byte boundary, keeping `inWord` current so the
  // vector loop can seed its carry from it.
  while ( processedBytes < length &&
          ( reinterpret_cast<usize>( tmp ) % 32 != 0 ) ) {
    const bool space = isWordSpace( static_cast<char>( *tmp ) );
    if ( !space && !inWord ) ++count;
    inWord = !space;
    ++tmp;
    ++processedBytes;
  }

  // The carry: only the top byte (lane 31) of `prevSpace` is ever read -- it
  // becomes lane 0's "previous" byte for the next block. Seed it from `inWord`;
  // whitespace-before-buffer holds exactly when we are not inside a word.
  __m256i prevSpace = _mm256_set1_epi8( inWord ? 0x00 : static_cast<char>( 0xFF ) );

  // Main loop: 32 bytes per iteration. The word-start mask is 0xFF (== -1) at
  // each start, so _mm256_sub_epi8 adds 1 per start straight into a byte
  // accumulator -- no per-iteration horizontal reduction. A lane gains at most 1
  // per iteration, so we drain into `count` only every 255 iterations before it
  // could overflow.
  while ( processedBytes + 32 <= length ) {
    const usize remIters = ( length - processedBytes ) / 32;
    const usize block = remIters < 255 ? remIters : 255;

    __m256i acc = _mm256_setzero_si256();
    for ( usize b = 0; b < block; ++b ) {
      const __m256i v =
          _mm256_loadu_si256( reinterpret_cast<const __m256i*>( tmp ) );
      const __m256i sp = spaceMask( v );

      // prevSp[i] = sp[i-1], a 1-byte cross-lane shift. alignr only shifts
      // within each 128-bit half, so first build `cross` whose low half is the
      // previous block's high half (its byte 15 is prevSpace[31], the carry for
      // lane 0) and whose high half is sp's low half (its byte 15 is sp[15], the
      // carry across the 128-bit boundary). alignr(sp, cross, 15) then yields the
      // full 1-byte shift with prevSp[0] = prevSpace[31].
      const __m256i cross = _mm256_permute2x128_si256( prevSpace, sp, 0x21 );
      const __m256i prevSp = _mm256_alignr_epi8( sp, cross, 15 );

      // start = nonSpace & prevSp = (~sp) & prevSp.
      const __m256i start = _mm256_andnot_si256( sp, prevSp );
      acc = _mm256_sub_epi8( acc, start );
      prevSpace = sp;
      tmp += 32;
      processedBytes += 32;
    }
    count += hsumBytes( acc );  // sum 32 lanes (max 32*255 = 8160)
  }

  // The last vector byte sets the in-word state for the scalar epilogue.
  if ( processedBytes > 0 )
    inWord = !isWordSpace( static_cast<char>( tmp[-1] ) );

  // Scalar epilogue for the remaining < 32 bytes.
  while ( processedBytes < length ) {
    const bool space = isWordSpace( static_cast<char>( *tmp ) );
    if ( !space && !inWord ) ++count;
    inWord = !space;
    ++tmp;
    ++processedBytes;
  }

  return count;
}
