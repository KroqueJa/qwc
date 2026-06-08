#pragma once
#include "typedef.h"

// Which counts to produce for an input. The frontend sets these from the
// command line once, up front, and a single pass computes every requested
// counter at once -- so `qwc -l -w -m -L` reads the data once, not four times.
//
// `bytes` and `chars` are never both set: wc shows a single char/byte column
// and the frontend picks which (-c => bytes, -m => chars). `target` is the
// qwc-only
// `--char` extension (count occurrences of `targetByte`).
struct Workload
{
  bool lines = false;
  bool words = false;
  bool bytes = false;
  bool chars = false;
  bool maxLine = false;
  bool maxLineInChars =
      false;  // -L measures characters (when -m is active), not bytes
  bool target = false;
  char targetByte = '\n';

  // Does anything here require scanning the file contents? Bytes alone come
  // from fstat, so a pure `-c` needs no scan at all.
  bool needsScan() const
  { return lines || words || chars || maxLine || target; }
};

// Every count qwc can produce for one input. Only the fields the Workload asked
// for are populated; the rest stay zero.
struct Counts
{
  usize lines = 0;
  usize words = 0;
  usize bytes = 0;
  usize chars = 0;
  usize maxLine = 0;
  usize target = 0;
};

// Compute the requested counts for `filename` (an empty name reads standard
// input) in one pass. Bytes come straight from fstat; the scanned counters are
// computed in parallel across chunks of the file, with words and the longest
// line stitched back together across chunk boundaries. Every requested counter
// shares the same read of the data.
Counts processFile(
    const char* filename, const Workload& work,
    usize bytesPerThread = 64ull * 1024 * 1024
);
