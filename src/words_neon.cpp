#include <arm_neon.h>

#include "words.h"

// NEON word counter -- the vectorized counterpart of words_scalar.cpp. A word
// is a maximal run of non-whitespace, so the count is the number of
// whitespace-to-word transitions: positions where the current byte is
// non-whitespace and the previous byte is whitespace (the byte before the
// buffer is "whitespace" exactly when `inWord` is false). Each 16-byte vector
// computes that transition mask in parallel; the only cross-lane dependency is
// the single "was the previous byte whitespace" bit, threaded through with
// vextq_u8.

// Whitespace per the C locale: ' ' (0x20) plus the contiguous control range
// '\t'..'\r' (0x09..0x0D). Returns 0xFF for whitespace lanes, 0x00 otherwise.
static inline uint8x16_t spaceMask( const uint8x16_t v )
{
  const uint8x16_t isSpace = vceqq_u8( v, vdupq_n_u8( 0x20 ) );
  const uint8x16_t geTab = vcgeq_u8( v, vdupq_n_u8( 0x09 ) );
  const uint8x16_t leCr = vcleq_u8( v, vdupq_n_u8( 0x0D ) );
  return vorrq_u8( isSpace, vandq_u8( geTab, leCr ) );
}

usize words( const char* buffer, const usize length, bool& inWord )
{
  const u8* tmp = reinterpret_cast<const u8*>( buffer );
  usize count = 0;
  usize processedBytes = 0;

  // Scalar prologue to a 16-byte boundary, keeping `inWord` current so the
  // vector loop can seed its carry from it.
  while ( processedBytes < length &&
          ( reinterpret_cast<usize>( tmp ) % 16 != 0 ) ) {
    const bool space = isWordSpace( static_cast<char>( *tmp ) );
    if ( !space && !inWord ) ++count;
    inWord = !space;
    ++tmp;
    ++processedBytes;
  }

  // The carry lane: only the top byte of `prevSpace` is read by the vextq_u8
  // below (it becomes lane 0's "previous" byte). Whitespace-before-buffer holds
  // exactly when we are not currently inside a word.
  uint8x16_t prevSpace = vdupq_n_u8( inWord ? 0x00 : 0xFF );

  // Main loop: 16 bytes per iteration. vceqq/vand produce 0xFF (== -1) at each
  // word start, so vsubq_u8 adds 1 per start straight into a byte accumulator
  // -- no per-iteration horizontal reduction. A lane gains at most 1 per
  // iteration, so we drain into `count` only every 255 iterations before it
  // could overflow.
  while ( processedBytes + 16 <= length ) {
    const usize remIters = ( length - processedBytes ) / 16;
    const usize block = remIters < 255 ? remIters : 255;

    uint8x16_t acc = vdupq_n_u8( 0 );
    for ( usize b = 0; b < block; ++b ) {
      const uint8x16_t v = vld1q_u8( tmp );
      const uint8x16_t sp = spaceMask( v );
      const uint8x16_t nonSpace = vmvnq_u8( sp );
      // prevSp[i] = sp[i-1], with prevSp[0] taken from the carried previous
      // byte.
      const uint8x16_t prevSp = vextq_u8( prevSpace, sp, 15 );
      const uint8x16_t start = vandq_u8( nonSpace, prevSp );
      acc = vsubq_u8( acc, start );
      prevSpace = sp;
      tmp += 16;
      processedBytes += 16;
    }
    count += vaddlvq_u8( acc );  // widen-sum 16 lanes (max 16*255 = 4080)
  }

  // The last vector byte sets the in-word state for the scalar epilogue.
  if ( processedBytes > 0 )
    inWord = !isWordSpace( static_cast<char>( tmp[-1] ) );

  // Scalar epilogue for the remaining < 16 bytes.
  while ( processedBytes < length ) {
    const bool space = isWordSpace( static_cast<char>( *tmp ) );
    if ( !space && !inWord ) ++count;
    inWord = !space;
    ++tmp;
    ++processedBytes;
  }

  return count;
}
