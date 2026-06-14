/*
 * Copyright Ji Krochmal 2026
 */
#include "chars.h"

// Naive, scalar character counter. Like words_scalar it is unvectorised by
// hand; the per-byte test is simple enough that the compiler autovectorises it
// at -O3, and the parallel chunking in processFile supplies the rest of the
// throughput.
usize chars( const char* buffer, const usize length )
{
  usize count = 0;
  for ( usize i = 0; i < length; ++i )
    // Count every byte that is not a UTF-8 continuation byte (10xxxxxx).
    if ( ( static_cast<unsigned char>( buffer[i] ) & 0xC0 ) != 0x80 ) ++count;
  return count;
}
