#include <unistd.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "countlines.h"
#include "processfile.h"
#include "result.h"

static const u32 MAX_THREADS = std::thread::hardware_concurrency();

int main( int argc, char** argv )
{
  usize bytesPerThread = 64 * 1024 * 1024;

  // Byte to count. Defaults to '\n' (line counting). --char overrides it with
  // an arbitrary byte.
  char target    = '\n';
  int  fileStart = 1;

  // Parse leading "--flag [value]" options; the first non-flag argument begins
  // the file list.
  while ( fileStart < argc && std::strncmp( argv[fileStart], "--", 2 ) == 0 ) {
    if ( std::strcmp( argv[fileStart], "--bytes-per-thread" ) == 0 ) {
      if ( fileStart + 1 >= argc ) {
        std::cerr << "Error: --bytes-per-thread requires a value\n";
        return 1;
      }
      bytesPerThread = std::strtoull( argv[fileStart + 1], nullptr, 10 );
      if ( bytesPerThread == 0 ) {
        std::cerr << "Error: --bytes-per-thread must be > 0\n";
        return 1;
      }
      fileStart += 2;
    } else if ( std::strcmp( argv[fileStart], "--char" ) == 0 ) {
      if ( fileStart + 1 >= argc || argv[fileStart + 1][0] == '\0' ) {
        std::cerr << "Error: --char requires a single-character value\n";
        return 1;
      }
      target = argv[fileStart + 1][0];
      fileStart += 2;
    } else {
      std::cerr << "Error: unknown flag " << argv[fileStart] << '\n';
      return 1;
    }
  }

  if ( fileStart == argc ) {
    std::cout << processFile( "", bytesPerThread, target ) << std::endl;
    return 0;
  }

  std::vector<Result> output( argc - fileStart );
  std::atomic<usize> nextFile = 0;
  std::vector<std::thread> pool( MAX_THREADS );

  for ( auto& t : pool ) {
    t = std::thread( [&]() {
      while ( true ) {
        usize idx = nextFile.fetch_add( 1 );
        if ( idx >= static_cast<usize>( argc - fileStart ) ) return;
        const char* filename = argv[idx + fileStart];
        usize lines = processFile( filename, bytesPerThread, target );
        output[idx] = { std::to_string( lines ) + " " + filename, lines };
      }
    } );
  }

  for ( auto& t : pool ) t.join();

  usize total = 0;
  for ( const auto& result : output ) {
    total += result.lineCount;
    if ( argc - fileStart > 1 ) std::cout << result.str << '\n';
  }
  std::cout << total << std::endl;

  return 0;
}