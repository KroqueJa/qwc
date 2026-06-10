#include "processfile.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include "chars.h"
#include "countlines.h"
#include "maxlinelen.h"
#include "words.h"

static const u32 MAX_THREADS = std::thread::hardware_concurrency();

namespace {

// Per-stream/per-chunk running state for every scanned counter. `inWord` and
// `line` carry across the successive read buffers of one byte range; the
// startsInWord/endsInWord edges let neighbouring chunks be stitched.
struct ScanState
{
  usize lines = 0;
  usize words = 0;
  usize chars = 0;
  usize target = 0;
  bool inWord = false;  // word-counting carry within this range
  bool startsInWord =
      false;  // first byte non-whitespace (for cross-chunk stitch)
  bool sawFirst = false;
  LineScan line;  // longest-line carry within this range
};

// Run every requested counter over one buffer, threading the carries in `s`.
// Each counter scans the (cache-resident) buffer independently; the data is
// read from the file only once, by the caller.
inline void scanBuffer(
    const char* buf, usize g, const Workload& w, ScanState& s
)
{
  if ( w.lines ) s.lines += count( buf, g, '\n' );
  if ( w.target ) s.target += count( buf, g, w.targetByte );
  if ( w.words ) {
    if ( !s.sawFirst && g > 0 ) s.startsInWord = !isWordSpace( buf[0] );
    s.words += words( buf, g, s.inWord );
  }

  // chars and the longest line interact: `wc -L -m` measures the longest line in
  // characters, and that count and the character total walk the same UTF-8
  // continuation bytes. When both are wanted, one fused pass produces both;
  // otherwise each is computed on its own. (w.maxLineInChars implies w.chars --
  // both are set exactly by -m -- so the fused path covers the char total.)
  if ( w.maxLine && w.maxLineInChars ) {
    maxLineLenChars( buf, g, s.line, s.chars );
  } else {
    if ( w.chars ) s.chars += chars( buf, g );
    if ( w.maxLine ) maxLineLen( buf, g, s.line, w.maxLineInChars );
  }

  if ( g > 0 ) s.sawFirst = true;
}

// Serial single-pass scan of a stream that can only be consumed once and whose
// length fstat cannot give us up front: standard input, and any non-regular
// file (FIFO, character/block device, socket, /proc, ...). Bytes are tallied as
// we read, since there is no reliable st_size. This is the boring,
// always-correct path -- no threads, no pread, no size assumptions.
Counts processStream( int fd, const Workload& w )
{
  thread_local char buffer[128 * 4096];
  ScanState s;
  Counts c{};
  isize bytesRead;
  while ( ( bytesRead = read( fd, buffer, sizeof( buffer ) ) ) > 0 ) {
    const auto g = static_cast<usize>( bytesRead );
    if ( w.bytes ) c.bytes += g;  // no usable fstat size; tally as we read
    scanBuffer( buffer, g, w, s );
  }
  c.lines = s.lines;
  c.words = s.words;
  c.chars = s.chars;
  c.target = s.target;
  // wc -L ignores a trailing line with no final newline, so drop the open run.
  if ( w.maxLine ) c.maxLine = s.line.maxComplete;
  return c;
}

}  // namespace

Counts processFile(
    const char* filename, const Workload& work, const usize bytesPerThread
)
{
  if ( filename[0] == '\0' ) return processStream( 0, work );

  // processFile runs on worker threads (see mapFiles), so these fatal I/O-error
  // paths use _Exit: it terminates immediately without running atexit handlers
  // or static destructors, which would otherwise race with the live workers (the
  // cerr message is already flushed, and no counts have been printed yet).
  int fd = open( filename, O_RDONLY );
  if ( fd < 0 ) {
    std::cerr << "Error opening file: " << filename << '\n';
    std::_Exit( 1 );
  }

  struct stat st{};
  if ( fstat( fd, &st ) < 0 ) {
    std::cerr << "Error stating file: " << filename << '\n';
    std::_Exit( 1 );
  }

  const usize fileSize = st.st_size;

  // Take the parallel fast path only for a regular file with a trustworthy,
  // nonzero size. Everything else goes through the serial stream scan:
  //   * non-regular inputs (FIFO, device, socket) -- no size, and pread can't
  //     seek them; and
  //   * regular files whose fstat size is 0 -- either a genuinely empty file, or
  //     a procfs/sysfs file that lies about its length and actually has content.
  // We cannot chunk what we cannot size, and must not assume 0 means empty, so
  // such inputs are simply read to EOF: correct on everything, while the common
  // case (a normal file with a real size) keeps the threaded fast path.
  if ( !S_ISREG( st.st_mode ) || fileSize == 0 ) {
    const Counts c = processStream( fd, work );
    close( fd );
    return c;
  }

  Counts result{};
  if ( work.bytes ) result.bytes = fileSize;  // bytes come straight from fstat

  // Nothing to scan (e.g. a bare `-c`): the fstat byte count is all we need.
  if ( !work.needsScan() ) {
    close( fd );
    return result;
  }

  // We will read every byte sequentially, so ask the kernel to start pulling
  // the whole file into the page cache up front.
#if defined( __APPLE__ )
  // On macOS F_RDADVISE is the most effective hint (MADV_WILLNEED is largely a
  // no-op). radvisory::ra_count is an int, so issue the advice in <=INT_MAX
  // chunks.
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
#elif defined( POSIX_FADV_SEQUENTIAL )
  // On Linux posix_fadvise gives the kernel both the access pattern and an
  // explicit readahead hint over the whole range.
  posix_fadvise( fd, 0, static_cast<off_t>( fileSize ), POSIX_FADV_SEQUENTIAL );
  posix_fadvise( fd, 0, static_cast<off_t>( fileSize ), POSIX_FADV_WILLNEED );
#endif

  u32 numThreads = static_cast<u32>( std::min(
      static_cast<usize>( MAX_THREADS ),
      ( fileSize + bytesPerThread - 1 ) / bytesPerThread
  ) );
  numThreads = std::max( numThreads, 1u );

  const usize chunkSize = fileSize / numThreads;

  std::vector<ScanState> results( numThreads );
  std::vector<std::thread> threads;
  threads.reserve( numThreads );

  // Each thread streams its byte range with pread() into a private, reused
  // buffer rather than faulting an mmap page-by-page. pread() is thread-safe
  // (the offset is per-call, not shared via fd), so one fd serves all threads
  // and the kernel bulk-copies straight from the warmed page cache. The whole
  // workload is computed on each buffer before moving on -- one read, every
  // requested counter.
  for ( u32 i = 0; i < numThreads; ++i ) {
    usize start = i * chunkSize;
    usize size = ( i == numThreads - 1 ) ? fileSize - i * chunkSize : chunkSize;
    threads.emplace_back( [fd, start, size, &results, i, &work]() {
      static constexpr usize BUF_SIZE = 1 << 20;  // 1 MiB
      std::vector<char> buffer( BUF_SIZE );
      ScanState s;
      usize remaining = size;
      usize pos = start;
      while ( remaining > 0 ) {
        const usize want = std::min( BUF_SIZE, remaining );
        const isize got =
            pread( fd, buffer.data(), want, static_cast<off_t>( pos ) );
        if ( got <= 0 ) break;
        const auto g = static_cast<usize>( got );
        scanBuffer( buffer.data(), g, work, s );
        remaining -= g;
        pos += g;
      }
      results[i] = s;
    } );
  }

  for ( auto& t: threads ) t.join();
  close( fd );

  // Lines, chars and target just sum. Words need a boundary stitch: each chunk
  // counted as if whitespace preceded it, so a word straddling a boundary is
  // counted twice (tail of the left chunk, fresh start of the right). When the
  // previous chunk ended mid-word and this one starts mid-word, drop the dupe.
  bool wordCarry = false;
  for ( const ScanState& s: results ) {
    result.lines += s.lines;
    result.chars += s.chars;
    result.target += s.target;
    result.words += s.words;
    if ( wordCarry && s.startsInWord ) --result.words;
    wordCarry = s.inWord;
  }

  // Longest line: a line split across a boundary is the previous chunk's open
  // run (`carry`) plus this chunk's prefix (bytes up to its first newline), but
  // only a newline realizes it as a counted line. The run still open after the
  // last chunk has no terminating newline, so -- like wc -L -- it is dropped.
  usize maxLen = 0;
  usize lineCarry = 0;
  for ( const ScanState& s: results ) {
    maxLen = std::max( maxLen, s.line.maxComplete );
    if ( s.line.hasNewline ) {
      maxLen = std::max( maxLen, lineCarry + s.line.prefixLen );
      lineCarry = s.line.cur;
    } else {
      lineCarry += s.line.cur;
    }
  }
  result.maxLine = maxLen;

  return result;
}
