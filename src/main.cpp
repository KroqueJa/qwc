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

int main( int argc, char** argv )
{
  Options opt;
  if ( const std::optional<int> rc = parseArgs( argc, argv, opt ) ) return *rc;

  // No file arguments: count standard input.
  if ( opt.files.empty() ) {
    std::cout << processFile( "", opt.bytesPerThread, opt.target, opt.mode )
              << std::endl;
    return 0;
  }

  if ( !collectFiles( opt ) ) return 1;

  // Count each file in parallel: a shared atomic cursor hands the next file to
  // whichever worker is free, with results indexed to match opt.files.
  const usize numFiles = opt.files.size();
  std::vector<Result> output( numFiles );
  std::atomic<usize> nextFile = 0;
  std::vector<std::thread> pool( MAX_THREADS );

  for ( auto& t: pool ) {
    t = std::thread( [&]() {
      while ( true ) {
        const usize idx = nextFile.fetch_add( 1 );
        if ( idx >= numFiles ) return;
        const std::string& filename = opt.files[idx];
        const usize lines = processFile(
            filename.c_str(), opt.bytesPerThread, opt.target, opt.mode
        );
        output[idx] = { std::to_string( lines ) + " " + filename, lines };
      }
    } );
  }

  for ( auto& t: pool ) t.join();

  printResults( opt, output );
  return 0;
}
