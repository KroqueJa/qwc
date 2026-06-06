#include "processfile.h"
#include "countlines.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <iostream>
#include <thread>
#include <vector>

static const unsigned MAX_THREADS = std::thread::hardware_concurrency();

size_t processFile( const char* filename, size_t bytesPerThread, char target )
{
  if ( filename[0] == '\0' ) {
    static thread_local char buffer[128 * 4096];
    ssize_t bytesRead;
    size_t totalLines = 0;
    while ( ( bytesRead = read( 0, buffer, sizeof( buffer ) ) ) > 0 )
      totalLines += countLines( buffer, (size_t)bytesRead, target );
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

  // We will read every byte sequentially, so ask the kernel to start
  // pulling the whole file into the page cache up front. On macOS F_RDADVISE
  // is the most effective hint for this (MADV_WILLNEED is largely a no-op).
  // radvisory::ra_count is an int, so issue the advice in <=INT_MAX chunks.
  for ( size_t off = 0; off < fileSize; ) {
    size_t            remaining = fileSize - off;
    struct radvisory  ra;
    ra.ra_offset = (off_t)off;
    ra.ra_count  = (int)std::min( remaining, (size_t)INT_MAX );
    fcntl( fd, F_RDADVISE, &ra );
    off += (size_t)ra.ra_count;
  }

  unsigned numThreads = (unsigned)std::min(
      (size_t)MAX_THREADS,
      ( fileSize + bytesPerThread - 1 ) / bytesPerThread
  );
  numThreads = std::max( numThreads, 1u );

  size_t chunkSize = fileSize / numThreads;

  std::vector<size_t>      counts( numThreads, 0 );
  std::vector<std::thread> threads;
  threads.reserve( numThreads );

  // Each thread streams its byte range with pread() into a private, reused
  // buffer rather than faulting an mmap page-by-page. pread() is thread-safe
  // (the offset is per-call, not shared via fd), so a single fd serves all
  // threads, and the kernel bulk-copies straight from the warmed page cache.
  for ( unsigned i = 0; i < numThreads; ++i ) {
    off_t  start = (off_t)( i * chunkSize );
    size_t size  = ( i == numThreads - 1 )
                   ? fileSize - i * chunkSize
                   : chunkSize;
    threads.emplace_back( [fd, start, size, &counts, i, target]() {
      static const size_t BUF_SIZE = 1 << 20;  // 1 MiB
      std::vector<char>   buffer( BUF_SIZE );
      size_t lines     = 0;
      size_t remaining = size;
      off_t  pos       = start;
      while ( remaining > 0 ) {
        size_t  want = std::min( BUF_SIZE, remaining );
        ssize_t got  = pread( fd, buffer.data(), want, pos );
        if ( got <= 0 ) break;
        lines     += countLines( buffer.data(), (size_t)got, target );
        remaining -= (size_t)got;
        pos       += got;
      }
      counts[i] = lines;
    } );
  }

  for ( auto& t : threads ) t.join();
  close( fd );

  size_t total = 0;
  for ( size_t c : counts ) total += c;
  return total;
}