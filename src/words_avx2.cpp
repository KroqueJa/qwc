#include <immintrin.h>

#include <algorithm>

#include "iswprint_table.h"
#include "words.h"

// AVX2 unified word counter. Per 32-byte block it builds two bitmasks -- S
// (separator bytes) and P (printable bytes) -- in vectors, then advances the
// run state machine over the masks with ctz hops (separators are sparse, so
// the walk visits runs, not bytes).
//
// C mode: S = ASCII whitespace, P = 0x21..0x7E, every full block vectorizes.
// UTF-8 mode vectorizes exactly the content it can classify bit-for-bit like
// the scalar kernel: ASCII, plus well-formed 2-byte sequences whose lead the
// generated kCandLead table certifies as "clean" (every code point in its row
// is printable, bar the exact C2 windows: U+0080-009F non-printable, U+00A0
// the nbspace separator). Anything else -- 3/4-byte sequences, invalid bytes,
// dirty leads like 0xCE (the U+03A2 unassigned hole) -- punts that one block
// to the scalar classifier, so the two kernels agree on ALL input, not just
// valid UTF-8. ASCII-dominant and Latin-ish text stays fully vectorized.

namespace {

enum class Cls : u8 { Sep, Print, Other };

// ---- scalar machinery: decodeCp/classifyC/classifyUtf8/step are duplicated
//      verbatim from words_scalar.cpp (the two TUs are never linked together;
//      mirror edits in both). ----

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

// Scalar walk of the owned subrange [from, stop) with full-buffer window
// [base, base+len): the entry path for a leading continuation byte, punted
// vector blocks, and the epilogue. Consumes whole code points (the last one
// may overhang `stop`); returns the first unconsumed position.
usize scalarUtf8( const u8* base, const usize len, const usize from,
                  const usize stop, WordScan& s, const bool nbspace )
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
inline void stepMasks( const u32 sMask, const u32 pMask, const u32 n,
                       WordScan& s )
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
        s.leadingHasPrintable =
            __builtin_ctz( sMask ) > 0 || s.runHasPrintable;
      }
      s.sawSeparator = true;
      const bool open = ( sMask >> 31 ) == 0;  // trailing run after last sep
      s.inWord = open;
      s.runHasPrintable = open;  // its bytes here are all printable
    } else {
      s.inWord = true;           // the whole block extends one printable run
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

// Unsigned in-range compare (bytes >= 0x80 are signed-negative, so bias by
// 0x80 first). Valid for lo >= 1 and hi <= 0xFE -- all uses here qualify.
inline __m256i rangeU( const __m256i v, const u8 lo, const u8 hi )
{
  const __m256i x =
      _mm256_xor_si256( v, _mm256_set1_epi8( static_cast<char>( 0x80 ) ) );
  return _mm256_and_si256(
      _mm256_cmpgt_epi8(
          x, _mm256_set1_epi8( static_cast<char>( ( lo ^ 0x80 ) - 1 ) ) ),
      _mm256_cmpgt_epi8(
          _mm256_set1_epi8( static_cast<char>( ( hi ^ 0x80 ) + 1 ) ), x ) );
}

inline u32 mm( const __m256i v )
{
  return static_cast<u32>( _mm256_movemask_epi8( v ) );
}

// ASCII separator/printable masks for one block. Bytes >= 0x80 compare
// signed-negative, so they fall in neither mask.
inline u32 asciiSep( const __m256i v )
{
  const __m256i tabCr =
      _mm256_and_si256( _mm256_cmpgt_epi8( v, _mm256_set1_epi8( 0x08 ) ),
                        _mm256_cmpgt_epi8( _mm256_set1_epi8( 0x0E ), v ) );
  return mm( _mm256_or_si256(
      tabCr, _mm256_cmpeq_epi8( v, _mm256_set1_epi8( 0x20 ) ) ) );
}

inline u32 asciiPrint( const __m256i v )
{
  return mm(
      _mm256_and_si256( _mm256_cmpgt_epi8( v, _mm256_set1_epi8( 0x20 ) ),
                        _mm256_cmpgt_epi8( _mm256_set1_epi8( 0x7F ), v ) ) );
}

}  // namespace

void words( const char* buf, const usize len, const usize ownedBegin,
            const usize ownedEnd, WordScan& s, const WordsMode& m )
{
  const u8* base = reinterpret_cast<const u8*>( buf );
  usize i = ownedBegin;

  if ( !m.utf8 ) {
    // C parameterization: purely bytewise, every full block vectorizes.
    for ( ; i + 32 <= ownedEnd; i += 32 ) {
      const __m256i v =
          _mm256_loadu_si256( reinterpret_cast<const __m256i*>( base + i ) );
      stepMasks( asciiSep( v ), asciiPrint( v ), 32, s );
    }
    for ( ; i < ownedEnd; ++i ) step( classifyC( base[i] ), s );
    return;
  }

  // UTF-8 parameterization. Each vector block needs one byte of lookahead
  // (the second byte of a window whose lead sits at bit 31), hence the
  // i + 33 <= len bound. The carries thread a block-straddling sequence into
  // the next block: carryLead = a clean lead at bit 31 (its continuation is
  // the next block's bit 0), carryS/carryN = that continuation's smeared
  // class. They live in locals, not WordScan, because no block ever straddles
  // a words() call -- the epilogue is always scalar.
  u32 carryS = 0, carryN = 0, carryLead = 0;
  while ( i + 33 <= len && i + 32 <= ownedEnd ) {
    const __m256i v =
        _mm256_loadu_si256( reinterpret_cast<const __m256i*>( base + i ) );
    u32 sMask = asciiSep( v );
    u32 pMask = asciiPrint( v );
    const u32 high = mm( v );
    if ( high != 0 ) {
      // The block vectorizes only if every high byte is part of a well-formed
      // 2-byte sequence (lead C2..DF + exactly one continuation, including a
      // pair straddling the block edge) whose lead is clean per kCandLead.
      const u32 lead2 = mm( rangeU( v, 0xC2, 0xDF ) );
      const u32 cont = mm( rangeU( v, 0x80, 0xBF ) );
      bool clean = high == ( lead2 | cont ) &&
                   cont == ( ( lead2 << 1 ) | carryLead );
      if ( clean && ( lead2 >> 31 ) != 0 &&
           ( base[i + 32] & 0xC0 ) != 0x80 )
        clean = false;  // lead at bit 31 with no continuation after the block
      if ( clean ) {
        u32 leads = lead2;
        while ( leads != 0 ) {
          const u32 b = static_cast<u32>( __builtin_ctz( leads ) );
          leads &= leads - 1;
          if ( kCandLead[base[i + b]] != 0 ) {
            clean = false;
            break;
          }
        }
      }
      if ( !clean ) {
        // Scalar-classify just this block; it consumes whole code points, so
        // resume at the first unconsumed byte with no pending carries.
        i = scalarUtf8( base, len, i, std::min( i + 32, ownedEnd ), s,
                        m.nbspace );
        carryS = carryN = carryLead = 0;
        continue;
      }

      // Exact C2 windows, masked at the lead position via 1-byte lookahead.
      const __m256i v1 = _mm256_loadu_si256(
          reinterpret_cast<const __m256i*>( base + i + 1 ) );
      const u32 isC2 =
          mm( _mm256_cmpeq_epi8( v, _mm256_set1_epi8( '\xC2' ) ) );
      const u32 s2 =
          m.nbspace
              ? isC2 &
                    mm( _mm256_cmpeq_epi8( v1, _mm256_set1_epi8( '\xA0' ) ) )
              : 0;  // POSIXLY_CORRECT: NBSP is printable word content instead
      const u32 n2 = isC2 & mm( rangeU( v1, 0x80, 0x9F ) );  // C1 controls

      // Smear each window across both bytes of its sequence; every other
      // high byte belongs to a printable 2-byte code point (that is what
      // kCandLead certified).
      const u32 sSm = s2 | ( s2 << 1 ) | carryS;
      const u32 nSm = n2 | ( n2 << 1 ) | carryN;
      sMask |= sSm;
      pMask |= high & ~( sSm | nSm );
      pMask &= ~( sSm | nSm );
      carryS = s2 >> 31;
      carryN = n2 >> 31;
      carryLead = lead2 >> 31;
    } else {
      carryS = carryN = carryLead = 0;
    }
    stepMasks( sMask, pMask, 32, s );
    i += 32;
  }

  scalarUtf8( base, len, i, ownedEnd, s, m.nbspace );
}
