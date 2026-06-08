#pragma once

#include <optional>
#include <string>
#include <vector>

#include "processfile.h"
#include "typedef.h"

// How to order the per-file output list. The chosen key governs both the
// display order and which files --top keeps; ordering is ascending by default
// (so the largest land at the bottom), and --reverse flips it.
enum class SortMode { None, Count, Name, Size };

// Everything the frontend parses out of the command line. The count flags map to
// wc's output columns, which always print in a fixed order regardless of the
// order the flags were given: lines, words, char/byte, longest line.
struct Options
{
  usize bytesPerThread = 64 * 1024 * 1024;

  // Selected columns. With no count flag at all, qwc shows lines, words and
  // bytes -- exactly like bare `wc`.
  bool lines = false;          // -l
  bool words = false;          // -w
  bool charByte = false;       // -c or -m: the single shared char/byte column
  bool charsNotBytes = false;  // within charByte: -m counts chars, -c counts bytes
  bool maxLine = false;        // -L
  bool target = false;         // --char: qwc extension (counts targetByte)
  char targetByte = '\n';

  bool recursive = false;        // expand directory arguments
  SortMode sortMode = SortMode::None;
  bool reverse = false;          // flip the display order
  usize topN = 0;                // keep only the highest N (0 == show all)
  std::vector<std::string> files;  // positional args; expanded by collectFiles

  // How many output columns are selected.
  int columnCount() const
  {
    return static_cast<int>( lines ) + static_cast<int>( words ) +
           static_cast<int>( charByte ) + static_cast<int>( maxLine ) +
           static_cast<int>( target );
  }

  // The counting workload these options imply (resolves the char/byte column to
  // either a byte or a character count).
  Workload workload() const;
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

// Print one output row: each selected column right-justified in a min-width-7
// field (matching wc's " %7ju"), then -- when `name` is non-null -- a space and
// the name. Used for stdin (no name), each file, and the "total" line.
void printCounts( const Options& opt, const Counts& c, const char* name );

// Render the counted files in wc's layout: one row per file (selected columns +
// name), plus a trailing "total" row when more than one file was counted. With
// exactly one column selected the listing honours --sort-*/--top/--reverse; with
// several columns it keeps the collected order, like bare `wc`.
void printResults( const Options& opt, const std::vector<Counts>& output );
