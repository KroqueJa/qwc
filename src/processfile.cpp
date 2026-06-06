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
#include "words.h"

static const u32 MAX_THREADS = std::thread::hardware_concurrency();

usize processFile(
    const char* filename, const usize bytesPerThread, char target,
    const CountMode mode
)
{
  if ( filename[0] == '\0' ) {
    thread_local char buffer[128 * 4096];
    isize bytesRead;
    usize total = 0;
    bool inWord = false;  // carried across reads for word counting
    while ( ( bytesRead = read( 0, buffer, sizeof( buffer ) ) ) > 0 ) {
      const usize got = static_cast<usize>( bytesRead );
      if ( mode == CountMode::Bytes )
        total += got;
      else if ( mode == CountMode::Words )
        total += words( buffer, got, inWord );
      else
        total += count( buffer, got, target );
    }
    return total;
  }

  int fd = open( filename, O_RDONLY );
  if ( fd < 0 ) {
    std::cerr << "Error opening file: " << filename << '\n';
    exit( 1 );
  }

  struct stat st{};
  if ( fstat( fd, &st ) < 0 ) {
    std::cerr << "Error stating file: " << filename << '\n';
    exit( 1 );
  }
  const usize fileSize = st.st_size;

  // Byte counting (`wc -c`) needs nothing but the size we just stat'd: skip the
  // readahead and the parallel scan entirely.
  if ( mode == CountMode::Bytes ) {
    close( fd );
    return fileSize;
  }

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
    radvisory ra{};
    ra.ra_offset = static_cast<i64>( off );
    ra.ra_count = static_cast<int>(
        std::min( remaining, static_cast<usize>( INT_MAX ) )
    );
    fcntl( fd, F_RDADVISE, &ra );
    off += static_cast<usize>( ra.ra_count );
  }

  // Word counting carries the in-a-word state across buffer boundaries, so for
  // now a single thread streams the whole file in order. (Chunked, vectorised
  // word counting is a later step.)
  if ( mode == CountMode::Words ) {
    static constexpr usize BUF_SIZE = 1 << 20;  // 1 MiB
    std::vector<char> buffer( BUF_SIZE );
    usize total = 0;
    bool inWord = false;
    usize pos = 0;
    while ( pos < fileSize ) {
      const usize want = std::min( BUF_SIZE, fileSize - pos );
      const isize got = pread( fd, buffer.data(), want, static_cast<off_t>( pos ) );
      if ( got <= 0 ) break;
      total += words( buffer.data(), static_cast<usize>( got ), inWord );
      pos += static_cast<usize>( got );
    }
    close( fd );
    return total;
  }

  u32 numThreads = static_cast<u32>( std::min(
      static_cast<usize>( MAX_THREADS ),
      ( fileSize + bytesPerThread - 1 ) / bytesPerThread
  ) );
  numThreads = std::max( numThreads, 1u );

  const usize chunkSize = fileSize / numThreads;

  std::vector<usize> counts( numThreads, 0 );
  std::vector<std::thread> threads;
  threads.reserve( numThreads );

  // Each thread streams its byte range with pread() into a private, reused
  // buffer rather than faulting an mmap page-by-page. pread() is thread-safe
  // (the offset is per-call, not shared via fd), so a single fd serves all
  // threads, and the kernel bulk-copies straight from the warmed page cache.
  for ( u32 i = 0; i < numThreads; ++i ) {
    usize start = i * chunkSize;
    usize size = ( i == numThreads - 1 ) ? fileSize - i * chunkSize : chunkSize;
    threads.emplace_back( [fd, start, size, &counts, i, target]() {
      static constexpr usize BUF_SIZE = 1 << 20;  // 1 MiB
      std::vector<char> buffer( BUF_SIZE );
      usize lines = 0;
      usize remaining = size;
      usize pos = start;
      while ( remaining > 0 ) {
        const usize want = std::min( BUF_SIZE, remaining );
        const isize got = pread( fd, buffer.data(), want, static_cast<off_t>( pos ) );
        if ( got <= 0 ) break;
        lines += count( buffer.data(), static_cast<usize>( got ), target );
        remaining -= static_cast<usize>( got );
        pos += static_cast<usize>( got );
      }
      counts[i] = lines;
    } );
  }

  for ( auto& t: threads ) t.join();
  close( fd );

  usize total = 0;
  for ( const usize c: counts ) total += c;
  return total;
}