#include <atomic>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "cli.h"
#include "processfile.h"
#include "result.h"

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

  // No file arguments: count standard input. wc prints just the padded count(s),
  // with no name.
  if ( opt.files.empty() ) {
    if ( opt.all )
      printAllLine( processFileAll( "", opt.bytesPerThread ), nullptr );
    else
      printCountLine(
          processFile( "", opt.bytesPerThread, opt.target, opt.mode ), nullptr );
    return 0;
  }

  if ( !collectFiles( opt ) ) return 1;
  const usize numFiles = opt.files.size();

  if ( opt.all ) {
    const std::vector<Counts> output = mapFiles<Counts>(
        numFiles,
        [&]( const usize idx ) {
          return processFileAll( opt.files[idx].c_str(), opt.bytesPerThread );
        }
    );
    printResultsAll( opt, output );
    return 0;
  }

  const std::vector<Result> output = mapFiles<Result>(
      numFiles,
      [&]( const usize idx ) {
        return Result{ processFile(
            opt.files[idx].c_str(), opt.bytesPerThread, opt.target, opt.mode
        ) };
      }
  );
  printResults( opt, output );
  return 0;
}
