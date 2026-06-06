#pragma once
#include "typedef.h"

// One file's tally (lines, words, or bytes, depending on the count mode). The
// file's name is not stored here -- it lives in Options::files at the same
// index, so printResults pairs them up when formatting.
struct Result
{
  usize count;
};
