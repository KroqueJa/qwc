/*
 * Copyright Ji Krochmal 2026
 */
#pragma once
#include "typedef.h"

// Locale-resolved word-splitting parameters, decided once at startup (see
// main.cpp): utf8 = LC_CTYPE codeset is UTF-8; nbspace = coreutils'
// non-breaking-space separators are active (off under POSIXLY_CORRECT).
struct WordsMode
{
  bool utf8 = false;
  bool nbspace = true;
};

// Carried word-scan state across the successive buffers of one byte range,
// plus the facts a parallel chunk reports for the seam merge. A word is a
// maximal non-separator run containing >= 1 printable character; it is counted
// when it ENDS (a separator after it), so `words` excludes the trailing open
// run -- that is handed forward via inWord/runHasPrintable and folded in by
// wordsFlush() (stream end) or the chunk merge in processfile.cpp.
struct WordScan
{
  usize words = 0;               // non-barren runs that ended in-range
  bool inWord = false;           // a run is open at the current position
  bool runHasPrintable = false;  // that open run has seen a printable
  // Seam-merge facts, relative to the start of the range:
  bool sawByte = false;              // any owned byte processed yet
  bool startsInWord = false;         // first owned cp was a word constituent
  bool sawSeparator = false;         // any separator seen in-range
  bool leadingEnded = false;         // the run open at range start terminated
  bool leadingHasPrintable = false;  // printable seen in that leading run
};

// Scan the owned region [ownedBegin, ownedEnd) of buf. Bytes outside the owned
// region are context only: multibyte windows may read up to 3 bytes either
// side, and a code point belongs to the scan owning its lead byte. Continues
// from (and updates) `s`.
void words(
    const char* buf, usize len, usize ownedBegin, usize ownedEnd, WordScan& s,
    const WordsMode& m
);

// Fold the trailing open run of a finished range into its count.
inline void wordsFlush( WordScan& s )
{
  if ( s.inWord && s.runHasPrintable ) ++s.words;
  s.inWord = false;
  s.runHasPrintable = false;
}

// ASCII whitespace, the C-locale separator set and the ASCII slice of the
// UTF-8 one. Kept for the kernels and tests.
inline bool isWordSpace( const char c )
{
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
         c == '\r';
}

// The UTF-8-locale separator set, pinned empirically against GNU wc 9.4 /
// glibc C.UTF-8 (scripts/probe-wc-words.py). Base set = the locale's iswspace
// class: ASCII whitespace, U+1680, U+2000-200A except U+2007, U+205F, U+3000.
// The nbspace four {U+00A0, U+2007, U+202F, U+2060} are coreutils' extension,
// dropped under POSIXLY_CORRECT (probe-confirmed: exactly these four flip).
inline bool isSepCp( const u32 cp, const bool nbspace )
{
  if ( cp == ' ' || ( cp >= 0x09 && cp <= 0x0D ) ) return true;
  if ( cp == 0x1680 || ( cp >= 0x2000 && cp <= 0x200A && cp != 0x2007 ) ||
       cp == 0x205F || cp == 0x3000 )
    return true;
  if ( nbspace &&
       ( cp == 0x00A0 || cp == 0x2007 || cp == 0x202F || cp == 0x2060 ) )
    return true;
  return false;
}
