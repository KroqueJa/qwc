#include "countlines.h"

usize count( const char* buffer, const usize length, const char target )
{
  usize lines = 0;
  for ( usize i = 0; i < length; ++i )
    if ( buffer[i] == target ) ++lines;
  return lines;
}