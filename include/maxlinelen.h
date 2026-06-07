#pragma once
#include "typedef.h"

// Running state for a longest-line scan, carried across the successive buffers of
// one byte range -- a single thread's chunk, or the whole stdin stream. `cur` is
// the length of the line currently open (bytes since the last newline);
// `maxComplete` is the longest line already terminated by a newline; `prefixLen`
// is the length of the very first line (the bytes before the first newline),
// recorded so a line straddling a chunk boundary can be stitched back together;
// `hasNewline` records whether any newline was seen at all.
//
// After scanning a whole range, `cur` is the length of the trailing, still-open
// line -- the suffix handed to the next chunk so a line straddling the boundary
// can be completed there. Note `wc -L` only measures lines that end in a
// newline: a trailing run with no final newline is NOT a counted line, so the
// caller folds `cur` forward but never reports it on its own.
struct LineScan
{
  usize cur = 0;
  usize maxComplete = 0;
  usize prefixLen = 0;
  bool hasNewline = false;
};

// Scan `length` bytes, updating `s`. A newline closes the current line (a
// candidate for the longest) and opens a fresh one; the newline byte itself is
// not counted, exactly like `wc -L`. Tabs and control bytes count as one each --
// `wc -L` on this platform measures bytes, with no tab-stop expansion. Each byte
// is classified on its own, so feeding a stream in pieces yields the same state
// as one shot, which is what lets a chunk be read buffer by buffer.
void maxLineLen( const char* buffer, usize length, LineScan& s );
