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

// ============================================================
// Types
// ============================================================

struct Result
{
  std::string str;
  size_t lineCount;
};

// ============================================================
// Globals
// ============================================================

std::vector<std::string> files;
std::atomic<size_t>      nextFile = 0;

static const size_t BIG_FILE_THRESHOLD = 256 * 1024 * 1024;

// ============================================================
// SIMD line counter
// ============================================================

size_t countLines( const char* buffer, ssize_t bytesRead )
{
  __m256i vec_target = _mm256_set1_epi8( '\n' );
  __m256i chunk1, chunk2, chunk3, chunk4;
  __m256i res1,   res2,   res3,   res4;
  int     mask1,  mask2,  mask3,  mask4;

  size_t lines = 0;
  const char* tmp = buffer;
  size_t processedBytes = 0;

  // Align to 32-byte boundary
  while ( processedBytes < (size_t)bytesRead && ( (uintptr_t)tmp % 32 != 0 ) ) {
    if ( *tmp == '\n' ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  // Process in 128-byte chunks
  while ( processedBytes + 127 < (size_t)bytesRead ) {
    chunk1 = _mm256_loadu_si256( (__m256i*)tmp         );
    chunk2 = _mm256_loadu_si256( (__m256i*)( tmp + 32  ) );
    chunk3 = _mm256_loadu_si256( (__m256i*)( tmp + 64  ) );
    chunk4 = _mm256_loadu_si256( (__m256i*)( tmp + 96  ) );
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
  while ( processedBytes < (size_t)bytesRead ) {
    if ( *tmp == '\n' ) ++lines;
    ++tmp;
    ++processedBytes;
  }

  return lines;
}

// ============================================================
// Big file handler — mmap + parallel count
// ============================================================

size_t processBigFile( int fd, size_t fileSize )
{
  char* mapped = (char*)mmap( NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0 );
  if ( mapped == MAP_FAILED ) {
    std::cerr << "mmap failed\n";
    exit( 1 );
  }
  madvise( mapped, fileSize, MADV_SEQUENTIAL );

  unsigned numThreads = std::thread::hardware_concurrency();
  size_t chunkSize    = fileSize / numThreads;

  std::vector<size_t>      counts( numThreads, 0 );
  std::vector<std::thread> threads;
  threads.reserve( numThreads );

  for ( unsigned i = 0; i < numThreads; ++i ) {
    char*  start = mapped + i * chunkSize;
    size_t size  = ( i == numThreads - 1 )
                   ? fileSize - i * chunkSize
                   : chunkSize;

    threads.emplace_back( [start, size, &counts, i]() {
      counts[i] = countLines( start, (ssize_t)size );
    } );
  }

  for ( auto& t : threads ) t.join();
  munmap( mapped, fileSize );

  size_t total = 0;
  for ( size_t c : counts ) total += c;
  return total;
}

// ============================================================
// Per-thread file processor
// ============================================================

void processFile( std::vector<Result>& localOutput )
{
  static thread_local char buffer[128 * 4096];

  while ( true ) {
    size_t idx = nextFile.fetch_add( 1 );
    if ( idx >= files.size() ) return;
    const std::string& filename = files[idx];

    int fd;
    if ( filename.empty() ) {
      fd = 0;
    } else {
      fd = open( filename.c_str(), O_RDONLY );
      if ( fd < 0 ) {
        std::cerr << "Error opening file: " << filename << '\n';
        exit( 1 );
      }

      struct stat st;
      if ( fstat( fd, &st ) == 0 && (size_t)st.st_size > BIG_FILE_THRESHOLD ) {
        size_t totalLines = processBigFile( fd, (size_t)st.st_size );
        close( fd );
        localOutput.push_back( { std::to_string( totalLines ) + " " + filename, totalLines } );
        continue;
      }

      posix_fadvise( fd, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE );
    }

    ssize_t bytesRead;
    size_t totalLines = 0;
    while ( ( bytesRead = read( fd, buffer, 128 * 4096 ) ) > 0 )
      totalLines += countLines( buffer, bytesRead );

    if ( !filename.empty() ) close( fd );

    localOutput.push_back( { std::to_string( totalLines ) + " " + filename, totalLines } );
  }
}

// ============================================================
// main
// ============================================================

int main( int argc, char** argv )
{
  if ( argc == 1 ) {
    files.push_back( "" );
    std::vector<Result> localOutput;
    processFile( localOutput );
    std::cout << localOutput[0].lineCount << std::endl;
  } else {
    files.assign( argv + 1, argv + argc );

    unsigned numThreads = std::thread::hardware_concurrency();
    std::vector<std::vector<Result>> threadOutputs( numThreads );
    std::vector<std::thread> threads( numThreads );

    for ( unsigned i = 0; i < numThreads; ++i )
      threads[i] = std::thread( processFile, std::ref( threadOutputs[i] ) );
    for ( auto& t : threads ) t.join();

    size_t total = 0;
    for ( const auto& localOutput : threadOutputs ) {
      for ( const auto& result : localOutput ) {
        total += result.lineCount;
        if ( argc > 2 ) std::cout << result.str << '\n';
      }
    }
    std::cout << total << std::endl;
  }

  return 0;
}