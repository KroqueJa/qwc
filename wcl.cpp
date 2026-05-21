#include <fcntl.h>
#include <immintrin.h>
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <string>

// Struct to keep the results clean
struct Result
{
  std::string str;
  size_t lineCount;
};

// Files to process and atomic index into them
std::vector<std::string> files;
std::atomic<size_t>      nextFile = 0;

// Pre-sized output vector — each thread writes to its own slot, no locking
std::vector<Result> output;

// Function to process a file path and put a `Result` struct into the `output`
// vector
void processFile()
{
  while ( true ) {
    // ---- Claim a file slot atomically ----
    size_t idx = nextFile.fetch_add( 1 );
    if ( idx >= files.size() ) return;
    const std::string& filename = files[idx];

    // ---- Open a file (or read STDIN) ----
    int fd;
    if ( filename.empty() ) {  // If filename is empty, read from STDIN
      fd = 0;
    } else {
      fd = open( filename.c_str(), O_RDONLY );
      if ( fd < 0 ) {
        std::cerr << "Error opening file: " << filename << '\n';
        exit( 1 );
      }
      posix_fadvise( fd, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE );
    }

    // Define a buffer for chunks of the file
    const size_t bufferSize = 128 * 4096;
    char buffer[bufferSize];
    ssize_t bytesRead;

    __m256i chunk1, chunk2, result1, result2;
    int mask1, mask2;

    // ---- Read the file into the buffer, and count newlines in each chunk ----
    size_t totalLines = 0;
    while ( ( bytesRead = read( fd, buffer, bufferSize ) ) > 0 ) {
      __m256i vec_target = _mm256_set1_epi8( '\n' );
      size_t lines = 0;
      char* tmp = buffer;
      size_t processedBytes = 0;

      // Align to 32-byte boundary
      while ( processedBytes < (size_t)bytesRead && ( (uintptr_t)tmp % 32 != 0 ) ) {
        if ( *tmp == '\n' ) ++lines;
        ++tmp;
        ++processedBytes;
      }

      // Process buffer in 64-byte chunks
      while ( processedBytes + 63 < (size_t)bytesRead ) {
        chunk1  = _mm256_loadu_si256( (__m256i*)tmp );
        chunk2  = _mm256_loadu_si256( (__m256i*)( tmp + 32 ) );
        result1 = _mm256_cmpeq_epi8( chunk1, vec_target );
        result2 = _mm256_cmpeq_epi8( chunk2, vec_target );
        mask1   = _mm256_movemask_epi8( result1 );
        mask2   = _mm256_movemask_epi8( result2 );
        lines  += _mm_popcnt_u32( mask1 ) + _mm_popcnt_u32( mask2 );
        tmp    += 64;
        processedBytes += 64;
      }

      // Process remaining bytes
      while ( processedBytes < (size_t)bytesRead ) {
        if ( *tmp == '\n' ) ++lines;
        ++tmp;
        ++processedBytes;
      }

      totalLines += lines;
    }

    if ( !filename.empty() ) close( fd );

    // Write directly to pre-assigned slot — no lock needed
    output[idx] = { std::to_string( totalLines ) + " " + filename, totalLines };
  }
}

int main( int argc, char** argv )
{
  if ( argc == 1 ) {
    // No arguments — read from STDIN
    files.push_back( "" );
    output.resize( 1 );
    processFile();
    std::cout << output[0].lineCount << std::endl;
  } else {
    files.assign( argv + 1, argv + argc );
    output.resize( files.size() );

    unsigned numThreads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads( numThreads );
    for ( unsigned i = 0; i < numThreads; ++i )
      threads[i] = std::thread( processFile );
    for ( auto& t : threads ) t.join();

    size_t total = 0;
    for ( const auto& result : output ) {
      total += result.lineCount;
      if ( argc > 2 ) std::cout << result.str << '\n';
    }
    std::cout << total << std::endl;
  }

  return 0;
}