#include <gtest/gtest.h>

#include <string>

#include "countlines.h"

namespace {

usize countStr( const std::string& s, char target = '\n' )
{
  return count( s.data(), s.size(), target );
}

TEST( Count, EmptyBuffer )
{
  EXPECT_EQ( countStr( "" ), 0u );
}

TEST( Count, NoTarget )
{
  EXPECT_EQ( countStr( "abcdef" ), 0u );
}

TEST( Count, SingleNewline )
{
  EXPECT_EQ( countStr( "\n" ), 1u );
}

TEST( Count, CountsNewlines )
{
  EXPECT_EQ( countStr( "a\nb\nc\n" ), 3u );
}

TEST( Count, ConsecutiveNewlines )
{
  EXPECT_EQ( countStr( "\n\n\n" ), 3u );
}

TEST( Count, NoTrailingNewline )
{
  EXPECT_EQ( countStr( "a\nb\nc" ), 2u );
}

TEST( Count, CustomTarget )
{
  EXPECT_EQ( countStr( "a,b,c,d", ',' ), 3u );
}

TEST( Count, LongBufferCrossesSimdBoundary )
{
  // Long enough to exercise the SIMD main loop plus a scalar tail.
  std::string s( 1000, 'x' );
  for ( int i = 0; i < 1000; i += 10 ) s[i] = '\n';
  EXPECT_EQ( countStr( s ), 100u );
}

}  // namespace