#include <arm_neon.h>

#include "maxlinelen.h"

// NEON longest-line scanner -- the vectorized counterpart of maxlinelen_scalar.
//
// Unlike line/word/char counting, the longest line is a segmented-max over the
// gaps between newlines, which carries state (the open run length) and branches
// on newline positions -- not a clean reduction. But the key observation makes
// it vectorize well: a 16-byte block that contains NO newline can neither end
// the current line nor start a new one, so its entire counted length simply
// extends the open run. That bulk case -- the common one for long lines, which
// is exactly when -L is interesting -- is handled with a single compare per 16
// bytes. Only blocks that actually contain a newline drop to the per-byte state
// machine, where the segment boundaries (and prefix/maxComplete updates) live.

// One byte through the scalar longest-line state machine. Shared by the
// alignment prologue, the scalar epilogue, and the per-byte slow path for
// 16-byte blocks that contain a newline.
static inline void scalarStep(
    const unsigned char c, LineScan& s, const bool countChars
)
{
  if ( c == '\n' ) {
    // The first line of the range is its prefix: it may join a line left open
    // by the previous chunk, so the merge needs its length on its own.
    if ( !s.hasNewline ) {
      s.prefixLen = s.cur;
      s.hasNewline = true;
    }
    if ( s.cur > s.maxComplete ) s.maxComplete = s.cur;
    s.cur = 0;  // the newline is not counted; start the next line
  } else if ( countChars && ( c & 0xC0 ) == 0x80 ) {
    // A UTF-8 continuation byte continues the current character; in character
    // mode it does not advance the line length.
  } else {
    ++s.cur;
  }
}

void maxLineLen(
    const char* buffer, const usize length, LineScan& s, const bool countChars
)
{
  const u8* tmp = reinterpret_cast<const u8*>( buffer );
  usize processed = 0;

  // Scalar prologue to a 16-byte boundary, threading the carry state so the
  // vector loop starts aligned.
  while ( processed < length && reinterpret_cast<usize>( tmp ) % 16 != 0 ) {
    scalarStep( *tmp, s, countChars );
    ++tmp;
    ++processed;
  }

  const uint8x16_t newline = vdupq_n_u8( static_cast<u8>( '\n' ) );
  const uint8x16_t contMask = vdupq_n_u8( 0xC0 );
  const uint8x16_t contTag = vdupq_n_u8( 0x80 );

  // Main loop: 16 bytes per iteration.
  while ( processed + 16 <= length ) {
    const uint8x16_t v = vld1q_u8( tmp );
    const uint8x16_t isNewline = vceqq_u8( v, newline );

    if ( vmaxvq_u8( isNewline ) == 0 ) {
      // No newline in this block: the whole counted length extends the open run.
      if ( countChars ) {
        // Bytes that are NOT UTF-8 continuation bytes (10xxxxxx) advance the
        // length; continuation bytes belong to a character already counted.
        const uint8x16_t isCont = vceqq_u8( vandq_u8( v, contMask ), contTag );
        const uint8x16_t contOnes = vshrq_n_u8( isCont, 7 );  // 0/1 per lane
        s.cur += 16u - vaddvq_u8( contOnes );
      } else {
        s.cur += 16;
      }
    } else {
      // A newline splits this block into segments: hand it to the per-byte
      // state machine, which records prefix/maxComplete and resets the run.
      for ( int i = 0; i < 16; ++i ) scalarStep( tmp[i], s, countChars );
    }

    tmp += 16;
    processed += 16;
  }

  // Scalar epilogue for the remaining < 16 bytes.
  while ( processed < length ) {
    scalarStep( *tmp, s, countChars );
    ++tmp;
    ++processed;
  }
}
