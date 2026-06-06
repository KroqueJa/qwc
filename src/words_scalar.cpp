#include "words.h"

// Naive, scalar word counter. Deliberately unvectorised and unchunked for now;
// SIMD and parallel chunking come as a later step.
usize words( const char* buffer, const usize length, bool& inWord )
{
  auto isSpace = []( const char c ) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
           c == '\r';
  };

  usize count = 0;
  for ( usize i = 0; i < length; ++i ) {
    const bool space = isSpace( buffer[i] );
    if ( !space && !inWord ) ++count;  // start of a new word
    inWord = !space;
  }
  return count;
}