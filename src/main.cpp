#include <atomic>
#include <clocale>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "cli.h"
#include "processfile.h"

static const u32 MAX_THREADS = std::thread::hardware_concurrency();

// Map `perFile(idx)` over [0, numFiles) across the thread pool: a shared atomic
// cursor hands the next file to whichever worker is free, and results are stored
// at the matching index so the output order mirrors opt.files.
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

int main( int argc, char** argv )
{
  Options opt;
  if ( const std::optional<int> rc = parseArgs( argc, argv, opt ) ) return *rc;

  // The char/byte column counts characters (-m) in the locale's encoding. In a
  // single-byte locale (e.g. C/POSIX) a character is just a byte, so wc's -m
  // collapses to -c. Mirror that: adopt the environment's locale and, when it
  // isn't multibyte, count bytes for that column instead of scanning code points.
  if ( opt.charByte && opt.charsNotBytes ) {
    std::setlocale( LC_CTYPE, "" );
    if ( MB_CUR_MAX <= 1 ) opt.charsNotBytes = false;
  }

  // Resolve the requested columns into a single counting workload, computed once
  // per file in a single pass.
  const Workload work = opt.workload();

  // No file arguments: count standard input. wc prints just the padded count(s),
  // with no name.
  if ( opt.files.empty() ) {
    printCounts( opt, processFile( "", work, opt.bytesPerThread ), nullptr );
    return 0;
  }

  if ( !collectFiles( opt ) ) return 1;
  const usize numFiles = opt.files.size();

  const std::vector<Counts> output = mapFiles<Counts>(
      numFiles,
      [&]( const usize idx ) {
        return processFile( opt.files[idx].c_str(), work, opt.bytesPerThread );
      }
  );
  printResults( opt, output );
  return 0;
}
