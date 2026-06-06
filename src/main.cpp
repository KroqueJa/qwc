#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "processfile.h"
#include "result.h"

static const u32 MAX_THREADS = std::thread::hardware_concurrency();

static void printHelp()
{
  std::cout <<
      "wcl - count things in files, fast.\n"
      "\n"
      "By default wcl counts the number of lines (newline characters) in each\n"
      "file you give it, just like `wc -l`. It reads big files in parallel, so\n"
      "it stays quick even on very large inputs.\n"
      "\n"
      "Usage:\n"
      "  wcl [options] [file ...]\n"
      "  wcl [options]            (reads from standard input)\n"
      "\n"
      "If you don't name any files, wcl reads from standard input - so you can\n"
      "pipe data straight into it, e.g.  cat access.log | wcl\n"
      "\n"
      "Options:\n"
      "  -c, --char C          Count occurrences of the byte C instead of\n"
      "                        newlines. Handy for tallying commas in a CSV\n"
      "                        (-c ,) or any other single character.\n"
      "  -r, --recursive       Treat directory arguments as whole trees: wcl\n"
      "                        walks into them and counts every file it finds,\n"
      "                        something wc can't do on its own.\n"
      "      --bytes-per-thread N\n"
      "                        Tune how much data each worker thread handles\n"
      "                        before another thread is spun up (default 64\n"
      "                        MiB). You rarely need this; it's here for\n"
      "                        squeezing out performance on unusual hardware.\n"
      "  -h, --help            Show this message and exit.\n"
      "\n"
      "Output:\n"
      "  Counting a single file (or piped input) prints just the number.\n"
      "  Counting several files prints one \"<count> <name>\" line per file,\n"
      "  followed by the grand total on its own line.\n"
      "\n"
      "Examples:\n"
      "  wcl notes.txt                 lines in notes.txt\n"
      "  wcl *.log                     lines in each log, plus a total\n"
      "  wcl --char , data.csv         commas in data.csv\n"
      "  wcl --recursive src           lines in every file under src/\n";
}

int main( int argc, char** argv )
{
  usize bytesPerThread = 64 * 1024 * 1024;

  // Byte to count. Defaults to '\n' (line counting). --char overrides it with
  // an arbitrary byte.
  char target = '\n';

  // --recursive expands any directory argument into all regular files beneath
  // it (something wc inexplicably can't do).
  bool recursive = false;
  int fileStart = 1;

  // Parse leading "--flag [value]" options; the first non-flag argument begins
  // the file list.
  auto isFlag = [&]( const char* arg, const char* shortName,
                     const char* longName ) {
    return std::strcmp( arg, shortName ) == 0 ||
           std::strcmp( arg, longName ) == 0;
  };

  while ( fileStart < argc && argv[fileStart][0] == '-' &&
          argv[fileStart][1] != '\0' ) {
    if ( std::strcmp( argv[fileStart], "--bytes-per-thread" ) == 0 ) {
      if ( fileStart + 1 >= argc ) {
        std::cerr << "Error: --bytes-per-thread requires a value\n";
        return 1;
      }
      bytesPerThread = std::strtoull( argv[fileStart + 1], nullptr, 10 );
      if ( bytesPerThread == 0 ) {
        std::cerr << "Error: --bytes-per-thread must be > 0\n";
        return 1;
      }
      fileStart += 2;
    } else if ( isFlag( argv[fileStart], "-c", "--char" ) ) {
      if ( fileStart + 1 >= argc || argv[fileStart + 1][0] == '\0' ) {
        std::cerr << "Error: --char requires a single-character value\n";
        return 1;
      }
      target = argv[fileStart + 1][0];
      fileStart += 2;
    } else if ( isFlag( argv[fileStart], "-r", "--recursive" ) ) {
      recursive = true;
      fileStart += 1;
    } else if ( isFlag( argv[fileStart], "-h", "--help" ) ) {
      printHelp();
      return 0;
    } else {
      std::cerr << "Error: unknown flag " << argv[fileStart] << '\n';
      return 1;
    }
  }

  if ( fileStart == argc ) {
    std::cout << processFile( "", bytesPerThread, target ) << std::endl;
    return 0;
  }

  // Build the list of files to count. With --recursive, a directory argument is
  // replaced by every regular file beneath it, sorted for stable output; plain
  // file arguments and the top-level argument order are left untouched.
  namespace fs = std::filesystem;
  std::vector<std::string> files;
  for ( int i = fileStart; i < argc; ++i ) {
    std::error_code ec;
    if ( recursive && fs::is_directory( argv[i], ec ) ) {
      std::vector<std::string> dirFiles;
      fs::recursive_directory_iterator it(
          argv[i], fs::directory_options::skip_permission_denied, ec
      );
      if ( ec ) {
        std::cerr << "Error reading directory: " << argv[i] << '\n';
        return 1;
      }
      const fs::recursive_directory_iterator end;
      for ( ; it != end; it.increment( ec ) ) {
        if ( ec ) continue;  // skip entries we can't stat and keep going
        if ( it->is_regular_file( ec ) && !ec )
          dirFiles.push_back( it->path().string() );
      }
      std::sort( dirFiles.begin(), dirFiles.end() );
      files.insert( files.end(), dirFiles.begin(), dirFiles.end() );
    } else {
      files.emplace_back( argv[i] );
    }
  }

  const usize numFiles = files.size();
  std::vector<Result> output( numFiles );
  std::atomic<usize> nextFile = 0;
  std::vector<std::thread> pool( MAX_THREADS );

  for ( auto& t: pool ) {
    t = std::thread( [&]() {
      while ( true ) {
        const usize idx = nextFile.fetch_add( 1 );
        if ( idx >= numFiles ) return;
        const std::string& filename = files[idx];
        const usize lines =
            processFile( filename.c_str(), bytesPerThread, target );
        output[idx] = { std::to_string( lines ) + " " + filename, lines };
      }
    } );
  }

  for ( auto& t: pool ) t.join();

  usize total = 0;
  for ( const auto& result: output ) {
    total += result.lineCount;
    if ( numFiles > 1 ) std::cout << result.str << '\n';
  }
  std::cout << total << std::endl;

  return 0;
}