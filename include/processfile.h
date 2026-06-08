#pragma once
#include "typedef.h"

// What processFile tallies for a file. Target counts occurrences of a chosen
// byte (newlines by default, like `wc -l`); Bytes reports the file's size in
// bytes, like `wc -c` -- the size is already known from fstat, so no scan is
// needed; Words counts whitespace-separated words, like `wc -w`; Chars counts
// UTF-8 characters (code points), like `wc -m` in a multibyte locale;
// MaxLineLength reports the length in bytes of the longest line, like `wc -L`.
enum class CountMode { Target, Bytes, Words, Chars, MaxLineLength };

usize processFile(
    const char* filename, usize bytesPerThread = 64 * 1024 * 1024,
    char target = '\n', CountMode mode = CountMode::Target
);

// Lines, words and bytes for one input -- the trio `wc` prints by default.
struct Counts
{
  usize lines;
  usize words;
  usize bytes;
};

// Count lines, words and bytes in a single pass (the bare-`wc` / no-flag mode).
// Doing all three at once is what lets it read stdin, which can only be consumed
// once. An empty filename reads standard input, matching processFile.
Counts processFileAll(
    const char* filename, usize bytesPerThread = 64 * 1024 * 1024
);