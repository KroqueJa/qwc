/*
 * Copyright Ji Krochmal 2026
 */
#pragma once
#include "typedef.h"

// Running state for a longest-line scan, carried across the successive buffers
// of one byte range -- a single thread's chunk, or the whole stdin stream.
// `cur` is the length of the line currently open (bytes since the last
// newline); `maxComplete` is the longest line already terminated by a newline;
// `prefixLen` is the length of the very first line (the bytes before the first
// newline), recorded so a line straddling a chunk boundary can be stitched back
// together; `hasNewline` records whether any newline was seen at all.
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
// not counted, exactly like `wc -L`. There is no tab-stop expansion.
//
// `countChars` chooses the unit, mirroring wc: by default a line's length is
// its byte count, but when wc's char/byte column is counting characters (-m
// active in a multibyte locale) `wc -L` measures the longest line in characters
// too. In that mode UTF-8 continuation bytes (0x80-0xBF) don't advance the
// length, so each code point counts once. Either way every byte is classified
// on its own, so feeding a stream in pieces matches one shot -- what lets a
// chunk be read buffer by buffer and split anywhere, mid-character included.
void maxLineLen(
    const char* buffer, usize length, LineScan& s, bool countChars = false
);

// Fused scan for `qwc -L -m`: in one pass, maintain the longest line measured
// in characters (in `s`, exactly as maxLineLen with countChars=true) AND add
// the buffer's character count to `charCount`. The character total and the
// per-line character length both walk the same UTF-8 continuation bytes, so the
// two are computed together instead of in the two separate passes (chars() then
// maxLineLen()) the generic engine would otherwise run for that flag pair. `s`
// and `charCount` carry across the successive buffers of one byte range, like
// maxLineLen.
void maxLineLenChars(
    const char* buffer, usize length, LineScan& s, usize& charCount
);
