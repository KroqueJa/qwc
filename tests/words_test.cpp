#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "test_util.h"
#include "words.h"

using qwctest::refWords;
using qwctest::wordsChunked;
using qwctest::wordsStr;

// ---------------------------------------------------------------------------
// Basic / edge cases. A "word" is a maximal run of non-whitespace, matching
// `wc -w`.
// ---------------------------------------------------------------------------
TEST( Words, EmptyBuffer )
{
  EXPECT_EQ( wordsStr( "" ), 0u );
}

TEST( Words, ZeroLengthIgnoresPointer )
{
  bool inWord = false;
  const char* garbage = "abc def";
  EXPECT_EQ( words( garbage, 0, inWord ), 0u );
  EXPECT_FALSE( inWord );
}

TEST( Words, OnlyWhitespace )
{
  EXPECT_EQ( wordsStr( " \t\n\v\f\r " ), 0u );
}

TEST( Words, SingleWord )
{
  EXPECT_EQ( wordsStr( "hello" ), 1u );
}

TEST( Words, TwoWordsOneSpace )
{
  EXPECT_EQ( wordsStr( "hello world" ), 2u );
}

TEST( Words, LeadingAndTrailingWhitespace )
{
  EXPECT_EQ( wordsStr( "   hello world   " ), 2u );
}

TEST( Words, MultipleSpacesBetweenWords )
{
  EXPECT_EQ( wordsStr( "a    b\t\tc\n\nd" ), 4u );
}

TEST( Words, AllWhitespaceKindsSeparate )
{
  // Each C-locale whitespace byte must act as a separator.
  EXPECT_EQ( wordsStr( "a b\tc\nd\ve\ff\rg" ), 7u );
}

TEST( Words, NonNewlineBytesAreWords )
{
  // Punctuation and high bytes are non-whitespace, so they count as words.
  EXPECT_EQ( wordsStr( ",.;! @# " ), 2u );
}

TEST( Words, EmbeddedNulIsNotWhitespace )
{
  const char buf[] = { 'a', '\0', 'b', ' ', 'c' };  // "a\0b" is one word
  bool inWord = false;
  EXPECT_EQ( words( buf, sizeof( buf ), inWord ), 2u );
}

// ---------------------------------------------------------------------------
// In-word carry: the same data fed in pieces, carrying state, must match a
// single-shot scan. This is the property the per-thread streaming relies on.
// ---------------------------------------------------------------------------
TEST( Words, CarryAcrossBuffersMatchesWhole )
{
  const std::string s = "the quick  brown\tfox jumps\nover the lazy dog";
  const usize expected = refWords( s );
  ASSERT_EQ( wordsStr( s ), expected );
  for ( size_t chunk : { size_t( 1 ), size_t( 2 ), size_t( 3 ), size_t( 5 ),
                         size_t( 7 ), size_t( 8 ), size_t( 16 ) } )
    EXPECT_EQ( wordsChunked( s, chunk ), expected ) << "chunk=" << chunk;
}

TEST( Words, SplitInsideAWordDoesNotDoubleCount )
{
  // "wordword" is a single word; splitting it anywhere must still yield 1.
  const std::string s = "wordword";
  for ( size_t chunk = 1; chunk <= s.size(); ++chunk )
    EXPECT_EQ( wordsChunked( s, chunk ), 1u ) << "chunk=" << chunk;
}

TEST( Words, CarryReportsTrailingWordState )
{
  bool inWord = false;
  EXPECT_EQ( words( "ab", 2, inWord ), 1u );
  EXPECT_TRUE( inWord );  // ended mid-word
  // Continuation in a second buffer must not be counted as a new word.
  EXPECT_EQ( words( "cd ef", 5, inWord ), 1u );  // only "ef" is new
}

// ---------------------------------------------------------------------------
// Randomized differential test against the reference, including random splits.
// ---------------------------------------------------------------------------
TEST( Words, FuzzAgainstReference )
{
  std::mt19937_64 rng( 0x5EED );
  // A small alphabet rich in whitespace makes runs and boundaries dense.
  const std::string alphabet = "ab \t\n c";
  std::uniform_int_distribution<size_t> lenDist( 0, 3000 );
  std::uniform_int_distribution<size_t> charDist( 0, alphabet.size() - 1 );

  for ( int iter = 0; iter < 2000; ++iter ) {
    const size_t len = lenDist( rng );
    std::string s( len, ' ' );
    for ( size_t i = 0; i < len; ++i ) s[i] = alphabet[charDist( rng )];

    const usize expected = refWords( s );
    EXPECT_EQ( wordsStr( s ), expected ) << "iter=" << iter << " len=" << len;

    // Split at a random point and verify the carry stitches the halves.
    std::uniform_int_distribution<size_t> splitDist( 0, len );
    const size_t chunk = splitDist( rng ) + 1;
    EXPECT_EQ( wordsChunked( s, chunk ), expected )
        << "iter=" << iter << " len=" << len << " chunk=" << chunk;
  }
}
