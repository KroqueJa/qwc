#include "processfile.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <iostream>
#include <thread>
#include <vector>

#include "countlines.h"

static const u32 MAX_THREADS = std::thread::hardware_concurrency();

usize processFile( const char* filename, usize bytesPerThread, char target )
{
  if ( filename[0] == '\0' ) {
    static thread_local char buffer[128 * 4096];
    isize bytesRead;
    usize totalLines = 0;
    while ( ( bytesRead = read( 0, buffer, sizeof( buffer ) ) ) > 0 )
      totalLines +=
          countLines( buffer, static_cast<usize>( bytesRead ), target );
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
  usize fileSize = static_cast<usize>( st.st_size );

  if ( fileSize == 0 ) {
    close( fd );
    return 0;
  }

  // We will read every byte sequentially, so ask the kernel to start
  // pulling the whole file into the page cache up front. On macOS F_RDADVISE
  // is the most effective hint for this (MADV_WILLNEED is largely a no-op).
  // radvisory::ra_count is an int, so issue the advice in <=INT_MAX chunks.
  for ( usize off = 0; off < fileSize; ) {
    usize remaining = fileSize - off;
    struct radvisory ra;
    ra.ra_offset = static_cast<i64>( off );
    ra.ra_count = static_cast<int>(
        std::min( remaining, static_cast<usize>( INT_MAX ) )
    );
    fcntl( fd, F_RDADVISE, &ra );
    off += static_cast<usize>( ra.ra_count );
  }

  u32 numThreads = static_cast<u32>( std::min(
      static_cast<usize>( MAX_THREADS ),
      ( fileSize + bytesPerThread - 1 ) / bytesPerThread
  ) );
  numThreads = std::max( numThreads, 1u );

  usize chunkSize = fileSize / numThreads;

  std::vector<usize> counts( numThreads, 0 );
  std::vector<std::thread> threads;
  threads.reserve( numThreads );

  // Each thread streams its byte range with pread() into a private, reused
  // buffer rather than faulting an mmap page-by-page. pread() is thread-safe
  // (the offset is per-call, not shared via fd), so a single fd serves all
  // threads, and the kernel bulk-copies straight from the warmed page cache.
  for ( u32 i = 0; i < numThreads; ++i ) {
    i64 start = static_cast<i64>( i * chunkSize );
    usize size = ( i == numThreads - 1 ) ? fileSize - i * chunkSize : chunkSize;
    threads.emplace_back( [fd, start, size, &counts, i, target]() {
      static const usize BUF_SIZE = 1 << 20;  // 1 MiB
      std::vector<char> buffer( BUF_SIZE );
      usize lines = 0;
      usize remaining = size;
      i64 pos = start;
      while ( remaining > 0 ) {
        usize want = std::min( BUF_SIZE, remaining );
        isize got = pread( fd, buffer.data(), want, pos );
        if ( got <= 0 ) break;
        lines += countLines( buffer.data(), static_cast<usize>( got ), target );
        remaining -= static_cast<usize>( got );
        pos += got;
      }
      counts[i] = lines;
    } );
  }

  for ( auto& t: threads ) t.join();
  close( fd );

  usize total = 0;
  for ( usize c: counts ) total += c;
  return total;
}