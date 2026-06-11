#include <langinfo.h>

#include <atomic>
#include <clocale>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cli.h"
#include "processfile.h"

static const u32 MAX_THREADS = std::thread::hardware_concurrency();

// Map `perFile(idx)` over [0, numFiles) across the thread pool: a shared atomic
// cursor hands the next file to whichever worker is free, and results are
// stored at the matching index so the output order mirrors opt.files.
//
// GCC 13's -fanalyzer mis-models the libstdc++ vector constructor and reports a
// bogus "use of uninitialized value" for the value-initialized `output` below;
// silence just that false positive (GCC only -- clang has no such option).
#if defined( __GNUC__ ) && !defined( __clang__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
#endif
template <typename T, typename Fn>
static std::vector<T> mapFiles( usize numFiles, Fn perFile )
{
  std::vector<T> output( numFiles );
  std::atomic<usize> nextFile = 0;
  std::vector<std::thread> pool( MAX_THREADS );
  for ( auto& t: pool ) {
    t = std::thread( [&]() {
      while ( true ) {
        const usize idx = nextFile.fetch_add( 1 );
        if ( idx >= numFiles ) return;
        output[idx] = perFile( idx );
      }
    } );
  }
  for ( auto& t: pool ) t.join();
  return output;
}
#if defined( __GNUC__ ) && !defined( __clang__ )
#pragma GCC diagnostic pop
#endif

int main( int argc, char** argv )
{
  Options opt;
  if ( const std::optional<int> rc = parseArgs( argc, argv, opt ) ) return *rc;

  // Adopt the environment's locale once, before any worker threads exist.
  // setlocale(LC_CTYPE, "") performs the POSIX LC_ALL > LC_CTYPE > LANG
  // resolution -- the same environment handling as wc. The codeset decides
  // both the -m collapse (a character is just a byte in single-byte locales,
  // so -m becomes -c) and the word-splitting flavour; POSIXLY_CORRECT disables
  // coreutils' non-breaking-space separators, exactly like wc.
  std::setlocale( LC_CTYPE, "" );  // NOLINT(concurrency-mt-unsafe)
  if ( opt.charByte && opt.charsNotBytes && MB_CUR_MAX <= 1 )
    opt.charsNotBytes = false;

  // Resolve the requested columns into a single counting workload, computed
  // once per file in a single pass.
  Workload work = opt.workload();
  const char* codeset = nl_langinfo( CODESET );
  work.wordsMode.utf8 =
      codeset != nullptr && std::string_view( codeset ) == "UTF-8";
  work.wordsMode.nbspace = std::getenv( "POSIXLY_CORRECT" ) == nullptr;

  // No file arguments: count standard input. wc prints just the padded
  // count(s), with no name.
  if ( opt.files.empty() ) {
    printCounts( opt, processFile( "", work, opt.bytesPerThread ), nullptr );
    return 0;
  }

  if ( !collectFiles( opt ) ) return 1;
  const usize numFiles = opt.files.size();

  const std::vector<Counts> output =
      mapFiles<Counts>( numFiles, [&]( const usize idx ) {
        return processFile( opt.files[idx].c_str(), work, opt.bytesPerThread );
      } );
  printResults( opt, output );
  return 0;
}
