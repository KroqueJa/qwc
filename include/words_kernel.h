/*
 * Copyright Ji Krochmal 2026
 */
#pragma once
#include "iswprint_table.h"
#include "typedef.h"
#include "words.h"

// Arch-independent core of the SIMD word counters. The strict UTF-8 decoder,
// the classifiers, the per-code-point `step`, the scalar-fallback walker
// `scalarUtf8`, and the mask state machine `stepMasks` are pure C++ over u32
// masks and bytes -- no intrinsics -- so the AVX2 and NEON kernels share them.
//
// All three kernels consume this header: words_avx2.cpp and words_neon.cpp
// supply their arch's mask primitives and the vector words() driver;
// words_scalar.cpp -- the semantic reference -- is just the scalar driver
// over the same machinery. This is the single home of the word semantics.

namespace qwc::words_kernel {

enum class Cls : u8
{
  Sep,
  Print,
  Other
};

// Strict UTF-8 decode at p (window-bounded by end). On success returns the
// scalar value and sets seqLen; on any invalid/truncated sequence consumes
// exactly 1 byte (seqLen=1) and returns 0xFFFFFFFF. Mirrors glibc mbrtowc
// strictness: no overlongs, no surrogates, max U+10FFFF.
inline u32 decodeCp( const u8* p, const u8* end, usize& seqLen )
{
  const u32 b0 = p[0];
  seqLen = 1;
  if ( b0 < 0x80 ) return b0;
  if ( b0 < 0xC2 || b0 > 0xF4 ) return 0xFFFFFFFF;
  const usize need = b0 < 0xE0 ? 2 : b0 < 0xF0 ? 3 : 4;
  if ( p + need > end ) return 0xFFFFFFFF;
  u32 cp = b0 & ( 0xFFu >> ( need + 1 ) );
  for ( usize k = 1; k < need; ++k ) {
    if ( ( p[k] & 0xC0 ) != 0x80 ) return 0xFFFFFFFF;
    cp = ( cp << 6 ) | ( p[k] & 0x3F );
  }
  static constexpr u32 kMin[5] = { 0, 0, 0x80, 0x800, 0x10000 };
  if ( cp < kMin[need] || cp > 0x10FFFF || ( cp >= 0xD800 && cp <= 0xDFFF ) )
    return 0xFFFFFFFF;
  seqLen = need;
  return cp;
}

inline Cls classifyC( const u8 b )
{
  if ( isWordSpace( static_cast<char>( b ) ) ) return Cls::Sep;
  if ( b >= 0x21 && b <= 0x7E ) return Cls::Print;
  return Cls::Other;  // controls, DEL, and every byte >= 0x80
}

inline Cls classifyUtf8( const u32 cp, const bool nbspace )
{
  if ( cp == 0xFFFFFFFF ) return Cls::Other;  // invalid byte
  if ( isSepCp( cp, nbspace ) ) return Cls::Sep;
  return qwcIswprint( cp ) ? Cls::Print : Cls::Other;
}

// One classified code point through the run state machine.
inline void step( const Cls c, WordScan& s )
{
  if ( !s.sawByte ) {
    s.sawByte = true;
    s.startsInWord = ( c != Cls::Sep );
  }
  if ( c == Cls::Sep ) {
    if ( s.inWord ) {
      if ( s.runHasPrintable ) ++s.words;
      // The first separator closes the leading run (if one was open).
      if ( !s.sawSeparator && s.startsInWord ) {
        s.leadingEnded = true;
        s.leadingHasPrintable = s.runHasPrintable;
      }
      s.inWord = false;
      s.runHasPrintable = false;
    }
    s.sawSeparator = true;
  } else {
    s.inWord = true;
    if ( c == Cls::Print ) s.runHasPrintable = true;
  }
}

// Scalar walk of the owned subrange [from, stop) with full-buffer window
// [base, base+len): the entry path for a leading continuation byte, punted
// vector blocks, and the epilogue. Consumes whole code points (the last one
// may overhang `stop`); returns the first unconsumed position.
inline usize scalarUtf8(
    const u8* base, const usize len, const usize from, const usize stop,
    WordScan& s, const bool nbspace
)
{
  const u8* end = base + len;
  usize i = from;

  // A continuation byte at the start belongs to a code point whose lead was
  // already classified (by the previous scan, or by the preceding vector
  // block) -- skip past it. If no valid sequence covers it, the bytes are
  // stray continuations owned here, classified one-by-one as invalid below.
  if ( i < stop && ( base[i] & 0xC0 ) == 0x80 ) {
    const usize back = i < 3 ? i : 3;
    for ( usize k = 1; k <= back; ++k ) {
      const u8 lead = base[i - k];
      if ( ( lead & 0xC0 ) == 0x80 ) continue;  // still a continuation byte
      usize seqLen = 1;
      if ( decodeCp( base + i - k, end, seqLen ) != 0xFFFFFFFF &&
           i - k + seqLen > i )
        i = i - k + seqLen;  // a valid sequence covers us: already classified
      break;
    }
  }

  while ( i < stop ) {
    usize seqLen = 1;
    const u32 cp = decodeCp( base + i, end, seqLen );
    step( classifyUtf8( cp, nbspace ), s );
    i += seqLen;
  }
  return i;
}

// Hop the state machine over one block's masks. `n` = valid byte count (32
// for full blocks); bit i of sMask/pMask describes byte i. Stepping a
// multibyte code point once per byte is equivalent to once per code point:
// repeated Sep steps re-close a closed run and repeated word-byte steps
// re-open an open one, so only the transitions -- identical either way --
// matter.
inline void stepMasks(
    const u32 sMask, const u32 pMask, const u32 n, WordScan& s
)
{
  // Fast path: every byte is a separator or printable (no barren "Other"
  // bytes), which is all of real text. Then every run that ends in this block
  // had a printable -- except a run carried in from before that ends at bit 0
  // without gaining a byte here -- so the count is a popcount of the
  // nonsep->sep transitions and the carries fall out of the mask edges.
  const u32 nonSep = ~sMask;
  if ( n == 32 && ( nonSep & ~pMask ) == 0 ) {
    if ( !s.sawByte ) {
      s.sawByte = true;
      s.startsInWord = ( nonSep & 1u ) != 0;
    }
    const u32 ends = sMask & ( ( nonSep << 1 ) | ( s.inWord ? 1u : 0u ) );
    u32 ended = static_cast<u32>( __builtin_popcount( ends ) );
    if ( ( ends & 1u ) && !s.runHasPrintable ) --ended;  // carried run: barren
    s.words += ended;
    if ( sMask != 0 ) {
      if ( !s.sawSeparator && s.startsInWord ) {
        // The range's leading run ends at this block's first separator; it
        // has a printable if any of its bytes are in this block.
        s.leadingEnded = true;
        s.leadingHasPrintable = __builtin_ctz( sMask ) > 0 || s.runHasPrintable;
      }
      s.sawSeparator = true;
      const bool open = ( sMask >> 31 ) == 0;  // trailing run after last sep
      s.inWord = open;
      s.runHasPrintable = open;  // its bytes here are all printable
    } else {
      s.inWord = true;  // the whole block extends one printable run
      s.runHasPrintable = true;
    }
    return;
  }

  u32 i = 0;
  while ( i < n ) {
    if ( ( sMask >> i ) & 1u ) {
      if ( !s.sawByte ) {
        s.sawByte = true;
        s.startsInWord = false;
      }
      if ( s.inWord ) {
        if ( s.runHasPrintable ) ++s.words;
        if ( !s.sawSeparator && s.startsInWord ) {
          s.leadingEnded = true;
          s.leadingHasPrintable = s.runHasPrintable;
        }
        s.inWord = false;
        s.runHasPrintable = false;
      }
      s.sawSeparator = true;
      const u32 rest = ~( sMask >> i );
      i += rest == 0 ? ( n - i ) : static_cast<u32>( __builtin_ctz( rest ) );
    } else {
      if ( !s.sawByte ) {
        s.sawByte = true;
        s.startsInWord = true;
      }
      s.inWord = true;
      const u32 rest = sMask >> i;
      const u32 len =
          rest == 0 ? ( n - i ) : static_cast<u32>( __builtin_ctz( rest ) );
      const u32 segMask = len >= 32 ? ~0u : ( ( 1u << len ) - 1u );
      if ( ( pMask >> i ) & segMask ) s.runHasPrintable = true;
      i += len;
    }
  }
}

}  // namespace qwc::words_kernel
