#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "processfile.h"
#include "test_util.h"

using qwctest::pfBytes;
using qwctest::pfChars;
using qwctest::pfLines;
using qwctest::pfMaxLine;
using qwctest::pfTarget;
using qwctest::pfWords;
using qwctest::refChars;
using qwctest::refCount;
using qwctest::refMaxLineLen;
using qwctest::refWords;

namespace {

// RAII temp file seeded with arbitrary bytes. Uses mkstemp so concurrent test
// processes don't collide.
class TempFile
{
 public:
  explicit TempFile( const std::string& contents )
  {
    char tmpl[] = "/tmp/qwc_pf_test_XXXXXX";
    int fd = mkstemp( tmpl );
    if ( fd < 0 ) std::abort();
    path_ = tmpl;
    if ( !contents.empty() ) {
      const ssize_t w = ::write( fd, contents.data(), contents.size() );
      if ( w != static_cast<ssize_t>( contents.size() ) ) std::abort();
    }
    ::close( fd );
  }

  ~TempFile() { ::unlink( path_.c_str() ); }

  TempFile( const TempFile& ) = delete;
  TempFile& operator=( const TempFile& ) = delete;

  const char* path() const { return path_.c_str(); }

 private:
  std::string path_;
};

std::string makePattern( size_t len, size_t every, char target = '\n' )
{
  std::string s( len, 'x' );
  for ( size_t i = 0; i < len; i += every ) s[i] = target;
  return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// Line counting (-l): counts newline bytes.
// ---------------------------------------------------------------------------
TEST( ProcessFileLines, EmptyFile )
{
  TempFile f( "" );
  EXPECT_EQ( pfLines( f.path() ), 0u );
}

TEST( ProcessFileLines, SingleNewline )
{
  TempFile f( "\n" );
  EXPECT_EQ( pfLines( f.path() ), 1u );
}

TEST( ProcessFileLines, SmallKnownCount )
{
  TempFile f( "a\nb\nc\n" );
  EXPECT_EQ( pfLines( f.path() ), 3u );
}

TEST( ProcessFileLines, NoTrailingNewline )
{
  TempFile f( "a\nb\nc" );
  EXPECT_EQ( pfLines( f.path() ), 2u );
}

// ---------------------------------------------------------------------------
// --char: count occurrences of an arbitrary byte (the qwc extension).
// ---------------------------------------------------------------------------
TEST( ProcessFileTarget, CustomTarget )
{
  TempFile f( "a,b,c,d,e" );
  EXPECT_EQ( pfTarget( f.path(), ',' ), 4u );
}

// ---------------------------------------------------------------------------
// Byte counting (-c): the file size from fstat, no scan.
// ---------------------------------------------------------------------------
TEST( ProcessFileBytes, CountsFileSize )
{
  TempFile f( "hello\nworld" );  // 11 bytes
  EXPECT_EQ( pfBytes( f.path() ), 11u );
}

TEST( ProcessFileBytes, EmptyFileIsZero )
{
  TempFile f( "" );
  EXPECT_EQ( pfBytes( f.path() ), 0u );
}

TEST( ProcessFileBytes, LargeFileSize )
{
  const std::string content = makePattern( 5 * 1024 * 1024, 100 );
  TempFile f( content );
  EXPECT_EQ( pfBytes( f.path(), 256 * 1024 ), content.size() );
}

// ---------------------------------------------------------------------------
// Word counting (-w, like `wc -w`).
// ---------------------------------------------------------------------------
TEST( ProcessFileWords, EmptyFile )
{
  TempFile f( "" );
  EXPECT_EQ( pfWords( f.path() ), 0u );
}

TEST( ProcessFileWords, OnlyWhitespace )
{
  TempFile f( "   \n\t \n" );
  EXPECT_EQ( pfWords( f.path() ), 0u );
}

TEST( ProcessFileWords, SmallKnownCount )
{
  TempFile f( "  the quick brown   fox\n" );
  EXPECT_EQ( pfWords( f.path() ), 4u );
}

// The critical chunk-stitching test: a small bytesPerThread forces many chunks
// so word boundaries fall across splits. The merge must neither drop a word nor
// double-count one straddling a boundary, for every chunk size.
TEST( ProcessFileWords, ChunkBoundariesDoNotMiscount )
{
  std::string content;
  for ( int i = 0; i < 4000; ++i ) content += "wordword ";
  content += "tail";  // no trailing whitespace
  const size_t expected = refWords( content );
  TempFile f( content );

  for ( size_t bpt : { size_t( 1 ), size_t( 2 ), size_t( 7 ), size_t( 64 ),
                       size_t( 1024 ), size_t( 4096 ), size_t( 100000 ),
                       size_t( 64 * 1024 * 1024 ) } ) {
    EXPECT_EQ( pfWords( f.path(), bpt ), expected ) << "bytesPerThread=" << bpt;
  }
}

TEST( ProcessFileWords, BoundaryInsideWhitespaceRun )
{
  std::string content = "alpha";
  content += std::string( 500, ' ' );
  content += "beta";
  const size_t expected = refWords( content );  // 2
  TempFile f( content );
  for ( size_t bpt : { size_t( 1 ), size_t( 8 ), size_t( 64 ), size_t( 256 ) } )
    EXPECT_EQ( pfWords( f.path(), bpt ), expected ) << "bytesPerThread=" << bpt;
}

TEST( ProcessFileWords, LargerThanReadBuffer )
{
  std::string content;
  while ( content.size() < 5 * 1024 * 1024 ) content += "lorem ipsum dolor ";
  const size_t expected = refWords( content );
  TempFile f( content );
  EXPECT_EQ( pfWords( f.path() ), expected );
  EXPECT_EQ( pfWords( f.path(), 256 * 1024 ), expected );  // many chunks
}

TEST( ProcessFileWords, Deterministic )
{
  std::string content;
  for ( int i = 0; i < 1000; ++i ) content += "a bb ccc  dddd\n";
  const size_t expected = refWords( content );
  TempFile f( content );
  for ( int i = 0; i < 8; ++i )
    EXPECT_EQ( pfWords( f.path(), 4096 ), expected ) << "run " << i;
}

// ---------------------------------------------------------------------------
// Character counting (-m, like `wc -m`): UTF-8 code points.
// ---------------------------------------------------------------------------
TEST( ProcessFileChars, EmptyFile )
{
  TempFile f( "" );
  EXPECT_EQ( pfChars( f.path() ), 0u );
}

TEST( ProcessFileChars, AsciiCountsLikeBytes )
{
  TempFile f( "hello\nworld" );  // 11 bytes, all single-byte
  EXPECT_EQ( pfChars( f.path() ), 11u );
}

TEST( ProcessFileChars, MultibyteCountsCodePoints )
{
  // "héllo wörld\n" -> 12 characters, 14 bytes.
  const std::string content = "h\xC3\xA9llo w\xC3\xB6rld\n";
  TempFile f( content );
  EXPECT_EQ( pfChars( f.path() ), 12u );
}

TEST( ProcessFileChars, ChunkBoundariesDoNotMiscount )
{
  std::string content;
  for ( int i = 0; i < 4000; ++i ) content += "a\xC3\xA9z\xE2\x98\x83 ";
  const size_t expected = refChars( content );
  TempFile f( content );

  for ( size_t bpt : { size_t( 1 ), size_t( 2 ), size_t( 7 ), size_t( 64 ),
                       size_t( 1024 ), size_t( 4096 ), size_t( 100000 ),
                       size_t( 64 * 1024 * 1024 ) } ) {
    EXPECT_EQ( pfChars( f.path(), bpt ), expected ) << "bytesPerThread=" << bpt;
  }
}

TEST( ProcessFileChars, LargerThanReadBuffer )
{
  std::string content;
  while ( content.size() < 5 * 1024 * 1024 ) content += "lorem \xE2\x98\x83 ip ";
  const size_t expected = refChars( content );
  TempFile f( content );
  EXPECT_EQ( pfChars( f.path() ), expected );
  EXPECT_EQ( pfChars( f.path(), 256 * 1024 ), expected );  // many chunks
}

// ---------------------------------------------------------------------------
// Longest-line length (-L, like `wc -L`).
// ---------------------------------------------------------------------------
TEST( ProcessFileMaxLine, EmptyFile )
{
  TempFile f( "" );
  EXPECT_EQ( pfMaxLine( f.path() ), 0u );
}

TEST( ProcessFileMaxLine, SmallKnownLength )
{
  TempFile f( "ab\nabcd\nx\n" );  // longest line "abcd" -> 4
  EXPECT_EQ( pfMaxLine( f.path() ), 4u );
}

TEST( ProcessFileMaxLine, UnterminatedFinalLineIgnored )
{
  // The final line "ccccc" has no trailing newline, so wc -L ignores it; the
  // longest terminated line is "bb" -> 2.
  TempFile f( "a\nbb\nccccc" );
  EXPECT_EQ( pfMaxLine( f.path() ), 2u );
}

// The critical stitch test: a small bytesPerThread forces many chunks, so the
// longest line straddles chunk edges. The merge must reassemble it (carry +
// prefix) and never under- or over-count, for every chunk size.
TEST( ProcessFileMaxLine, ChunkBoundariesDoNotMiscount )
{
  std::string content = "aa\nbb\n";
  content += std::string( 9000, 'x' );  // the longest line, no newline yet
  content += "\ncc\n";
  const size_t expected = refMaxLineLen( content );  // 9000
  ASSERT_EQ( expected, 9000u );
  TempFile f( content );

  for ( size_t bpt : { size_t( 1 ), size_t( 2 ), size_t( 7 ), size_t( 64 ),
                       size_t( 1024 ), size_t( 4096 ), size_t( 100000 ),
                       size_t( 64 * 1024 * 1024 ) } ) {
    EXPECT_EQ( pfMaxLine( f.path(), bpt ), expected )
        << "bytesPerThread=" << bpt;
  }
}

TEST( ProcessFileMaxLine, BoundaryOnNewline )
{
  std::string content;
  for ( int i = 0; i < 2000; ++i ) content += "abcd\n";  // every line length 4
  const size_t expected = refMaxLineLen( content );      // 4
  TempFile f( content );
  for ( size_t bpt : { size_t( 1 ), size_t( 5 ), size_t( 64 ), size_t( 256 ) } )
    EXPECT_EQ( pfMaxLine( f.path(), bpt ), expected )
        << "bytesPerThread=" << bpt;
}

TEST( ProcessFileMaxLine, LargerThanReadBuffer )
{
  std::string content = "tiny\n";
  content += std::string( 3 * 1024 * 1024, 'y' );  // the longest line
  content += "\ntiny\n";
  const size_t expected = refMaxLineLen( content );
  TempFile f( content );
  EXPECT_EQ( pfMaxLine( f.path() ), expected );
  EXPECT_EQ( pfMaxLine( f.path(), 256 * 1024 ), expected );  // many chunks
}

// ---------------------------------------------------------------------------
// Line counting under heavy chunking and large inputs.
// ---------------------------------------------------------------------------
TEST( ProcessFileLines, ChunkBoundariesDoNotMiscount )
{
  const std::string content = makePattern( 200000, 5 );
  const size_t expected = refCount( content, '\n' );
  TempFile f( content );

  for ( size_t bpt : { size_t( 1 ), size_t( 7 ), size_t( 64 ), size_t( 1024 ),
                       size_t( 4096 ), size_t( 100000 ),
                       size_t( 64 * 1024 * 1024 ) } ) {
    EXPECT_EQ( pfLines( f.path(), bpt ), expected )
        << "bytesPerThread=" << bpt;
  }
}

TEST( ProcessFileLines, NewlinesExactlyOnChunkSplits )
{
  const size_t len = 8192;
  std::string content( len, 'x' );
  for ( size_t i = 0; i < len; i += 64 ) content[i] = '\n';
  TempFile f( content );
  const size_t expected = refCount( content, '\n' );
  EXPECT_EQ( pfLines( f.path(), 64 ), expected );
  EXPECT_EQ( pfLines( f.path(), 128 ), expected );
}

TEST( ProcessFileLines, LargerThanReadBuffer )
{
  const std::string content = makePattern( 5 * 1024 * 1024, 100 );
  const size_t expected = refCount( content, '\n' );
  TempFile f( content );
  EXPECT_EQ( pfLines( f.path() ), expected );
  EXPECT_EQ( pfLines( f.path(), 256 * 1024 ), expected );  // many threads
}

TEST( ProcessFileLines, DenseLargeFile )
{
  const std::string content( 3 * 1024 * 1024, '\n' );
  TempFile f( content );
  EXPECT_EQ( pfLines( f.path() ), content.size() );
}

TEST( ProcessFileLines, Deterministic )
{
  const std::string content = makePattern( 500000, 11 );
  const size_t expected = refCount( content, '\n' );
  TempFile f( content );
  for ( int i = 0; i < 8; ++i )
    EXPECT_EQ( pfLines( f.path(), 4096 ), expected ) << "run " << i;
}

// ---------------------------------------------------------------------------
// The fused engine: a Workload requesting several counts computes them all in
// one pass, and each must equal the single-counter result -- across chunk sizes,
// so the words/longest-line stitching survives alongside the others.
// ---------------------------------------------------------------------------
TEST( ProcessFileFused, AllCountersMatchIndividualPasses )
{
  std::string content;
  for ( int i = 0; i < 3000; ++i ) content += "h\xC3\xA9llo w\xC3\xB6rld foo\n";
  content += "a very long trailing line with no newline at the end";
  TempFile f( content );

  Workload w;
  w.lines = w.words = w.bytes = w.chars = w.maxLine = w.target = true;
  w.targetByte = 'o';

  for ( size_t bpt : { size_t( 1 ), size_t( 7 ), size_t( 64 ), size_t( 4096 ),
                       size_t( 100000 ), size_t( 64 * 1024 * 1024 ) } ) {
    const Counts c = processFile( f.path(), w, bpt );
    EXPECT_EQ( c.lines, pfLines( f.path(), bpt ) ) << "bpt=" << bpt;
    EXPECT_EQ( c.words, pfWords( f.path(), bpt ) ) << "bpt=" << bpt;
    EXPECT_EQ( c.bytes, pfBytes( f.path(), bpt ) ) << "bpt=" << bpt;
    EXPECT_EQ( c.chars, pfChars( f.path(), bpt ) ) << "bpt=" << bpt;
    EXPECT_EQ( c.maxLine, pfMaxLine( f.path(), bpt ) ) << "bpt=" << bpt;
    EXPECT_EQ( c.target, pfTarget( f.path(), 'o', bpt ) ) << "bpt=" << bpt;
  }
}

TEST( ProcessFileFused, DefaultTrioMatchesKnownCounts )
{
  TempFile f( "alpha beta\ngamma\n\nx y z\n" );  // 4 lines, 6 words, 24 bytes
  Workload w;
  w.lines = w.words = w.bytes = true;
  const Counts c = processFile( f.path(), w );
  EXPECT_EQ( c.lines, 4u );
  EXPECT_EQ( c.words, 6u );
  EXPECT_EQ( c.bytes, 24u );
  // Unrequested counters stay zero.
  EXPECT_EQ( c.chars, 0u );
  EXPECT_EQ( c.maxLine, 0u );
}

// ---------------------------------------------------------------------------
// Missing file: prints an error and exit(1).
// ---------------------------------------------------------------------------
TEST( ProcessFileDeathTest, MissingFileExitsWithCode1 )
{
  EXPECT_EXIT(
      pfLines( "/nonexistent/qwc/path/should/not/exist_zzz" ),
      ::testing::ExitedWithCode( 1 ), "Error opening file" );
}

// ---------------------------------------------------------------------------
// stdin path: an empty filename makes processFile read fd 0.
// ---------------------------------------------------------------------------
class StdinFixture : public ::testing::Test
{
 protected:
  void feedStdin( const std::string& content )
  {
    tmp_ = std::make_unique<TempFile>( content );
    savedStdin_ = ::dup( STDIN_FILENO );
    ASSERT_GE( savedStdin_, 0 );
    int fd = ::open( tmp_->path(), O_RDONLY );
    ASSERT_GE( fd, 0 );
    ASSERT_GE( ::dup2( fd, STDIN_FILENO ), 0 );
    ::close( fd );
  }

  void TearDown() override
  {
    if ( savedStdin_ >= 0 ) {
      ::dup2( savedStdin_, STDIN_FILENO );
      ::close( savedStdin_ );
      savedStdin_ = -1;
    }
  }

  int savedStdin_ = -1;
  std::unique_ptr<TempFile> tmp_;
};

TEST_F( StdinFixture, CountsLinesFromStdin )
{
  feedStdin( "one\ntwo\nthree\n" );
  EXPECT_EQ( pfLines( "" ), 3u );
}

TEST_F( StdinFixture, EmptyStdin )
{
  feedStdin( "" );
  EXPECT_EQ( pfLines( "" ), 0u );
}

TEST_F( StdinFixture, StdinLargerThanReadBuffer )
{
  const std::string content = makePattern( 2 * 1024 * 1024, 50 );
  const size_t expected = refCount( content, '\n' );
  feedStdin( content );
  EXPECT_EQ( pfLines( "" ), expected );
}

TEST_F( StdinFixture, StdinCustomTarget )
{
  feedStdin( "a;b;c;" );
  EXPECT_EQ( pfTarget( "", ';' ), 3u );
}

TEST_F( StdinFixture, StdinByteCount )
{
  feedStdin( "hello world" );  // 11 bytes
  EXPECT_EQ( pfBytes( "" ), 11u );
}

TEST_F( StdinFixture, StdinWordCount )
{
  feedStdin( "  the quick brown   fox\n" );
  EXPECT_EQ( pfWords( "" ), 4u );
}

TEST_F( StdinFixture, StdinWordsLargerThanReadBuffer )
{
  std::string content;
  while ( content.size() < 2 * 1024 * 1024 ) content += "lorem ipsum dolor ";
  const size_t expected = refWords( content );
  feedStdin( content );
  EXPECT_EQ( pfWords( "" ), expected );
}

TEST_F( StdinFixture, StdinCharCount )
{
  feedStdin( "h\xC3\xA9llo w\xC3\xB6rld\n" );  // 12 chars, 14 bytes
  EXPECT_EQ( pfChars( "" ), 12u );
}

TEST_F( StdinFixture, StdinCharsLargerThanReadBuffer )
{
  std::string content;
  while ( content.size() < 2 * 1024 * 1024 ) content += "lorem \xE2\x98\x83 ip ";
  const size_t expected = refChars( content );
  feedStdin( content );
  EXPECT_EQ( pfChars( "" ), expected );
}

TEST_F( StdinFixture, StdinMaxLineLength )
{
  feedStdin( "ab\nabcd\nx\n" );  // longest line "abcd" -> 4
  EXPECT_EQ( pfMaxLine( "" ), 4u );
}

TEST_F( StdinFixture, StdinMaxLineLengthLargerThanReadBuffer )
{
  std::string content = "tiny\n";
  content += std::string( 2 * 1024 * 1024, 'z' );
  content += "\ntiny\n";
  const size_t expected = refMaxLineLen( content );
  feedStdin( content );
  EXPECT_EQ( pfMaxLine( "" ), expected );
}

TEST_F( StdinFixture, StdinFusedAllCounts )
{
  feedStdin( "alpha beta\ngamma\n\nx y z\n" );  // 4 lines, 6 words, 24 bytes
  Workload w;
  w.lines = w.words = w.bytes = true;
  const Counts c = processFile( "", w );
  EXPECT_EQ( c.lines, 4u );
  EXPECT_EQ( c.words, 6u );
  EXPECT_EQ( c.bytes, 24u );
}

TEST_F( StdinFixture, StdinFusedLargerThanReadBuffer )
{
  std::string content;
  while ( content.size() < 2 * 1024 * 1024 ) content += "lorem ipsum\ndolor\n";
  feedStdin( content );
  Workload w;
  w.lines = w.words = w.bytes = true;
  const Counts c = processFile( "", w );
  EXPECT_EQ( c.lines, refCount( content, '\n' ) );
  EXPECT_EQ( c.words, refWords( content ) );
  EXPECT_EQ( c.bytes, content.size() );
}
