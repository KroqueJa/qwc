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
  std::cout
      << "qwc - quick wc: count lines, words, bytes and more, fast.\n"
         "\n"
         "With no count flag qwc prints the line, word and byte counts of each "
         "file,\n"
         "exactly like `wc`. It reads big files in parallel, so it stays quick "
         "even\n"
         "on very large inputs.\n"
         "\n"
         "Usage:\n"
         "  qwc [options] [file ...]\n"
         "  qwc [options]            (reads from standard input)\n"
         "\n"
         "If you don't name any files, qwc reads from standard input - so you "
         "can\n"
         "pipe data straight into it, e.g.  cat access.log | qwc\n"
         "\n"
         "Count flags (combine freely, e.g. -lw or -l -w; their order never "
         "changes\n"
         "the column order. With none, qwc prints lines, words and bytes like "
         "bare\n"
         "`wc`. -c and -m share one column; whichever comes last wins):\n"
         "  -l, --lines           Count lines (newline characters), like `wc "
         "-l`.\n"
         "  -w, --words           Count whitespace-separated words, like `wc "
         "-w`.\n"
         "  -c, --bytes           Count bytes, like `wc -c`. The size is read\n"
         "                        straight from the filesystem, so no scanning "
         "of\n"
         "                        the file contents is needed.\n"
         "  -m, --chars           Count characters, like `wc -m`. In a UTF-8 "
         "locale\n"
         "                        this counts code points (so an accented "
         "letter is\n"
         "                        one character, not two bytes); in a "
         "single-byte\n"
         "                        locale it falls back to bytes, exactly as wc "
         "does.\n"
         "  -L, --max-line-length Print the length of the longest line in "
         "bytes,\n"
         "                        like `wc -L`. The trailing newline is not "
         "counted,\n"
         "                        and with several files the \"total\" line "
         "reports\n"
         "                        the longest line across all of them (the "
         "maximum,\n"
         "                        not a sum).\n"
         "      --char C          Count occurrences of the byte C (a qwc "
         "extension,\n"
         "                        not in wc). Handy for tallying commas in a "
         "CSV\n"
         "                        (--char ,) or any other single character.\n"
         "\n"
         "Other options:\n"
         "  -r, --recursive       Treat directory arguments as whole trees: "
         "qwc\n"
         "                        walks into them and counts every file it "
         "finds,\n"
         "                        something wc can't do on its own.\n"
         "      --sort-by-count   When listing several files, order them by "
         "their\n"
         "                        count, smallest first - so the biggest ones "
         "end\n"
         "                        up at the bottom, right next to the grand "
         "total\n"
         "                        where they're easy to spot.\n"
         "      --sort-by-name    Order the listing alphabetically by file "
         "name.\n"
         "      --sort-by-size    Order the listing by file size on disk, "
         "smallest\n"
         "                        first.\n"
         "      --reverse         Flip whichever ordering is in effect (e.g. "
         "put\n"
         "                        the biggest counts at the top instead).\n"
         "      --top N           Show only the N files that rank highest by "
         "the\n"
         "                        active sort (counts, if none is given). The "
         "grand\n"
         "                        total still covers every file, not just the "
         "N\n"
         "                        shown.\n"
         "      --bytes-per-thread N\n"
         "                        Tune how much data each worker thread "
         "handles\n"
         "                        before another thread is spun up (default "
         "64\n"
         "                        MiB). You rarely need this; it's here for\n"
         "                        squeezing out performance on unusual "
         "hardware.\n"
         "  -h, --help            Show this message and exit.\n"
         "\n"
         "Output:\n"
         "  qwc matches wc's layout so it can stand in for it: each file "
         "prints its\n"
         "  right-aligned count(s) followed by the file name, piped input "
         "prints just\n"
         "  the count(s), and counting several files adds a final \"total\" "
         "line.\n"
         "\n"
         "Examples:\n"
         "  qwc notes.txt                 lines, words and bytes in notes.txt\n"
         "  qwc *.log                     counts for each log, plus a total\n"
         "  qwc -l notes.txt              lines in notes.txt\n"
         "  qwc -c notes.txt              bytes in notes.txt\n"
         "  qwc -w notes.txt              words in notes.txt\n"
         "  qwc -m notes.txt              characters in notes.txt\n"
         "  qwc -L notes.txt              length of the longest line in "
         "notes.txt\n"
         "  qwc --char , data.csv         commas in data.csv\n"
         "  qwc --recursive src           counts for every file under src/\n"
         "  qwc -r --top 10 src           the 10 biggest files under src/\n";
}

std::optional<int> parseArgs( int argc, char** argv, Options& opt )
{
  // Track whether any count flag was given. With none, qwc prints lines, words
  // and bytes -- the three columns bare `wc` shows. Count flags accumulate (and
  // their order never affects the column order), so `-l -w` and `-lw` and `-wl`
  // are all "lines and words".
  bool countFlag = false;

  // Apply one short count flag letter. Returns false if the letter is unknown.
  auto applyShort = [&]( char c ) -> bool {
    switch ( c ) {
      case 'l':
        opt.lines = true;
        countFlag = true;
        return true;
      case 'w':
        opt.words = true;
        countFlag = true;
        return true;
      case 'c':
        opt.charByte = true;
        opt.charsNotBytes = false;
        countFlag = true;
        return true;
      case 'm':
        opt.charByte = true;
        opt.charsNotBytes = true;
        countFlag = true;
        return true;
      case 'L':
        opt.maxLine = true;
        countFlag = true;
        return true;
      case 'r':
        opt.recursive = true;
        return true;
      default:
        return false;
    }
  };

  // Parse leading options; the first non-flag argument begins the file list. A
  // bare "-" is treated as a file, not a flag.
  int fileStart = 1;
  while ( fileStart < argc && argv[fileStart][0] == '-' &&
          argv[fileStart][1] != '\0' ) {
    const char* arg = argv[fileStart];
    if ( arg[1] == '-' ) {
      // Long options.
      if ( std::strcmp( arg, "--bytes-per-thread" ) == 0 ) {
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
      } else if ( std::strcmp( arg, "--char" ) == 0 ) {
        if ( fileStart + 1 >= argc || argv[fileStart + 1][0] == '\0' ) {
          std::cerr << "Error: --char requires a single-character value\n";
          return 1;
        }
        opt.target = true;
        opt.targetByte = argv[fileStart + 1][0];
        countFlag = true;
        fileStart += 2;
      } else if ( std::strcmp( arg, "--lines" ) == 0 ) {
        opt.lines = true;
        countFlag = true;
        fileStart += 1;
      } else if ( std::strcmp( arg, "--words" ) == 0 ) {
        opt.words = true;
        countFlag = true;
        fileStart += 1;
      } else if ( std::strcmp( arg, "--bytes" ) == 0 ) {
        opt.charByte = true;
        opt.charsNotBytes = false;
        countFlag = true;
        fileStart += 1;
      } else if ( std::strcmp( arg, "--chars" ) == 0 ) {
        opt.charByte = true;
        opt.charsNotBytes = true;
        countFlag = true;
        fileStart += 1;
      } else if ( std::strcmp( arg, "--max-line-length" ) == 0 ) {
        opt.maxLine = true;
        countFlag = true;
        fileStart += 1;
      } else if ( std::strcmp( arg, "--recursive" ) == 0 ) {
        opt.recursive = true;
        fileStart += 1;
      } else if ( std::strcmp( arg, "--sort-by-count" ) == 0 ) {
        opt.sortMode = SortMode::Count;
        fileStart += 1;
      } else if ( std::strcmp( arg, "--sort-by-name" ) == 0 ) {
        opt.sortMode = SortMode::Name;
        fileStart += 1;
      } else if ( std::strcmp( arg, "--sort-by-size" ) == 0 ) {
        opt.sortMode = SortMode::Size;
        fileStart += 1;
      } else if ( std::strcmp( arg, "--reverse" ) == 0 ) {
        opt.reverse = true;
        fileStart += 1;
      } else if ( std::strcmp( arg, "--top" ) == 0 ) {
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
      } else if ( std::strcmp( arg, "--help" ) == 0 ) {
        printHelp();
        return 0;
      } else {
        std::cerr << "Error: unknown flag " << arg << '\n';
        return 1;
      }
    } else if ( std::strcmp( arg, "-h" ) == 0 ) {
      printHelp();
      return 0;
    } else {
      // A bundle of short flags, e.g. "-l", "-lw", "-lwcmL". Each letter is its
      // own count/option flag; wc allows them stacked, so we do too.
      for ( const char* p = arg + 1; *p != '\0'; ++p ) {
        if ( !applyShort( *p ) ) {
          std::cerr << "Error: unknown flag -" << *p << '\n';
          return 1;
        }
      }
      fileStart += 1;
    }
  }

  // --top needs a ranking criterion; default it to counts when none was asked
  // for, so `qwc -l --top 10 ...` means "the 10 files with the most lines".
  if ( opt.topN > 0 && opt.sortMode == SortMode::None )
    opt.sortMode = SortMode::Count;

  // No count flag means the bare-`wc` view: lines, words and bytes together.
  if ( !countFlag ) {
    opt.lines = true;
    opt.words = true;
    opt.charByte = true;
    opt.charsNotBytes = false;  // the char/byte column defaults to bytes
  }

  for ( int i = fileStart; i < argc; ++i ) opt.files.emplace_back( argv[i] );
  return std::nullopt;
}

Workload Options::workload() const
{
  Workload w;
  w.lines = lines;
  w.words = words;
  w.maxLine = maxLine;
  w.target = target;
  w.targetByte = targetByte;
  if ( charByte ) {
    w.chars = charsNotBytes;
    w.bytes = !charsNotBytes;
  }
  // wc measures the longest line in the same unit as the active char/byte
  // column: characters when -m is in effect, bytes otherwise.
  w.maxLineInChars = charByte && charsNotBytes;
  return w;
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

namespace {

// qwc's output columns, in wc's fixed order. The --char tally (qwc-only) has no
// wc counterpart, so it is appended last.
enum class Column
{
  Lines,
  Words,
  CharByte,
  MaxLine,
  Target
};

std::vector<Column> selectedColumns( const Options& opt )
{
  std::vector<Column> cols;
  if ( opt.lines ) cols.push_back( Column::Lines );
  if ( opt.words ) cols.push_back( Column::Words );
  if ( opt.charByte ) cols.push_back( Column::CharByte );
  if ( opt.maxLine ) cols.push_back( Column::MaxLine );
  if ( opt.target ) cols.push_back( Column::Target );
  return cols;
}

usize columnValue( const Counts& c, const Column col, const Options& opt )
{
  switch ( col ) {
    case Column::Lines:
      return c.lines;
    case Column::Words:
      return c.words;
    case Column::CharByte:
      return opt.charsNotBytes ? c.chars : c.bytes;
    case Column::MaxLine:
      return c.maxLine;
    case Column::Target:
      return c.target;
  }
  return 0;
}

}  // namespace

void printCounts( const Options& opt, const Counts& c, const char* name )
{
  // Each selected column right-justified in a min-width-7 field (" %7ju", which
  // grows for larger counts exactly as wc does), then the optional name.
  for ( const Column col: selectedColumns( opt ) )
    std::cout << ' ' << std::setw( 7 ) << columnValue( c, col, opt );
  if ( name ) std::cout << ' ' << name;
  std::cout << '\n';
}

void printResults( const Options& opt, const std::vector<Counts>& output )
{
  const usize numFiles = output.size();

  // The grand total covers every file, independent of sorting or --top. Each
  // column sums, except the longest line, which is a maximum (matching wc -L).
  Counts total{};
  for ( const Counts& c: output ) {
    total.lines += c.lines;
    total.words += c.words;
    total.bytes += c.bytes;
    total.chars += c.chars;
    total.target += c.target;
    total.maxLine = std::max( total.maxLine, c.maxLine );
  }

  // Display order via an index permutation, leaving `output` untouched.
  // Sorting,
  // --top and --reverse only apply to a single-column listing -- there is no
  // one value to rank by otherwise -- so bare/multi-column qwc keeps the
  // collected order, exactly like wc.
  std::vector<usize> order( numFiles );
  std::iota( order.begin(), order.end(), 0 );

  const bool single = opt.columnCount() == 1;
  if ( single && opt.sortMode != SortMode::None ) {
    const Column col = selectedColumns( opt ).front();

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
    std::sort( order.begin(), order.end(), [&]( const usize a, const usize b ) {
      if ( opt.sortMode == SortMode::Count ) {
        const usize va = columnValue( output[a], col, opt );
        const usize vb = columnValue( output[b], col, opt );
        if ( va != vb ) return va < vb;
      } else if ( opt.sortMode == SortMode::Size && sizes[a] != sizes[b] ) {
        return sizes[a] < sizes[b];
      }
      return opt.files[a] < opt.files[b];
    } );
  }

  // --top N keeps the N highest-ranked files: the tail of the ascending order.
  if ( single && opt.topN > 0 && opt.topN < order.size() )
    order.erase( order.begin(), order.end() - static_cast<isize>( opt.topN ) );

  // --reverse flips the final display order (e.g. biggest counts first).
  if ( single && opt.reverse ) std::reverse( order.begin(), order.end() );

  // wc prints one row per file -- including for a single file (with its name).
  for ( const usize i: order )
    printCounts( opt, output[i], opt.files[i].c_str() );

  // The "total" row appears only when more than one file was counted, matching
  // wc. (--top may narrow the listing above, but the total still covers every
  // file.) A recursive walk that matched no files has nothing to name, so it
  // just prints a bare zero row rather than nothing at all.
  if ( numFiles > 1 )
    printCounts( opt, total, "total" );
  else if ( numFiles == 0 )
    printCounts( opt, total, nullptr );
}
