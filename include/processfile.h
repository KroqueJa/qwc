#pragma once
#include "typedef.h"

// What processFile tallies for a file. Target counts occurrences of a chosen
// byte (newlines by default, like `wc -l`); Bytes reports the file's size in
// bytes, like `wc -c` -- the size is already known from fstat, so no scan is
// needed; Words counts whitespace-separated words, like `wc -w`.
enum class CountMode { Target, Bytes, Words };

usize processFile(
    const char* filename, usize bytesPerThread = 64 * 1024 * 1024,
    char target = '\n', CountMode mode = CountMode::Target
);