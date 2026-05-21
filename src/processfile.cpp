#include "processfile.h"
#include "countlines.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <thread>
#include <vector>

static const size_t   BYTES_PER_THREAD = 64 * 1024 * 1024;
static const unsigned MAX_THREADS      = std::thread::hardware_concurrency();

size_t processFile( const char* filename )
{
  if ( filename[0] == '\0' ) {
    static thread_local char buffer[128 * 4096];
    ssize_t bytesRead;
    size_t totalLines = 0;
    while ( ( bytesRead = read( 0, buffer, sizeof( buffer ) ) ) > 0 )
      totalLines += countLines( buffer, (size_t)bytesRead );
    return totalLines;
  }

  int fd = open( filename, O_RDONLY );
  if ( fd < 0 ) {
    std::cerr << "Error opening file: " << filename << '\n';
    exit( 1 );
  }

  struct stat st;
  if ( fstat( fd, &st ) < 0 ) {
    std::cerr << "Error stating file: " << filename << '\n';
    exit( 1 );
  }
  size_t fileSize = (size_t)st.st_size;

  if ( fileSize == 0 ) {
    close( fd );
    return 0;
  }

  char* mapped = (char*)mmap( NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0 );
  close( fd );
  if ( mapped == MAP_FAILED ) {
    std::cerr << "mmap failed: " << filename << '\n';
    exit( 1 );
  }
  madvise( mapped, fileSize, MADV_SEQUENTIAL );

  unsigned numThreads = (unsigned)std::min(
      (size_t)MAX_THREADS,
      ( fileSize + BYTES_PER_THREAD - 1 ) / BYTES_PER_THREAD
  );
  numThreads = std::max( numThreads, 1u );

  size_t chunkSize = fileSize / numThreads;

  std::vector<size_t>      counts( numThreads, 0 );
  std::vector<std::thread> threads;
  threads.reserve( numThreads );

  for ( unsigned i = 0; i < numThreads; ++i ) {
    const char* start = mapped + i * chunkSize;
    size_t      size  = ( i == numThreads - 1 )
                        ? fileSize - i * chunkSize
                        : chunkSize;
    threads.emplace_back( [start, size, &counts, i]() {
      counts[i] = countLines( start, size );
    } );
  }

  for ( auto& t : threads ) t.join();
  munmap( mapped, fileSize );

  size_t total = 0;
  for ( size_t c : counts ) total += c;
  return total;
}