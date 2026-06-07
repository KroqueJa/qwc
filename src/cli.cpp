#include "cli.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

void printHelp()
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
      "      --char C          Count occurrences of the byte C instead of\n"
      "                        newlines. Handy for tallying commas in a CSV\n"
      "                        (--char ,) or any other single character.\n"
      "  -c, --chars           Count bytes instead of lines, like `wc -c`. The\n"
      "                        size is read straight from the filesystem, so no\n"
      "                        scanning of the file contents is needed.\n"
      "  -w, --words           Count whitespace-separated words instead of\n"
      "                        lines, like `wc -w`.\n"
      "  -m, --multibyte-chars Count characters instead of lines, like `wc -m`.\n"
      "                        In a UTF-8 locale this counts code points (so an\n"
      "                        accented letter is one character, not two bytes);\n"
      "                        in a single-byte locale it falls back to bytes,\n"
      "                        exactly as wc does.\n"
      "  -L, --max-line-length Print the length of the longest line in bytes\n"
      "                        instead of a count, like `wc -L`. The trailing\n"
      "                        newline is not counted, and with several files the\n"
      "                        \"total\" line reports the longest line across all\n"
      "                        of them (the maximum, not a sum).\n"
      "  -a, --all             Print lines, words and bytes together, like\n"
      "                        running `wc` with no flags.\n"
      "  -r, --recursive       Treat directory arguments as whole trees: wcl\n"
      "                        walks into them and counts every file it finds,\n"
      "                        something wc can't do on its own.\n"
      "      --sort-by-count   When listing several files, order them by their\n"
      "                        count, smallest first - so the biggest ones end\n"
      "                        up at the bottom, right next to the grand total\n"
      "                        where they're easy to spot.\n"
      "      --sort-by-name    Order the listing alphabetically by file name.\n"
      "      --sort-by-size    Order the listing by file size on disk, smallest\n"
      "                        first.\n"
      "      --reverse         Flip whichever ordering is in effect (e.g. put\n"
      "                        the biggest counts at the top instead).\n"
      "      --top N           Show only the N files that rank highest by the\n"
      "                        active sort (counts, if none is given). The grand\n"
      "                        total still covers every file, not just the N\n"
      "                        shown.\n"
      "      --bytes-per-thread N\n"
      "                        Tune how much data each worker thread handles\n"
      "                        before another thread is spun up (default 64\n"
      "                        MiB). You rarely need this; it's here for\n"
      "                        squeezing out performance on unusual hardware.\n"
      "  -h, --help            Show this message and exit.\n"
      "\n"
      "Output:\n"
      "  wcl matches wc's layout so it can stand in for it: each file prints a\n"
      "  right-aligned count followed by the file name, piped input prints just\n"
      "  the count, and counting several files adds a final \"total\" line.\n"
      "\n"
      "Examples:\n"
      "  wcl notes.txt                 lines in notes.txt\n"
      "  wcl *.log                     lines in each log, plus a total\n"
      "  wcl --char , data.csv         commas in data.csv\n"
      "  wcl -c notes.txt              bytes in notes.txt\n"
      "  wcl -w notes.txt              words in notes.txt\n"
      "  wcl -m notes.txt              characters in notes.txt\n"
      "  wcl -L notes.txt              length of the longest line in notes.txt\n"
      "  wcl -a notes.txt              lines, words and bytes in notes.txt\n"
      "  wcl --recursive src           lines in every file under src/\n"
      "  wcl -r --top 10 src           the 10 biggest files under src/\n";
}

std::optional<int> parseArgs( int argc, char** argv, Options& opt )
{
  auto isFlag = []( const char* arg, const char* shortName,
                    const char* longName ) {
    return std::strcmp( arg, shortName ) == 0 ||
           std::strcmp( arg, longName ) == 0;
  };

  // Parse leading "-flag [value]" options; the first non-flag argument begins
  // the file list. A bare "-" is treated as a file, not a flag.
  int fileStart = 1;
  while ( fileStart < argc && argv[fileStart][0] == '-' &&
          argv[fileStart][1] != '\0' ) {
    if ( std::strcmp( argv[fileStart], "--bytes-per-thread" ) == 0 ) {
      if ( fileStart + 1 >= argc ) {
        std::cerr << "Error: --bytes-per-thread requires a value\n";
        return 1;
      }
      opt.bytesPerThread = std::strtoull( argv[fileStart + 1], nullptr, 10 );
      if ( opt.bytesPerThread == 0 ) {
        std::cerr << "Error: --bytes-per-thread must be > 0\n";
        return 1;
      }
      fileStart += 2;
    } else if ( std::strcmp( argv[fileStart], "--char" ) == 0 ) {
      if ( fileStart + 1 >= argc || argv[fileStart + 1][0] == '\0' ) {
        std::cerr << "Error: --char requires a single-character value\n";
        return 1;
      }
      opt.target = argv[fileStart + 1][0];
      fileStart += 2;
    } else if ( isFlag( argv[fileStart], "-c", "--chars" ) ) {
      opt.mode = CountMode::Bytes;
      fileStart += 1;
    } else if ( isFlag( argv[fileStart], "-w", "--words" ) ) {
      opt.mode = CountMode::Words;
      fileStart += 1;
    } else if ( isFlag( argv[fileStart], "-m", "--multibyte-chars" ) ) {
      opt.mode = CountMode::Chars;
      fileStart += 1;
    } else if ( isFlag( argv[fileStart], "-L", "--max-line-length" ) ) {
      opt.mode = CountMode::MaxLineLength;
      fileStart += 1;
    } else if ( isFlag( argv[fileStart], "-a", "--all" ) ) {
      opt.all = true;
      fileStart += 1;
    } else if ( isFlag( argv[fileStart], "-r", "--recursive" ) ) {
      opt.recursive = true;
      fileStart += 1;
    } else if ( std::strcmp( argv[fileStart], "--sort-by-count" ) == 0 ) {
      opt.sortMode = SortMode::Count;
      fileStart += 1;
    } else if ( std::strcmp( argv[fileStart], "--sort-by-name" ) == 0 ) {
      opt.sortMode = SortMode::Name;
      fileStart += 1;
    } else if ( std::strcmp( argv[fileStart], "--sort-by-size" ) == 0 ) {
      opt.sortMode = SortMode::Size;
      fileStart += 1;
    } else if ( std::strcmp( argv[fileStart], "--reverse" ) == 0 ) {
      opt.reverse = true;
      fileStart += 1;
    } else if ( std::strcmp( argv[fileStart], "--top" ) == 0 ) {
      if ( fileStart + 1 >= argc ) {
        std::cerr << "Error: --top requires a value\n";
        return 1;
      }
      opt.topN = std::strtoull( argv[fileStart + 1], nullptr, 10 );
      if ( opt.topN == 0 ) {
        std::cerr << "Error: --top must be > 0\n";
        return 1;
      }
      fileStart += 2;
    } else if ( isFlag( argv[fileStart], "-h", "--help" ) ) {
      printHelp();
      return 0;
    } else {
      std::cerr << "Error: unknown flag " << argv[fileStart] << '\n';
      return 1;
    }
  }

  // --top needs a ranking criterion; default it to counts when none was asked
  // for, so `wcl --top 10 ...` means "the 10 files with the most lines".
  if ( opt.topN > 0 && opt.sortMode == SortMode::None )
    opt.sortMode = SortMode::Count;

  for ( int i = fileStart; i < argc; ++i ) opt.files.emplace_back( argv[i] );
  return std::nullopt;
}

bool collectFiles( Options& opt )
{
  // With --recursive, a directory argument is replaced by every regular file
  // beneath it; plain file arguments and the top-level argument order are left
  // untouched.
  std::vector<std::string> expanded;
  for ( const std::string& path: opt.files ) {
    if ( std::error_code ec; opt.recursive && fs::is_directory( path, ec ) ) {
      std::vector<std::string> dirFiles;
      fs::recursive_directory_iterator it(
          path, fs::directory_options::skip_permission_denied, ec
      );
      if ( ec ) {
        std::cerr << "Error reading directory: " << path << '\n';
        return false;
      }
      const fs::recursive_directory_iterator end;
      for ( ; it != end; it.increment( ec ) ) {
        if ( ec ) continue;  // skip entries we can't stat and keep going
        if ( it->is_regular_file( ec ) && !ec )
          dirFiles.push_back( it->path().string() );
      }
      // Alphabetize for stable output -- but only when no sort key is active,
      // since otherwise the whole list is re-ordered below and this is wasted
      // work. (With no key, --reverse still has this stable order to flip.)
      if ( opt.sortMode == SortMode::None )
        std::sort( dirFiles.begin(), dirFiles.end() );
      expanded.insert( expanded.end(), dirFiles.begin(), dirFiles.end() );
    } else {
      expanded.push_back( path );
    }
  }
  opt.files = std::move( expanded );
  return true;
}

void printCountLine( const usize count, const char* name )
{
  // " %7ju %s\n" -- a leading space, the count right-justified to a minimum
  // field of 7 (it grows for larger counts, exactly as wc does), then the name.
  std::cout << ' ' << std::setw( 7 ) << count;
  if ( name ) std::cout << ' ' << name;
  std::cout << '\n';
}

void printAllLine( const Counts& c, const char* name )
{
  // Bare wc's layout: " %7ju %7ju %7ju %s\n" -- lines, words, bytes, name.
  std::cout << ' ' << std::setw( 7 ) << c.lines << ' ' << std::setw( 7 )
            << c.words << ' ' << std::setw( 7 ) << c.bytes;
  if ( name ) std::cout << ' ' << name;
  std::cout << '\n';
}

void printResults( const Options& opt, const std::vector<Result>& output )
{
  const usize numFiles = output.size();

  // The grand total always covers every file, independent of sorting or --top.
  // For --max-line-length the "total" is the longest line across all files (a
  // maximum), matching `wc -L`; every other mode sums the per-file counts.
  usize total = 0;
  if ( opt.mode == CountMode::MaxLineLength )
    for ( const Result& result: output ) total = std::max( total, result.count );
  else
    for ( const Result& result: output ) total += result.count;

  // Decide the display order via an index permutation, leaving `output` (and the
  // total) untouched. Default order is the order files were collected in.
  std::vector<usize> order( numFiles );
  std::iota( order.begin(), order.end(), 0 );

  if ( opt.sortMode != SortMode::None ) {
    // File sizes are only needed for --sort-by-size, so fetch them lazily.
    std::vector<std::uintmax_t> sizes;
    if ( opt.sortMode == SortMode::Size ) {
      sizes.resize( numFiles );
      for ( usize i = 0; i < numFiles; ++i ) {
        std::error_code ec;
        const std::uintmax_t s = fs::file_size( opt.files[i], ec );
        sizes[i] = ec ? 0 : s;
      }
    }

    // Sort ascending by the chosen key, tie-broken by filename so output is
    // deterministic regardless of which thread finished first.
    std::sort(
        order.begin(), order.end(),
        [&]( const usize a, const usize b ) {
          if ( opt.sortMode == SortMode::Count &&
               output[a].count != output[b].count )
            return output[a].count < output[b].count;
          if ( opt.sortMode == SortMode::Size && sizes[a] != sizes[b] )
            return sizes[a] < sizes[b];
          return opt.files[a] < opt.files[b];
        }
    );
  }

  // --top N keeps the N highest-ranked files: the tail of the ascending order.
  if ( opt.topN > 0 && opt.topN < order.size() )
    order.erase( order.begin(), order.end() - static_cast<isize>( opt.topN ) );

  // --reverse flips the final display order (e.g. biggest counts first).
  if ( opt.reverse ) std::reverse( order.begin(), order.end() );

  // wc prints one line per file -- including for a single file (with its name).
  for ( const usize i: order )
    printCountLine( output[i].count, opt.files[i].c_str() );

  // The "<total> total" line appears only when more than one file was counted,
  // matching wc. (--top may narrow the listing above, but the total still
  // covers every file.) A recursive walk that matched no files has nothing to
  // name, so it just prints a bare zero rather than nothing at all.
  if ( numFiles > 1 )
    printCountLine( total, "total" );
  else if ( numFiles == 0 )
    printCountLine( total, nullptr );
}

void printResultsAll( const Options& opt, const std::vector<Counts>& output )
{
  const usize numFiles = output.size();

  Counts total{};
  for ( const Counts& c: output ) {
    total.lines += c.lines;
    total.words += c.words;
    total.bytes += c.bytes;
  }

  // wc has no sorting or --top, so --all keeps the collected file order.
  for ( usize i = 0; i < numFiles; ++i )
    printAllLine( output[i], opt.files[i].c_str() );

  if ( numFiles > 1 )
    printAllLine( total, "total" );
  else if ( numFiles == 0 )
    printAllLine( total, nullptr );
}
