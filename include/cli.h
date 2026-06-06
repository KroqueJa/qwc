#pragma once

#include <optional>
#include <string>
#include <vector>

#include "processfile.h"
#include "result.h"
#include "typedef.h"

// How to order the per-file output list. The chosen key governs both the
// display order and which files --top keeps; ordering is ascending by default
// (so the largest land at the bottom), and --reverse flips it.
enum class SortMode { None, Count, Name, Size };

// Everything the frontend parses out of the command line.
struct Options
{
  usize bytesPerThread = 64 * 1024 * 1024;
  char target = '\n';            // byte to count; '\n' counts lines
  CountMode mode = CountMode::Target;  // --chars switches to byte counting
  bool recursive = false;        // expand directory arguments
  SortMode sortMode = SortMode::None;
  bool reverse = false;          // flip the display order
  usize topN = 0;                // keep only the highest N (0 == show all)
  std::vector<std::string> files;  // positional args; expanded by collectFiles
};

// Print the help / usage text to stdout.
void printHelp();

// Parse argv into `opt`. Returns std::nullopt to keep running; otherwise the
// process exit code (0 after --help, 1 on a usage error). Help and error
// messages are written here.
std::optional<int> parseArgs( int argc, char** argv, Options& opt );

// Expand any directory arguments in opt.files into the regular files beneath
// them (only under --recursive); plain file arguments are left as-is. Returns
// false after reporting an unreadable directory.
bool collectFiles( Options& opt );

// Print one BSD-wc-style count line: a leading space, `count` right-justified
// to a minimum width of 7, and -- when `name` is non-null -- a space and the
// name. Matches `wc`'s `" %7ju %s\n"` (and `" %7ju\n"` for stdin) so wcl drops
// in for wc.
void printCountLine( usize count, const char* name );

// Render counted results in wc's layout: one "<count> <name>" line per file
// (ordered per `opt`), plus a trailing "<total> total" line when more than one
// file was counted. A single file prints just its own line, like wc.
void printResults( const Options& opt, const std::vector<Result>& output );
