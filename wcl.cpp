#include <fcntl.h>
#include <immintrin.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <algorithm>

// ============================================================
// Types
// ============================================================

struct Result
{
  std::string str;
  size_t lineCount;
};

// ============================================================
// Tuning
// ============================================================

static const size_t BYTES_PER_THREAD = 64 * 1024 * 1024; // 64 MB
static const unsigned MAX_THREADS    = std::thread::hardware_concurrency();

// ============================================================
// SIMD line counter
// ============================================================

size_t countLines( const char* buffer, size_t length )
{
  __m256i vec_target = _mm256_set1_epi8( '\n' );
  __m256i chunk1, chunk2, chunk3, chunk4;
  __m256i res1,   res2,   res3,   res4;
  int     mask1,  mask2,  mask3,  mask4;

  size_t lines = 0;
  const char* tmp = buffer;
  size_t processedBytes = 0;

  // Align to 32-byte boundary
  while ( processedBytes < length && ( (uintptr_t)tmp % 32 != 0 ) ) {
    if ( *tmp == '\n' ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  // Process in 128-byte chunks
  while ( processedBytes + 127 < length ) {
    chunk1 = _mm256_loadu_si256( (__m256i*)tmp        );
    chunk2 = _mm256_loadu_si256( (__m256i*)( tmp + 32 ) );
    chunk3 = _mm256_loadu_si256( (__m256i*)( tmp + 64 ) );
    chunk4 = _mm256_loadu_si256( (__m256i*)( tmp + 96 ) );
    res1   = _mm256_cmpeq_epi8( chunk1, vec_target );
    res2   = _mm256_cmpeq_epi8( chunk2, vec_target );
    res3   = _mm256_cmpeq_epi8( chunk3, vec_target );
    res4   = _mm256_cmpeq_epi8( chunk4, vec_target );
    mask1  = _mm256_movemask_epi8( res1 );
    mask2  = _mm256_movemask_epi8( res2 );
    mask3  = _mm256_movemask_epi8( res3 );
    mask4  = _mm256_movemask_epi8( res4 );
    lines += _mm_popcnt_u32( mask1 ) + _mm_popcnt_u32( mask2 )
           + _mm_popcnt_u32( mask3 ) + _mm_popcnt_u32( mask4 );
    tmp            += 128;
    processedBytes += 128;
  }

  // Remaining bytes
  while ( processedBytes < length ) {
    if ( *tmp == '\n' ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  return lines;
}

// ============================================================
// Process a single file — mmap + scale threads to size
// ============================================================

size_t processFile( const char* filename )
{
  int fd;
  if ( filename[0] == '\0' ) {
    // STDIN — can't mmap, fall back to read loop
    static thread_local char buffer[128 * 4096];
    ssize_t bytesRead;
    size_t totalLines = 0;
    while ( ( bytesRead = read( 0, buffer, sizeof( buffer ) ) ) > 0 )
      totalLines += countLines( buffer, (size_t)bytesRead );
    return totalLines;
  }

  fd = open( filename, O_RDONLY );
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
  close( fd ); // safe to close after mmap
  if ( mapped == MAP_FAILED ) {
    std::cerr << "mmap failed: " << filename << '\n';
    exit( 1 );
  }
  madvise( mapped, fileSize, MADV_SEQUENTIAL );

  // Scale thread count to file size, capped at MAX_THREADS
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

// ============================================================
// main
// ============================================================

int main( int argc, char** argv )
{
  if ( argc == 1 ) {
    std::cout << processFile( "" ) << std::endl;
    return 0;
  }

  std::vector<Result> output( argc - 1 );

  // Process files concurrently — but cap total active threads.
  // Each file may spawn up to MAX_THREADS internally, so we process
  // files one at a time from a pool to avoid oversubscription.
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