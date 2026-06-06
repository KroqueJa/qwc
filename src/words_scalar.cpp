#include "words.h"

// Naive, scalar word counter. Deliberately unvectorised and unchunked for now;
// SIMD and parallel chunking come as a later step.
usize words( const char* buffer, const usize length, bool& inWord )
{
  usize count = 0;
  for ( usize i = 0; i < length; ++i ) {
    const bool space = isWordSpace( buffer[i] );
    if ( !space && !inWord ) ++count;  // start of a new word
    inWord = !space;
  }
  return count;
}