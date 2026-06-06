#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <random>
#include <string>

#include "processfile.h"
#include "test_util.h"

using wcltest::refCount;
using wcltest::refWords;

namespace {

// RAII temp file seeded with arbitrary bytes. Uses mkstemp so concurrent test
// processes don't collide.
class TempFile
{
 public:
  explicit TempFile( const std::string& contents )
  {
    char tmpl[] = "/tmp/wcl_pf_test_XXXXXX";
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
// File path
// ---------------------------------------------------------------------------
TEST( ProcessFile, EmptyFile )
{
  TempFile f( "" );
  EXPECT_EQ( processFile( f.path() ), 0u );
}

TEST( ProcessFile, SingleNewline )
{
  TempFile f( "\n" );
  EXPECT_EQ( processFile( f.path() ), 1u );
}

TEST( ProcessFile, SmallKnownCount )
{
  TempFile f( "a\nb\nc\n" );
  EXPECT_EQ( processFile( f.path() ), 3u );
}

TEST( ProcessFile, NoTrailingNewline )
{
  TempFile f( "a\nb\nc" );
  EXPECT_EQ( processFile( f.path() ), 2u );
}

TEST( ProcessFile, CustomTarget )
{
  TempFile f( "a,b,c,d,e" );
  EXPECT_EQ( processFile( f.path(), 64 * 1024 * 1024, ',' ), 4u );
}

// ---------------------------------------------------------------------------
// Byte counting (CountMode::Bytes, like `wc -c`): returns the file size, taken
// straight from fstat without scanning. The target byte is irrelevant.
// ---------------------------------------------------------------------------
TEST( ProcessFileBytes, CountsFileSize )
{
  TempFile f( "hello\nworld" );  // 11 bytes
  EXPECT_EQ( processFile( f.path(), 64 * 1024 * 1024, '\n', CountMode::Bytes ),
             11u );
}

TEST( ProcessFileBytes, EmptyFileIsZero )
{
  TempFile f( "" );
  EXPECT_EQ( processFile( f.path(), 64 * 1024 * 1024, '\n', CountMode::Bytes ),
             0u );
}

TEST( ProcessFileBytes, TargetDoesNotAffectByteCount )
{
  const std::string content( 1234, 'x' );
  TempFile f( content );
  // Whatever the target, byte mode reports the size.
  EXPECT_EQ( processFile( f.path(), 64 * 1024 * 1024, 'x', CountMode::Bytes ),
             content.size() );
}

TEST( ProcessFileBytes, LargeFileSize )
{
  const std::string content = makePattern( 5 * 1024 * 1024, 100 );
  TempFile f( content );
  EXPECT_EQ( processFile( f.path(), 256 * 1024, '\n', CountMode::Bytes ),
             content.size() );
}

// ---------------------------------------------------------------------------
// Word counting (CountMode::Words, like `wc -w`).
// ---------------------------------------------------------------------------
TEST( ProcessFileWords, EmptyFile )
{
  TempFile f( "" );
  EXPECT_EQ( processFile( f.path(), 64 * 1024 * 1024, '\n', CountMode::Words ),
             0u );
}

TEST( ProcessFileWords, OnlyWhitespace )
{
  TempFile f( "   \n\t \n" );
  EXPECT_EQ( processFile( f.path(), 64 * 1024 * 1024, '\n', CountMode::Words ),
             0u );
}

TEST( ProcessFileWords, SmallKnownCount )
{
  TempFile f( "  the quick brown   fox\n" );
  EXPECT_EQ( processFile( f.path(), 64 * 1024 * 1024, '\n', CountMode::Words ),
             4u );
}

// ---------------------------------------------------------------------------
// The critical chunk-stitching test: a small bytesPerThread forces many chunks
// so word boundaries fall across splits. The merge must neither drop a word nor
// double-count one straddling a boundary, for every chunk size.
// ---------------------------------------------------------------------------
TEST( ProcessFileWords, ChunkBoundariesDoNotMiscount )
{
  // Long non-whitespace runs interspersed with single spaces, so many words
  // straddle chunk edges.
  std::string content;
  for ( int i = 0; i < 4000; ++i ) content += "wordword ";
  content += "tail";  // no trailing whitespace
  const size_t expected = refWords( content );
  TempFile f( content );

  for ( size_t bpt : { size_t( 1 ), size_t( 2 ), size_t( 7 ), size_t( 64 ),
                       size_t( 1024 ), size_t( 4096 ), size_t( 100000 ),
                       size_t( 64 * 1024 * 1024 ) } ) {
    EXPECT_EQ( processFile( f.path(), bpt, '\n', CountMode::Words ), expected )
        << "bytesPerThread=" << bpt;
  }
}

TEST( ProcessFileWords, BoundaryInsideWhitespaceRun )
{
  // A long whitespace run between words: chunk edges may land inside it, which
  // must not invent or drop a word.
  std::string content = "alpha";
  content += std::string( 500, ' ' );
  content += "beta";
  const size_t expected = refWords( content );  // 2
  TempFile f( content );
  for ( size_t bpt : { size_t( 1 ), size_t( 8 ), size_t( 64 ), size_t( 256 ) } )
    EXPECT_EQ( processFile( f.path(), bpt, '\n', CountMode::Words ), expected )
        << "bytesPerThread=" << bpt;
}

TEST( ProcessFileWords, LargerThanReadBuffer )
{
  // Exceeds the 1 MiB per-thread read buffer, exercising the streaming loop and
  // the carry across read buffers within a single chunk.
  std::string content;
  while ( content.size() < 5 * 1024 * 1024 ) content += "lorem ipsum dolor ";
  const size_t expected = refWords( content );
  TempFile f( content );
  EXPECT_EQ( processFile( f.path(), 64 * 1024 * 1024, '\n', CountMode::Words ),
             expected );
  EXPECT_EQ( processFile( f.path(), 256 * 1024, '\n', CountMode::Words ),
             expected );  // many chunks
}

TEST( ProcessFileWords, Deterministic )
{
  std::string content;
  for ( int i = 0; i < 1000; ++i ) content += "a bb ccc  dddd\n";
  const size_t expected = refWords( content );
  TempFile f( content );
  for ( int i = 0; i < 8; ++i )
    EXPECT_EQ( processFile( f.path(), 4096, '\n', CountMode::Words ), expected )
        << "run " << i;
}

// ---------------------------------------------------------------------------
// processFileAll: lines, words and bytes in one pass (the bare-wc / --all mode).
// ---------------------------------------------------------------------------
TEST( ProcessFileAll, EmptyFile )
{
  TempFile f( "" );
  const Counts c = processFileAll( f.path() );
  EXPECT_EQ( c.lines, 0u );
  EXPECT_EQ( c.words, 0u );
  EXPECT_EQ( c.bytes, 0u );
}

TEST( ProcessFileAll, SmallKnownCounts )
{
  TempFile f( "alpha beta\ngamma\n\nx y z\n" );  // 4 lines, 6 words, 24 bytes
  const Counts c = processFileAll( f.path() );
  EXPECT_EQ( c.lines, 4u );
  EXPECT_EQ( c.words, 6u );
  EXPECT_EQ( c.bytes, 24u );
}

TEST( ProcessFileAll, NoTrailingNewline )
{
  TempFile f( "a b\nc d" );  // 1 line, 4 words, 7 bytes
  const Counts c = processFileAll( f.path() );
  EXPECT_EQ( c.lines, 1u );
  EXPECT_EQ( c.words, 4u );
  EXPECT_EQ( c.bytes, 7u );
}

// The all-pass shares the word-stitching logic, so verify it across chunk sizes
// against the same oracle used for the word-only path.
TEST( ProcessFileAll, ChunkBoundariesMatchOracles )
{
  std::string content;
  for ( int i = 0; i < 4000; ++i ) content += "wordword ";
  content += "tail";
  const size_t expWords = refWords( content );
  const size_t expLines = refCount( content, '\n' );
  TempFile f( content );

  for ( size_t bpt : { size_t( 1 ), size_t( 7 ), size_t( 64 ), size_t( 4096 ),
                       size_t( 100000 ), size_t( 64 * 1024 * 1024 ) } ) {
    const Counts c = processFileAll( f.path(), bpt );
    EXPECT_EQ( c.lines, expLines ) << "bytesPerThread=" << bpt;
    EXPECT_EQ( c.words, expWords ) << "bytesPerThread=" << bpt;
    EXPECT_EQ( c.bytes, content.size() ) << "bytesPerThread=" << bpt;
  }
}

TEST( ProcessFileAll, LargerThanReadBuffer )
{
  std::string content;
  while ( content.size() < 5 * 1024 * 1024 ) content += "lorem ipsum dolor\n";
  TempFile f( content );
  const Counts c = processFileAll( f.path(), 256 * 1024 );  // many chunks
  EXPECT_EQ( c.lines, refCount( content, '\n' ) );
  EXPECT_EQ( c.words, refWords( content ) );
  EXPECT_EQ( c.bytes, content.size() );
}

// ---------------------------------------------------------------------------
// Multi-threaded chunking: a small bytesPerThread forces many threads, so line
// boundaries fall across chunk splits. Because we count bytes (not lines),
// splits must neither drop nor double-count targets.
// ---------------------------------------------------------------------------
TEST( ProcessFile, ChunkBoundariesDoNotMiscount )
{
  const std::string content = makePattern( 200000, 5 );
  const size_t expected = refCount( content, '\n' );
  TempFile f( content );

  for ( size_t bpt : { size_t( 1 ), size_t( 7 ), size_t( 64 ), size_t( 1024 ),
                       size_t( 4096 ), size_t( 100000 ),
                       size_t( 64 * 1024 * 1024 ) } ) {
    EXPECT_EQ( processFile( f.path(), bpt, '\n' ), expected )
        << "bytesPerThread=" << bpt;
  }
}

TEST( ProcessFile, TargetsExactlyOnChunkSplits )
{
  // bytesPerThread chosen so chunk edges land right on target bytes.
  const size_t len = 8192;
  std::string content( len, 'x' );
  for ( size_t i = 0; i < len; i += 64 ) content[i] = '\n';
  TempFile f( content );
  const size_t expected = refCount( content, '\n' );
  EXPECT_EQ( processFile( f.path(), 64, '\n' ), expected );
  EXPECT_EQ( processFile( f.path(), 128, '\n' ), expected );
}

// ---------------------------------------------------------------------------
// Large file: exceeds the 1 MiB per-thread read buffer, exercising the pread
// streaming loop within a chunk.
// ---------------------------------------------------------------------------
TEST( ProcessFile, LargerThanReadBuffer )
{
  const std::string content = makePattern( 5 * 1024 * 1024, 100 );
  const size_t expected = refCount( content, '\n' );
  TempFile f( content );
  EXPECT_EQ( processFile( f.path() ), expected );
  EXPECT_EQ( processFile( f.path(), 256 * 1024 ), expected );  // many threads
}

TEST( ProcessFile, DenseLargeFile )
{
  const std::string content( 3 * 1024 * 1024, '\n' );
  TempFile f( content );
  EXPECT_EQ( processFile( f.path() ), content.size() );
}

TEST( ProcessFile, Deterministic )
{
  const std::string content = makePattern( 500000, 11 );
  const size_t expected = refCount( content, '\n' );
  TempFile f( content );
  for ( int i = 0; i < 8; ++i )
    EXPECT_EQ( processFile( f.path(), 4096 ), expected ) << "run " << i;
}

// ---------------------------------------------------------------------------
// Missing file: prints an error and exit(1).
// ---------------------------------------------------------------------------
TEST( ProcessFileDeathTest, MissingFileExitsWithCode1 )
{
  EXPECT_EXIT(
      processFile( "/nonexistent/wcl/path/should/not/exist_zzz" ),
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

TEST_F( StdinFixture, CountsFromStdin )
{
  feedStdin( "one\ntwo\nthree\n" );
  EXPECT_EQ( processFile( "" ), 3u );
}

TEST_F( StdinFixture, EmptyStdin )
{
  feedStdin( "" );
  EXPECT_EQ( processFile( "" ), 0u );
}

TEST_F( StdinFixture, StdinLargerThanReadBuffer )
{
  // The stdin path reads in 128*4096 = 512 KiB chunks; exceed that.
  const std::string content = makePattern( 2 * 1024 * 1024, 50 );
  const size_t expected = refCount( content, '\n' );
  feedStdin( content );
  EXPECT_EQ( processFile( "" ), expected );
}

TEST_F( StdinFixture, StdinCustomTarget )
{
  feedStdin( "a;b;c;" );
  EXPECT_EQ( processFile( "", 64 * 1024 * 1024, ';' ), 3u );
}

TEST_F( StdinFixture, StdinByteCount )
{
  feedStdin( "hello world" );  // 11 bytes
  EXPECT_EQ( processFile( "", 64 * 1024 * 1024, '\n', CountMode::Bytes ), 11u );
}

TEST_F( StdinFixture, StdinWordCount )
{
  feedStdin( "  the quick brown   fox\n" );
  EXPECT_EQ( processFile( "", 64 * 1024 * 1024, '\n', CountMode::Words ), 4u );
}

TEST_F( StdinFixture, StdinWordsLargerThanReadBuffer )
{
  // The stdin path reads in 128*4096 = 512 KiB chunks; exceed that so the
  // in-word state must carry across reads.
  std::string content;
  while ( content.size() < 2 * 1024 * 1024 ) content += "lorem ipsum dolor ";
  const size_t expected = refWords( content );
  feedStdin( content );
  EXPECT_EQ( processFile( "", 64 * 1024 * 1024, '\n', CountMode::Words ),
             expected );
}

TEST_F( StdinFixture, StdinAllCounts )
{
  feedStdin( "alpha beta\ngamma\n\nx y z\n" );  // 4 lines, 6 words, 24 bytes
  const Counts c = processFileAll( "" );
  EXPECT_EQ( c.lines, 4u );
  EXPECT_EQ( c.words, 6u );
  EXPECT_EQ( c.bytes, 24u );
}

TEST_F( StdinFixture, StdinAllLargerThanReadBuffer )
{
  std::string content;
  while ( content.size() < 2 * 1024 * 1024 ) content += "lorem ipsum\ndolor\n";
  feedStdin( content );
  const Counts c = processFileAll( "" );
  EXPECT_EQ( c.lines, refCount( content, '\n' ) );
  EXPECT_EQ( c.words, refWords( content ) );
  EXPECT_EQ( c.bytes, content.size() );
}
