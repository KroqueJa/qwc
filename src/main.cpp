#include <langinfo.h>

#include <algorithm>
#include <atomic>
#include <clocale>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cli.h"
#include "processfile.h"

// hardware_concurrency() can report 0 when it cannot tell; treat that as 1 so
// we never end up with an empty worker pool. Spelled as a ternary over the
// noexcept hardware_concurrency() rather than std::max, which is not noexcept
// and so could (in principle) throw during static initialization.
static const u32 MAX_THREADS = std::thread::hardware_concurrency() > 0
                                   ? std::thread::hardware_concurrency()
                                   : 1;

// Files per worker on the no-scan (bytes-only, `-c`) path: each file is a bare
// fstat (~microseconds on a warm local cache), so a worker only earns its
// ~tens-of-microseconds spawn cost once it has this many files to chew through.
// Below this the glob runs serially; above it we scale up to MAX_THREADS. A
// cold or networked open() costs far more and parallelises sooner, so this
// warm-tuned default is deliberately conservative. Override with
// -DQWC_FILES_PER_THREAD=N.
#ifndef QWC_FILES_PER_THREAD
#define QWC_FILES_PER_THREAD 32
#endif

// Map `perFile(idx)` over [0, numFiles) across the thread pool: a shared atomic
// cursor hands the next file to whichever worker is free, and results are
// stored at the matching index so the output order mirrors opt.files.
//
// GCC 13's -fanalyzer mis-models the libstdc++ vector constructor and reports a
// bogus "use of uninitialized value" for the value-initialized `output` below.
// The diagnostic carries no source location, so no #pragma region can catch
// it; it is muted for this TU via -Wno-analyzer-use-of-uninitialized-value in
// CMakeLists.txt (GCC only).
template <typename T, typename Fn>
static std::vector<T> mapFiles( usize numFiles, u32 numThreads, Fn perFile )
{
  std::vector<T> output( numFiles );

  // One worker (or one file): run inline. Spawning a pool just to hand a single
  // thread the whole glob adds spawn+join latency for no parallelism. Results
  // are still stored by index, so output order matches the input order.
  if ( numThreads <= 1 || numFiles <= 1 ) {
    for ( usize i = 0; i < numFiles; ++i ) output[i] = perFile( i );
    return output;
  }

  std::atomic<usize> nextFile = 0;
  std::vector<std::thread> pool( numThreads );
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

int main( int argc, char** argv )
{
  Options opt;
  if ( const std::optional<int> rc = parseArgs( argc, argv, opt ) ) return *rc;

  // Adopt the environment's locale once, before any worker threads exist.
  // setlocale(LC_CTYPE, "") performs the POSIX LC_ALL > LC_CTYPE > LANG
  // resolution -- the same environment handling as wc. The codeset decides
  // both the -m collapse (a character is just a byte in single-byte locales,
  // so the -m column displays the byte count) and the word-splitting flavour;
  // POSIXLY_CORRECT disables coreutils' non-breaking-space separators, exactly
  // like wc.
  std::setlocale( LC_CTYPE, "" );  // NOLINT(concurrency-mt-unsafe)
  if ( opt.chars && MB_CUR_MAX <= 1 ) opt.charsAreBytes = true;

  // Resolve the requested columns into a single counting workload, computed
  // once per file in a single pass.
  Workload work = opt.workload();
  // Like setlocale above: called once at startup, before any worker threads
  // are spawned, so the thread-safety warnings do not apply here.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* codeset = nl_langinfo( CODESET );
  work.wordsMode.utf8 =
      codeset != nullptr && std::string_view( codeset ) == "UTF-8";
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  work.wordsMode.nbspace = std::getenv( "POSIXLY_CORRECT" ) == nullptr;

  // No file arguments: count standard input. wc prints just the padded
  // count(s), with no name.
  if ( opt.files.empty() ) {
    printCounts( opt, processFile( "", work, opt.bytesPerThread ), nullptr );
    return 0;
  }

  if ( !collectFiles( opt ) ) return 1;
  const usize numFiles = opt.files.size();

  // Choose the worker count before spawning anything. A bytes-only workload is
  // a bare fstat per file, so it scales workers with the glob (one per
  // QWC_FILES_PER_THREAD files) and small globs stay serial. A scanning
  // workload is heavy per file, so it uses as many workers as files (capped)
  // and leans on each file's own internal chunk-parallelism for the rest.
  const u32 numThreads =
      work.needsScan()
          ? static_cast<u32>( std::min<usize>( numFiles, MAX_THREADS ) )
          : static_cast<u32>( std::clamp<usize>(
                numFiles / QWC_FILES_PER_THREAD, 1, MAX_THREADS
            ) );

  const std::vector<Counts> output =
      mapFiles<Counts>( numFiles, numThreads, [&]( const usize idx ) {
        return processFile( opt.files[idx].c_str(), work, opt.bytesPerThread );
      } );
  printResults( opt, output );
  return 0;
}
