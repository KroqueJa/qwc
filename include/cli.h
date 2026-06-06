#pragma once

#include <optional>
#include <string>
#include <vector>

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

// Render counted results: per-file "<count> <name>" lines (only when more than
// one file was counted), ordered per `opt`, followed by the grand total.
void printResults( const Options& opt, const std::vector<Result>& output );
