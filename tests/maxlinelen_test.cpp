#include <gtest/gtest.h>

#include <random>
#include <string>

#include "maxlinelen.h"
#include "test_util.h"

using wcltest::maxLineLenChunked;
using wcltest::maxLineLenStr;
using wcltest::refMaxLineLen;

// The longest-line length, like `wc -L`: the most bytes between newlines, with
// the newline itself excluded. Only newline-terminated lines count -- a trailing
// run with no final newline is ignored (matching macOS/BSD wc).

// ---------------------------------------------------------------------------
// Basic / edge cases
// ---------------------------------------------------------------------------
TEST( MaxLineLen, EmptyBuffer )
{
  EXPECT_EQ( maxLineLenStr( "" ), 0u );
}

TEST( MaxLineLen, ZeroLengthIgnoresPointer )
{
  LineScan ls;
  const char* garbage = "abcdef";
  maxLineLen( garbage, 0, ls );
  EXPECT_EQ( ls.maxComplete, 0u );
}

TEST( MaxLineLen, SingleLineNoNewlineNotCounted )
{
  // No terminating newline -> wc -L reports 0, not 4.
  EXPECT_EQ( maxLineLenStr( "abcd" ), 0u );
}

TEST( MaxLineLen, TrailingNewlineExcluded )
{
  EXPECT_EQ( maxLineLenStr( "abcd\n" ), 4u );
}

TEST( MaxLineLen, BlankLineIsZero )
{
  EXPECT_EQ( maxLineLenStr( "\n" ), 0u );
  EXPECT_EQ( maxLineLenStr( "\n\n\n" ), 0u );
}

TEST( MaxLineLen, LongestAmongSeveral )
{
  EXPECT_EQ( maxLineLenStr( "ab\nabcd\nx\n" ), 4u );
}

TEST( MaxLineLen, UnterminatedFinalLineIgnored )
{
  // "ccccc" has no trailing newline, so the longest *terminated* line is "bb".
  EXPECT_EQ( maxLineLenStr( "a\nbb\nccccc" ), 2u );
}

TEST( MaxLineLen, TabsAndControlBytesCountAsOne )
{
  // No tab-stop expansion: "a\tb" is 3 bytes, "\a" (bell) + x is 2.
  EXPECT_EQ( maxLineLenStr( "a\tb\n" ), 3u );
  EXPECT_EQ( maxLineLenStr( "\ax\n" ), 2u );
}

TEST( MaxLineLen, CarriageReturnCounts )
{
  // CR is an ordinary byte here; "a\r" before the newline is length 2.
  EXPECT_EQ( maxLineLenStr( "a\r\n" ), 2u );
}

TEST( MaxLineLen, EmbeddedNulCounts )
{
  const char buf[] = { 'a', '\0', 'b', '\n' };  // line "a\0b" -> 3
  LineScan ls;
  maxLineLen( buf, sizeof( buf ), ls );
  EXPECT_EQ( ls.maxComplete, 3u );
}

// ---------------------------------------------------------------------------
// Streaming carry: the same data fed in pieces, carrying state, must match a
// single-shot scan. This is the property the per-thread read loop relies on.
// ---------------------------------------------------------------------------
TEST( MaxLineLen, CarryAcrossBuffersMatchesWhole )
{
  const std::string s = "short\nmuch longer line here\ntiny\n";
  const usize expected = refMaxLineLen( s );
  ASSERT_EQ( maxLineLenStr( s ), expected );
  for ( size_t chunk : { size_t( 1 ), size_t( 2 ), size_t( 3 ), size_t( 5 ),
                         size_t( 7 ), size_t( 8 ), size_t( 16 ) } )
    EXPECT_EQ( maxLineLenChunked( s, chunk ), expected ) << "chunk=" << chunk;
}

TEST( MaxLineLen, SplitInsideLongLineDoesNotMiscount )
{
  // One long (terminated) line split anywhere must still report its full length.
  const std::string s = std::string( 50, 'x' ) + "\n";
  for ( size_t chunk = 1; chunk <= s.size(); ++chunk )
    EXPECT_EQ( maxLineLenChunked( s, chunk ), 50u ) << "chunk=" << chunk;
}

// ---------------------------------------------------------------------------
// Randomized differential test against the reference, including random splits.
// ---------------------------------------------------------------------------
TEST( MaxLineLen, FuzzAgainstReference )
{
  std::mt19937_64 rng( 0x1EAF );
  // An alphabet rich in newlines makes lines short and boundaries dense.
  const std::string alphabet = "ab\n\ncd\n";
  std::uniform_int_distribution<size_t> lenDist( 0, 3000 );
  std::uniform_int_distribution<size_t> charDist( 0, alphabet.size() - 1 );

  for ( int iter = 0; iter < 2000; ++iter ) {
    const size_t len = lenDist( rng );
    std::string s( len, ' ' );
    for ( size_t i = 0; i < len; ++i ) s[i] = alphabet[charDist( rng )];

    const usize expected = refMaxLineLen( s );
    EXPECT_EQ( maxLineLenStr( s ), expected )
        << "iter=" << iter << " len=" << len;

    std::uniform_int_distribution<size_t> splitDist( 1, len + 1 );
    EXPECT_EQ( maxLineLenChunked( s, splitDist( rng ) ), expected )
        << "iter=" << iter << " len=" << len;
  }
}
