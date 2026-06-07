#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "chars.h"
#include "countlines.h"
#include "maxlinelen.h"
#include "typedef.h"
#include "words.h"

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

// Independent reference for `words`. Deliberately structured differently from
// the implementation under test -- it tokenizes by skipping whitespace then
// skipping a word, rather than counting whitespace->word transitions -- so a
// shared bug is unlikely. Whitespace is the C-locale set.
inline bool refIsSpace( unsigned char c )
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
         c == '\r';
}

inline usize refWords( const std::string& s )
{
  usize n = 0;
  size_t i = 0;
  while ( i < s.size() ) {
    while ( i < s.size() && refIsSpace( static_cast<unsigned char>( s[i] ) ) )
      ++i;
    if ( i >= s.size() ) break;
    ++n;  // start of a word
    while ( i < s.size() && !refIsSpace( static_cast<unsigned char>( s[i] ) ) )
      ++i;
  }
  return n;
}

// Run `words` over a whole string in one shot (fresh in-word state).
inline usize wordsStr( const std::string& s )
{
  bool inWord = false;
  return words( s.data(), s.size(), inWord );
}

// Independent reference for `chars` (UTF-8 code points). Structured differently
// from the implementation under test: it walks the leading byte of each sequence
// and skips the continuation bytes, rather than counting non-continuation bytes,
// so a shared bug is unlikely. For well-formed UTF-8 it equals `wc -m`.
inline usize refChars( const std::string& s )
{
  usize n = 0;
  size_t i = 0;
  while ( i < s.size() ) {
    const auto b = static_cast<unsigned char>( s[i] );
    size_t len = 1;
    if ( ( b & 0x80 ) == 0x00 ) len = 1;        // 0xxxxxxx
    else if ( ( b & 0xE0 ) == 0xC0 ) len = 2;   // 110xxxxx
    else if ( ( b & 0xF0 ) == 0xE0 ) len = 3;   // 1110xxxx
    else if ( ( b & 0xF8 ) == 0xF0 ) len = 4;   // 11110xxx
    i += len;
    ++n;
  }
  return n;
}

// Run `chars` over a whole string in one shot.
inline usize charsStr( const std::string& s )
{
  return chars( s.data(), s.size() );
}

// Run `chars` over the string fed in fixed-size pieces, the way a single thread
// streams successive read buffers. Because each byte is classified on its own,
// this must agree with charsStr regardless of where the splits land.
inline usize charsChunked( const std::string& s, size_t chunk )
{
  usize total = 0;
  for ( size_t i = 0; i < s.size(); i += chunk )
    total += chars( s.data() + i, std::min( chunk, s.size() - i ) );
  return total;
}

// Independent reference for the longest-line length (`wc -L`). Structured by
// splitting on newlines and measuring each segment's byte length, rather than
// the running-counter approach the implementation uses, so a shared bug is
// unlikely. The newline is excluded, and -- matching `wc -L` -- a trailing run
// with no final newline is not a counted line (the bytes after the last newline
// are intentionally ignored).
inline usize refMaxLineLen( const std::string& s )
{
  usize best = 0;
  size_t start = 0;
  for ( size_t i = 0; i < s.size(); ++i ) {
    if ( s[i] == '\n' ) {
      best = std::max( best, i - start );  // line [start, i), newline excluded
      start = i + 1;
    }
  }
  return best;
}

// Run `maxLineLen` over a whole string in one shot. The answer is the longest
// newline-terminated line; the trailing open run (`ls.cur`) is dropped.
inline usize maxLineLenStr( const std::string& s )
{
  LineScan ls;
  maxLineLen( s.data(), s.size(), ls );
  return ls.maxComplete;
}

// Run `maxLineLen` over the string fed in fixed-size pieces, carrying state
// across them, the way a single thread streams successive read buffers within
// one chunk. Must agree with maxLineLenStr regardless of where splits land.
inline usize maxLineLenChunked( const std::string& s, size_t chunk )
{
  LineScan ls;
  for ( size_t i = 0; i < s.size(); i += chunk )
    maxLineLen( s.data() + i, std::min( chunk, s.size() - i ), ls );
  return ls.maxComplete;
}

// Run `words` feeding the string in fixed-size pieces, carrying in-word state
// across them -- the way a single thread streams successive read buffers. Must
// agree with wordsStr/refWords regardless of where the splits land.
inline usize wordsChunked( const std::string& s, size_t chunk )
{
  bool inWord = false;
  usize total = 0;
  for ( size_t i = 0; i < s.size(); i += chunk )
    total += words( s.data() + i, std::min( chunk, s.size() - i ), inWord );
  return total;
}

}  // namespace wcltest
