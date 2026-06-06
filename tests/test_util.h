#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "countlines.h"
#include "typedef.h"

namespace wcltest {

// Independent, obviously-correct reference implementation of `count`, used as
// the oracle for the SIMD/scalar implementation under test. Compares at the
// byte level so it is immune to plain-char signedness differences.
inline usize refCount( const char* buffer, usize length, char target )
{
  const auto t = static_cast<unsigned char>( target );
  usize n = 0;
  for ( usize i = 0; i < length; ++i )
    if ( static_cast<unsigned char>( buffer[i] ) == t ) ++n;
  return n;
}

inline usize refCount( const std::string& s, char target = '\n' )
{
  return refCount( s.data(), s.size(), target );
}

inline usize countStr( const std::string& s, char target = '\n' )
{
  return count( s.data(), s.size(), target );
}

}  // namespace wcltest
