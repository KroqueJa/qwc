#include <unistd.h>

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#include "countlines.h"
#include "processfile.h"
#include "result.h"

static const unsigned MAX_THREADS = std::thread::hardware_concurrency();

int main( int argc, char** argv )
{
  if ( argc == 1 ) {
    std::cout << processFile( "" ) << std::endl;
    return 0;
  }

  std::vector<Result> output( argc - 1 );
  std::atomic<size_t> nextFile = 0;
  std::vector<std::thread> pool( MAX_THREADS );

  for ( auto& t : pool ) {
    t = std::thread( [&]() {
      while ( true ) {
        size_t idx = nextFile.fetch_add( 1 );
        if ( idx >= (size_t)( argc - 1 ) ) return;
        const char* filename = argv[idx + 1];
        size_t lines = processFile( filename );
        output[idx] = { std::to_string( lines ) + " " + filename, lines };
      }
    } );
  }

  for ( auto& t : pool ) t.join();

  size_t total = 0;
  for ( const auto& result : output ) {
    total += result.lineCount;
    if ( argc > 2 ) std::cout << result.str << '\n';
  }
  std::cout << total << std::endl;

  return 0;
}