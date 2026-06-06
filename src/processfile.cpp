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
      const auto got = static_cast<usize>( bytesRead );
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

  u32 numThreads = static_cast<u32>( std::min(
      static_cast<usize>( MAX_THREADS ),
      ( fileSize + bytesPerThread - 1 ) / bytesPerThread
  ) );
  numThreads = std::max( numThreads, 1u );

  const usize chunkSize = fileSize / numThreads;

  // Per-chunk tally. `count` is what the chunk found scanning in isolation (as
  // if preceded by whitespace). For word counting we also record the edge
  // states so neighboring chunks can be stitched: `startsInWord` (first byte
  // is non-whitespace) and `endsInWord` (last byte is non-whitespace -- the
  // carry handed to the next chunk). Both are unused for line/byte counting.
  struct ChunkTally
  {
    usize count = 0;
    bool startsInWord = false;
    bool endsInWord = false;
  };
  std::vector<ChunkTally> results( numThreads );
  std::vector<std::thread> threads;
  threads.reserve( numThreads );

  // Each thread streams its byte range with pread() into a private, reused
  // buffer rather than faulting an mmap page-by-page. pread() is thread-safe
  // (the offset is per-call, not shared via fd), so a single fd serves all
  // threads, and the kernel bulk-copies straight from the warmed page cache.
  for ( u32 i = 0; i < numThreads; ++i ) {
    usize start = i * chunkSize;
    usize size = ( i == numThreads - 1 ) ? fileSize - i * chunkSize : chunkSize;
    threads.emplace_back( [fd, start, size, &results, i, target, mode]() {
      static constexpr usize BUF_SIZE = 1 << 20;  // 1 MiB
      std::vector<char> buffer( BUF_SIZE );
      ChunkTally r;
      bool inWord = false;  // carried across this chunk's own buffers
      bool first = true;
      usize remaining = size;
      usize pos = start;
      while ( remaining > 0 ) {
        const usize want = std::min( BUF_SIZE, remaining );
        const isize got = pread( fd, buffer.data(), want, static_cast<off_t>( pos ) );
        if ( got <= 0 ) break;
        const auto g = static_cast<usize>( got );
        if ( mode == CountMode::Words ) {
          if ( first ) r.startsInWord = !isWordSpace( buffer[0] );
          r.count += words( buffer.data(), g, inWord );
        } else {
          r.count += count( buffer.data(), g, target );
        }
        first = false;
        remaining -= g;
        pos += g;
      }
      if ( mode == CountMode::Words ) r.endsInWord = inWord;
      results[i] = r;
    } );
  }

  for ( auto& t: threads ) t.join();
  close( fd );

  if ( mode == CountMode::Words ) {
    // Stitch the chunks back together in order. Each chunk counted as if a
    // whitespace preceded it, so a word straddling a boundary is counted twice:
    // once as the tail of the left chunk, once as a fresh word starting the
    // right chunk. `carry` tracks whether the previous chunk ended mid-word; if
    // it did and this chunk also starts mid-word, the two are halves of one
    // word -- drop the duplicate. (Chunks are non-empty here, since numThreads
    // never exceeds fileSize, so the carry never has to skip past a chunk.)
    usize total = 0;
    bool carry = false;
    for ( const auto& [count, startsInWord, endsInWord]: results ) {
      total += count;
      if ( carry && startsInWord ) --total;
      carry = endsInWord;
    }
    return total;
  }

  usize total = 0;
  for ( const ChunkTally& r: results ) total += r.count;
  return total;
}

Counts processFileAll( const char* filename, const usize bytesPerThread )
{
  // stdin: a single sequential pass tallies all three at once -- it has to,
  // since fd 0 can only be read once.
  if ( filename[0] == '\0' ) {
    thread_local char buffer[128 * 4096];
    isize bytesRead;
    Counts c{};
    bool inWord = false;  // carried across reads for word counting
    while ( ( bytesRead = read( 0, buffer, sizeof( buffer ) ) ) > 0 ) {
      const auto got = static_cast<usize>( bytesRead );
      c.lines += count( buffer, got, '\n' );
      c.words += words( buffer, got, inWord );
      c.bytes += got;
    }
    return c;
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

  Counts result{};
  result.bytes = st.st_size;  // bytes come straight from the stat
  if ( result.bytes == 0 ) {
    close( fd );
    return result;
  }
  const usize fileSize = result.bytes;

  // Warm the page cache up front (see processFile for the F_RDADVISE rationale).
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

  u32 numThreads = static_cast<u32>( std::min(
      static_cast<usize>( MAX_THREADS ),
      ( fileSize + bytesPerThread - 1 ) / bytesPerThread
  ) );
  numThreads = std::max( numThreads, 1u );

  const usize chunkSize = fileSize / numThreads;

  // Per-chunk lines and words, plus the word-edge flags used to stitch words
  // across chunk boundaries (see processFile's word path).
  struct AllTally
  {
    usize lines = 0;
    usize words = 0;
    bool startsInWord = false;
    bool endsInWord = false;
  };
  std::vector<AllTally> results( numThreads );
  std::vector<std::thread> threads;
  threads.reserve( numThreads );

  for ( u32 i = 0; i < numThreads; ++i ) {
    usize start = i * chunkSize;
    usize size = ( i == numThreads - 1 ) ? fileSize - i * chunkSize : chunkSize;
    threads.emplace_back( [fd, start, size, &results, i]() {
      static constexpr usize BUF_SIZE = 1 << 20;  // 1 MiB
      std::vector<char> buffer( BUF_SIZE );
      AllTally t;
      bool inWord = false;  // carried across this chunk's own buffers
      bool first = true;
      usize remaining = size;
      usize pos = start;
      while ( remaining > 0 ) {
        const usize want = std::min( BUF_SIZE, remaining );
        const isize got = pread( fd, buffer.data(), want, static_cast<off_t>( pos ) );
        if ( got <= 0 ) break;
        const auto g = static_cast<usize>( got );
        t.lines += count( buffer.data(), g, '\n' );
        if ( first ) t.startsInWord = !isWordSpace( buffer[0] );
        t.words += words( buffer.data(), g, inWord );
        first = false;
        remaining -= g;
        pos += g;
      }
      t.endsInWord = inWord;
      results[i] = t;
    } );
  }

  for ( auto& t: threads ) t.join();
  close( fd );

  // Lines and bytes just sum; words need the same boundary stitch as the
  // word-only path (drop a word split across two chunks, counted twice).
  bool carry = false;
  for ( const AllTally& t: results ) {
    result.lines += t.lines;
    result.words += t.words;
    if ( carry && t.startsInWord ) --result.words;
    carry = t.endsInWord;
  }
  return result;
}