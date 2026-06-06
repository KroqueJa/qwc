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
