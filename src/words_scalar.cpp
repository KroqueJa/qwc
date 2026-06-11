#include "words.h"

#include "iswprint_table.h"

// Unified scalar word counter -- the semantic reference for the SIMD kernels.
// A word is a maximal non-separator run containing at least one printable
// character, counted when it ends. Two parameterizations of one machine: the
// C locale (byte-wise: ASCII whitespace separators, 0x21-0x7E printable) and
// UTF-8 locales (decoded code points against the probe-pinned separator set
// and the generated glibc iswprint table). See include/words.h and
// docs/superpowers/specs/2026-06-10-unicode-whitespace-words-design.md.

namespace {

enum class Cls : u8 { Sep, Print, Other };

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
  if ( cp < kMin[need] || cp > 0x10FFFF ||
       ( cp >= 0xD800 && cp <= 0xDFFF ) )
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

}  // namespace

void words( const char* buf, const usize len, const usize ownedBegin,
            const usize ownedEnd, WordScan& s, const WordsMode& m )
{
  const u8* base = reinterpret_cast<const u8*>( buf );
  const u8* end = base + len;
  usize i = ownedBegin;

  if ( !m.utf8 ) {
    for ( ; i < ownedEnd; ++i ) step( classifyC( base[i] ), s );
    return;
  }

  // A continuation byte at the owned start belongs to a code point whose lead
  // sits in the context (the previous scan owned and classified it) -- skip
  // past it. If no valid sequence covers it, the bytes are stray continuations
  // owned here, classified one-by-one as invalid by the main loop.
  if ( i < ownedEnd && ( base[i] & 0xC0 ) == 0x80 ) {
    const usize back = i < 3 ? i : 3;
    for ( usize k = 1; k <= back; ++k ) {
      const u8 lead = base[i - k];
      if ( ( lead & 0xC0 ) == 0x80 ) continue;  // still a continuation byte
      usize seqLen = 1;
      if ( decodeCp( base + i - k, end, seqLen ) != 0xFFFFFFFF &&
           i - k + seqLen > i )
        i = i - k + seqLen;  // a valid sequence covers us: context owns it
      break;
    }
  }

  while ( i < ownedEnd ) {
    usize seqLen = 1;
    const u32 cp = decodeCp( base + i, end, seqLen );
    step( classifyUtf8( cp, m.nbspace ), s );
    i += seqLen;
  }
}
