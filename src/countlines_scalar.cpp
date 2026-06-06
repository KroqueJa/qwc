#include "countlines.h"

size_t countLines( const char* buffer, size_t length, char target )
{
  size_t lines = 0;
  for ( size_t i = 0; i < length; ++i )
    if ( buffer[i] == target ) ++lines;
  return lines;
}