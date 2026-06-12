#include "words.h"

#include <gtest/gtest.h>

#include <random>
#include <string>

#include "iswprint_table.h"
#include "test_util.h"

using qwctest::refWords;
using qwctest::wordsChunked;
using qwctest::wordsStr;

// Word counting, wc semantics: a word is a maximal run of non-separator
// characters containing at least one PRINTABLE character (a run of only
// control bytes / unprintable code points is "barren" and not counted). In the
// C parameterization separators are ASCII whitespace and printables are
// 0x21-0x7E; in the UTF-8 parameterization the separator set adds the unicode
// whitespace GNU wc honours (probe-pinned, see scripts/probe-wc-words.py) and
// printability follows glibc iswprint via the generated table.

namespace {

// Split the scan at byte `at`, continuing one WordScan across both calls with
// the +-3-byte context window processfile provides -- proves any buffer split
// yields the same answer.
usize wordsSplit( const std::string& s, const usize at, const WordsMode m = {} )
{
  WordScan ws;
  const usize ctxEnd = std::min( s.size(), at + 3 );
  words( s.data(), ctxEnd, 0, at, ws, m );
  const usize back = at < 3 ? at : 3;
  words(
      s.data() + at - back, s.size() - ( at - back ), back,
      s.size() - ( at - back ), ws, m
  );
  wordsFlush( ws );
  return ws.words;
}

const WordsMode kUtf8{ true, true };
const WordsMode kUtf8Posix{ true, false };

}  // namespace

// ---------------------------------------------------------------------------
// The generated iswprint table reproduces known glibc classifications. These
// pin the table's integrity (and document the surprising ones).
// ---------------------------------------------------------------------------
TEST( IswprintTable, KnownClassifications )
{
  EXPECT_TRUE( qwcIswprint( 'a' ) );
  EXPECT_FALSE( qwcIswprint( 0x01 ) );
  EXPECT_FALSE( qwcIswprint( 0x7F ) );    // DEL
  EXPECT_TRUE( qwcIswprint( 0x00A0 ) );   // NBSP
  EXPECT_TRUE( qwcIswprint( 0x200B ) );   // ZWSP: printable per glibc!
  EXPECT_TRUE( qwcIswprint( 0x4E00 ) );   // CJK
  EXPECT_FALSE( qwcIswprint( 0x03A2 ) );  // unassigned hole in Greek
  EXPECT_TRUE( qwcIswprint( 0x1F600 ) );  // emoji
  EXPECT_FALSE( qwcIswprint( 0xD800 ) );  // surrogate
  EXPECT_FALSE( qwcIswprint( 0x110000 ) );
}

// ---------------------------------------------------------------------------
// C parameterization
// ---------------------------------------------------------------------------
TEST( WordsC, Basics )
{
  EXPECT_EQ( wordsStr( "" ), 0u );
  EXPECT_EQ( wordsStr( "hello" ), 1u );
  EXPECT_EQ( wordsStr( "  the quick brown   fox\n" ), 4u );
  EXPECT_EQ( wordsStr( " \t\n\v\f\r " ), 0u );
}

TEST( WordsC, PrintabilityRule )
{
  EXPECT_EQ( wordsStr( " \x01 " ), 0u );   // barren: ctrl only
  EXPECT_EQ( wordsStr( " \x01g " ), 1u );  // rescued by 'g'
  EXPECT_EQ( wordsStr( " \x7F " ), 0u );   // DEL is not printable
  EXPECT_EQ( wordsStr( " \xFF " ), 0u );   // high byte: not printable in C
  EXPECT_EQ(
      wordsStr( "a\xFF"
                "b" ),
      1u
  );  // ...but word-constituent
}

// ---------------------------------------------------------------------------
// UTF-8 parameterization. Non-ASCII characters appear as escaped UTF-8 byte
// sequences so the test data is visible and editor-proof.
// ---------------------------------------------------------------------------
TEST( WordsUtf8, UnicodeSeparators )
{
  EXPECT_EQ(
      wordsStr(
          "a\xC2\xA0"  // U+00A0 NBSP
          "b",
          kUtf8
      ),
      2u
  );
  EXPECT_EQ(
      wordsStr(
          "a\xE3\x80\x80"  // U+3000 ideographic space
          "b",
          kUtf8
      ),
      2u
  );
  EXPECT_EQ(
      wordsStr(
          "a\xE2\x80\x83"  // U+2003 em space
          "b\xE1\x9A\x80"  // U+1680 ogham space
          "c",
          kUtf8
      ),
      3u
  );
  EXPECT_EQ(
      wordsStr(
          "a\xE2\x80\x8B"  // U+200B ZWSP: NOT a separator
          "b",
          kUtf8
      ),
      1u
  );
  EXPECT_EQ(
      wordsStr(
          "a\xE2\x80\xA8"  // U+2028 LS: NOT a separator
          "b",
          kUtf8
      ),
      1u
  );
  EXPECT_EQ(
      wordsStr(
          "a\xC2\x85"  // U+0085 NEL: NOT a separator
          "b",
          kUtf8
      ),
      1u
  );
}

TEST( WordsUtf8, PosixlyCorrectDropsNbspace )
{
  // The probe-pinned nbspace four stop separating: A0, 2007, 202F, 2060.
  EXPECT_EQ(
      wordsStr(
          "a\xC2\xA0"
          "b",
          kUtf8Posix
      ),
      1u
  );
  EXPECT_EQ(
      wordsStr(
          "a\xE2\x80\x87"
          "b",
          kUtf8Posix
      ),
      1u
  );
  EXPECT_EQ(
      wordsStr(
          "a\xE2\x80\xAF"
          "b",
          kUtf8Posix
      ),
      1u
  );
  EXPECT_EQ(
      wordsStr(
          "a\xE2\x81\xA0"
          "b",
          kUtf8Posix
      ),
      1u
  );
  // The base (iswspace) set survives POSIXLY_CORRECT.
  EXPECT_EQ(
      wordsStr(
          "a\xE2\x80\x82"  // U+2002 en space
          "b",
          kUtf8Posix
      ),
      2u
  );
  EXPECT_EQ(
      wordsStr(
          "a\xE3\x80\x80"
          "b",
          kUtf8Posix
      ),
      2u
  );
}

TEST( WordsUtf8, BarrenAndRescuedRuns )
{
  // ZWSP is printable per glibc (probe-confirmed: wc counts it), so a
  // ZWSP-only run IS a word.
  EXPECT_EQ( wordsStr( " \xE2\x80\x8B ", kUtf8 ), 1u );
  EXPECT_EQ( wordsStr( " \x01\x02 ", kUtf8 ), 0u );      // ctrl: barren
  EXPECT_EQ( wordsStr( " \x01x ", kUtf8 ), 1u );         // rescued
  EXPECT_EQ( wordsStr( " \xE4\xB8\x80 ", kUtf8 ), 1u );  // U+4E00 CJK
  EXPECT_EQ( wordsStr( " \xCE\xA2 ", kUtf8 ), 0u );      // U+03A2 unassigned
  EXPECT_EQ( wordsStr( " \xCE\xA2\xCE\xB1 ", kUtf8 ), 1u );  // rescued by alpha
}

TEST( WordsUtf8, InvalidBytesAreWordStuffButNotPrintable )
{
  EXPECT_EQ( wordsStr( " \xFF ", kUtf8 ), 0u );  // invalid-only run: barren
  EXPECT_EQ(
      wordsStr(
          "a\xFF"
          "b",
          kUtf8
      ),
      1u
  );
  EXPECT_EQ( wordsStr( "a\xFF b", kUtf8 ), 2u );
  // Truncated lead at end of input: word-constituent, not printable.
  EXPECT_EQ( wordsStr( " \xE3\x80", kUtf8 ), 0u );
}

TEST( WordsUtf8, MatchesReferenceOnMixedText )
{
  const std::string s =
      "caf\xC3\xA9 au\xE3\x80\x80lait \xE2\x80\x8B \x01 "
      "\xE4\xB8\xAD\xE6\x96\x87 ok\n";
  EXPECT_EQ( wordsStr( s, kUtf8 ), refWords( s, true ) );
}

// ---------------------------------------------------------------------------
// Split-independence: any buffer boundary (even mid-sequence) must agree.
// ---------------------------------------------------------------------------
TEST( WordsSplit, EveryBoundaryAgrees )
{
  const std::string s =
      "a\xE3\x80\x80"
      "b \xE2\x80\x8B c\xC2\xA0也 d \x02 caf\xC3\xA9";
  const usize whole = wordsStr( s, kUtf8 );
  EXPECT_EQ( whole, refWords( s, true ) );
  for ( usize at = 0; at <= s.size(); ++at )
    EXPECT_EQ( wordsSplit( s, at, kUtf8 ), whole ) << "split at " << at;
}

TEST( WordsSplit, ChunkedFeedAgreesAcrossSizes )
{
  std::string s;
  std::mt19937 rng( 42 );
  const char* toks[] = { "word", "\xE3\x80\x80", " ",    "\xC2\xA0",
                         "x",    "\xE2\x80\x8B", "\x01", "\xE4\xB8\x80",
                         "\t",   "\xCE\xA2" };
  for ( int i = 0; i < 2000; ++i ) s += toks[rng() % 10];
  const usize whole = wordsStr( s, kUtf8 );
  EXPECT_EQ( whole, refWords( s, true ) );
  for ( usize chunk: { usize( 1 ), usize( 2 ), usize( 3 ), usize( 7 ),
                       usize( 64 ), usize( 4096 ) } )
    EXPECT_EQ( wordsChunked( s, chunk, kUtf8 ), whole ) << "chunk=" << chunk;
}

TEST( WordsSplit, CModeChunkedAgrees )
{
  std::string s;
  std::mt19937 rng( 7 );
  const char* toks[] = { "alpha", " ", "\x01", "\t", "b", "\xFF" };
  for ( int i = 0; i < 3000; ++i ) s += toks[rng() % 6];
  const usize whole = wordsStr( s );
  EXPECT_EQ( whole, refWords( s ) );
  for ( usize chunk: { usize( 1 ), usize( 3 ), usize( 64 ), usize( 1024 ) } )
    EXPECT_EQ( wordsChunked( s, chunk ), whole ) << "chunk=" << chunk;
}

// ---------------------------------------------------------------------------
// Seam-merge facts reported for the parallel-chunk merge.
// ---------------------------------------------------------------------------
TEST( WordsSeamFacts, LeadingAndTrailingRuns )
{
  WordScan ws;
  const std::string s = "xx yy";
  words( s.data(), s.size(), 0, s.size(), ws, {} );
  EXPECT_TRUE( ws.sawByte );
  EXPECT_TRUE( ws.startsInWord );
  EXPECT_TRUE( ws.leadingEnded );
  EXPECT_TRUE( ws.leadingHasPrintable );
  EXPECT_TRUE( ws.sawSeparator );
  EXPECT_TRUE( ws.inWord );  // "yy" still open
  EXPECT_TRUE( ws.runHasPrintable );
  EXPECT_EQ( ws.words, 1u );  // only "xx" ended
}

TEST( WordsSeamFacts, BarrenLeadingRun )
{
  WordScan ws;
  const std::string s = "\x01\x01 ok";
  words( s.data(), s.size(), 0, s.size(), ws, {} );
  EXPECT_TRUE( ws.startsInWord );
  EXPECT_TRUE( ws.leadingEnded );
  EXPECT_FALSE( ws.leadingHasPrintable );  // the merge may rescue it
  EXPECT_EQ( ws.words, 0u );               // barren run not counted here
}

TEST( WordsSeamFacts, SingleRunSpansWholeRange )
{
  WordScan ws;
  const std::string s = "abcdef";
  words( s.data(), s.size(), 0, s.size(), ws, {} );
  EXPECT_TRUE( ws.startsInWord );
  EXPECT_FALSE( ws.sawSeparator );
  EXPECT_FALSE( ws.leadingEnded );
  EXPECT_TRUE( ws.inWord );
  EXPECT_EQ( ws.words, 0u );
}

TEST( WordsSeamFacts, EmptyRangeReportsNothing )
{
  WordScan ws;
  words( "x", 1, 0, 0, ws, {} );
  EXPECT_FALSE( ws.sawByte );
  EXPECT_EQ( ws.words, 0u );
}
