#include <unistd.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "countlines.h"
#include "processfile.h"
#include "result.h"

static const unsigned MAX_THREADS = std::thread::hardware_concurrency();

int main( int argc, char** argv )
{
  size_t bytesPerThread = 64 * 1024 * 1024;
  int fileStart = 1;

  // Parse --bytes-per-thread flag
  if ( argc > 2 && std::strcmp( argv[1], "--bytes-per-thread" ) == 0 ) {
    if ( argc < 4 ) {
      std::cerr << "Error: --bytes-per-thread requires a value and at least one file\n";
      return 1;
    }
    bytesPerThread = std::strtoull( argv[2], nullptr, 10 );
    if ( bytesPerThread == 0 ) {
      std::cerr << "Error: --bytes-per-thread must be > 0\n";
      return 1;
    }
    fileStart = 3;
  }

  if ( fileStart == argc ) {
    std::cout << processFile( "", bytesPerThread ) << std::endl;
    return 0;
  }

  std::vector<Result> output( argc - fileStart );
  std::atomic<size_t> nextFile = 0;
  std::vector<std::thread> pool( MAX_THREADS );

  for ( auto& t : pool ) {
    t = std::thread( [&]() {
      while ( true ) {
        size_t idx = nextFile.fetch_add( 1 );
        if ( idx >= (size_t)( argc - fileStart ) ) return;
        const char* filename = argv[idx + fileStart];
        size_t lines = processFile( filename, bytesPerThread );
        output[idx] = { std::to_string( lines ) + " " + filename, lines };
      }
    } );
  }

  for ( auto& t : pool ) t.join();

  size_t total = 0;
  for ( const auto& result : output ) {
    total += result.lineCount;
    if ( argc - fileStart > 1 ) std::cout << result.str << '\n';
  }
  std::cout << total << std::endl;

  return 0;
}